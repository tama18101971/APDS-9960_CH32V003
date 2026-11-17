/* Includes the unmodified driver. Only public I2C functions and SDK are mocked. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/apds9960.c"
SysTick_Type mock_tick;
uint32_t SystemCoreClock=48000000;
SysTick_Type *mock_systick_access(void) { if(mock_tick.CTLR&1) mock_tick.CNT+=6000; return &mock_tick; }
static void mock_sensor_delay(uint32_t n);
void Delay_Ms(uint32_t n) { mock_sensor_delay(n); mock_tick.SR=0; mock_tick.CMP=n*6000; mock_tick.CNT=mock_tick.CMP; mock_tick.CTLR&=~1u; }
static uint8_t regs[256], fifo[32][4], samples[64];
static unsigned count,offset,block_calls,bytes_read,max_len,read_calls,write_calls,pdata_pos,pdata_n,gs_reads;
static int fail_read_reg,fail_write_reg,fail_write_once,fail_block_at,forced_level,hold_valid,overflow_after_read;
static uint8_t fail_code;
static int fresh_proximity=1;
static void mock_sensor_delay(uint32_t n) { if(n && fresh_proximity) regs[REG_STATUS]|=ST_PVALID; }
static unsigned failures,checks,observations;
#define CHECK(c) do { checks++; if(!(c)) { failures++; printf("FAIL line %d: %s\n",__LINE__,#c); } }while(0)
#define OBS(name,c) do { checks++; if(c) { observations++; printf("REPRO %s\n",name); } else {failures++; printf("NOT REPRODUCED %s line %d\n",name,__LINE__);} }while(0)
static void reset_model(void) {
 memset(regs,0,sizeof(regs)); memset(fifo,0,sizeof(fifo)); memset(&mock_tick,0,sizeof(mock_tick));
 count=offset=block_calls=bytes_read=max_len=read_calls=write_calls=pdata_pos=pdata_n=gs_reads=0;
 fail_read_reg=fail_write_reg=-1; fail_write_once=fail_block_at=hold_valid=overflow_after_read=0; forced_level=-1; fail_code=I2C_NACK;
 g_sensor_id=0xAB;g_irq_enabled=APDS_INT_MODE;g_discard_pending=0;fresh_proximity=1;regs[REG_ID]=0xAB; regs[REG_STATUS]=ST_PVALID; regs[REG_PDATA]=160;
 gesture_reset(); g_reinit_count=0; g_last_error=0; g_last_i2c_status=0; g_cal_valid=0;
}
static void load_axis(unsigned n,int axis,int direction) {
 count=n; offset=0; regs[REG_GSTATUS]=GST_GVALID;
 for(unsigned i=0;i<n;i++) {
   for(unsigned j=0;j<4;j++) fifo[i][j]=100;
   int low=(i<n/2)?40:160; int high=200-low;
   fifo[i][axis]=direction>0?low:high; fifo[i][axis+1]=direction>0?high:low;
 }
}
uint8_t i2c_read_register(uint8_t dev,uint8_t reg,uint8_t *value) {
 read_calls++; if(dev!=0x39) abort(); if(reg==REG_GSTATUS) gs_reads++;
 if((int)reg==fail_read_reg) return fail_code;
 if(!value) { /* Do not invoke UB in the model; note forwarding separately. */ return I2C_NACK; }
 if(reg==REG_GFLVL) *value=(forced_level>=0)?(uint8_t)forced_level:(uint8_t)count;
 else if(reg==REG_PDATA && pdata_n) *value=samples[(pdata_pos++ < pdata_n)?pdata_pos-1:pdata_n-1];
 else *value=regs[reg];
 if(reg==REG_PDATA) regs[REG_STATUS]&=(uint8_t)~ST_PVALID;
 return I2C_OK;
}
uint8_t i2c_write_register(uint8_t dev,uint8_t reg,uint8_t value) {
 write_calls++; if(dev!=0x39) abort();
 if((int)reg==fail_write_reg || (fail_write_once && write_calls==(unsigned)fail_write_once)) return fail_code;
 if(reg==REG_GCONF4 && (value&4)) { count=offset=0; regs[REG_GSTATUS]=0; }
 regs[reg]=value; /* ENABLE=0 is oscillator/engine disable, not POR or FIFO_CLR. */
 return I2C_OK;
}
uint8_t i2c_read_buffer(uint8_t dev,uint8_t reg,uint8_t *buf,uint16_t len) {
 block_calls++; if(dev!=0x39 || reg!=0xFC || len%4 || len>32) abort();
 if(len>max_len) max_len=len;
 if(fail_block_at && block_calls==(unsigned)fail_block_at) { if(len) buf[0]=0xEE; return fail_code; }
 for(unsigned i=0;i<len;i++) buf[i]=(i/4<count)?fifo[offset+i/4][i%4]:0;
 unsigned n=len/4; if(n>count)n=count; count-=n; offset+=n; bytes_read+=len;
 if(count==0 && !hold_valid) regs[REG_GSTATUS]&=(uint8_t)~GST_GVALID;
 if(overflow_after_read) regs[REG_GSTATUS]|=GST_GFOV;
 return I2C_OK;
}
static void test_api(void) {
 reset_model();CHECK(configure_registers());CHECK(write_calls==23);
 reset_model();fail_write_once=22;CHECK(apds_init());
 reset_model();fail_read_reg=REG_ENABLE;CHECK(!apds_init());CHECK(apds_getLastError()==APDS_ERR_SENSOR_HANG);
 reset_model();fail_write_once=2;CHECK(!apds_wakeup());CHECK(regs[REG_ENABLE]==EN_PON);CHECK(apds_getLastError()==APDS_ERR_I2C);
 reset_model();fail_read_reg=REG_ENABLE;CHECK(!apds_wakeup());CHECK(apds_getLastI2CStatus()==I2C_NACK);
 reset_model();g_reinit_count=3;CHECK(apds_getReinitCount()==3);
#if APDS_ENABLE_CALIBRATION
 reset_model();fail_write_once=24;CHECK(!apds_init());CHECK(apds_getLastError()==APDS_ERR_I2C);
 reset_model();fail_write_once=25;CHECK(!apds_init());CHECK(apds_getLastError()==APDS_ERR_I2C);
#endif

 const uint8_t ids[]={0xAB,0x9C,0xA8,0x9E};
 for(unsigned i=0;i<4;i++){reset_model();regs[REG_ID]=ids[i];CHECK(apds_init());CHECK(apds_getLastError()==0);}
 reset_model();regs[REG_ID]=0;CHECK(!apds_init());CHECK(g_last_error==APDS_ERR_INVALID_ID);CHECK(write_calls==0);
 reset_model();fail_read_reg=REG_ID;CHECK(!apds_init());CHECK(g_last_error==APDS_ERR_I2C);
 for(unsigned c=1;c<=5;c++){reset_model();fail_code=c;fail_read_reg=REG_PDATA;uint8_t v=77;CHECK(!apds_readProximity(&v));CHECK(v==77);CHECK(g_last_error==APDS_ERR_I2C);CHECK(g_last_i2c_status==c);fail_read_reg=-1;CHECK(apds_readProximity(&v));CHECK(g_last_error==0);CHECK(g_last_i2c_status==c);}
 reset_model();uint8_t v=0;CHECK(apds_readStatus(&v)&&v==ST_PVALID);fail_read_reg=REG_STATUS;CHECK(!apds_readStatus(&v));
 const uint8_t wrregs[]={REG_ENABLE,REG_ATIME,REG_WTIME,REG_PPULSE,REG_POFFSET_UR,REG_POFFSET_DL,REG_CONTROL,REG_CONFIG1,REG_CONFIG2,REG_CONFIG3,REG_GPENTH,REG_GEXTH,REG_GCONF1,REG_GCONF2,REG_GOFFSET_U,REG_GOFFSET_D,REG_GOFFSET_L,REG_GOFFSET_R,REG_GPULSE,REG_GCONF3,REG_GCONF4};
 for(unsigned i=0;i<sizeof(wrregs);i++){reset_model();fail_write_reg=wrregs[i];CHECK(!apds_init());CHECK(g_last_error==APDS_ERR_SENSOR_HANG);}
 reset_model();fail_write_once=1;CHECK(apds_init());
 reset_model();CHECK(apds_sleep());CHECK(regs[REG_ENABLE]==EN_PON);CHECK(apds_wakeup());CHECK(regs[REG_ENABLE]==(EN_PON|EN_PEN|EN_GEN|EN_WEN));CHECK(apds_shutdown());CHECK(regs[REG_ENABLE]==0);CHECK(apds_wakeup());
 reset_model();fail_write_reg=REG_ENABLE;CHECK(!apds_sleep());CHECK(!apds_shutdown());CHECK(!apds_wakeup());
 reset_model();fail_read_reg=REG_GSTATUS;CHECK(!apds_available());CHECK(g_last_error==APDS_ERR_I2C);
 reset_model();CHECK(!apds_available());CHECK(g_last_error==0);regs[REG_GSTATUS]=GST_GVALID;CHECK(apds_available());
 reset_model();fail_write_reg=REG_GCONF4;apds_enableInterrupt();CHECK(g_last_error==APDS_ERR_I2C);apds_disableInterrupt();CHECK(g_last_error==APDS_ERR_I2C);fail_write_reg=-1;apds_enableInterrupt();CHECK(g_last_error==APDS_ERR_I2C);fail_read_reg=REG_GSTATUS;apds_clearInterrupt();CHECK(g_last_error==APDS_ERR_I2C);
}
static void test_fifo_math(void) {
 for(unsigned d=4;d<=6;d++){reset_model();load_axis(4,0,1);for(unsigned i=0;i<4;i++){fifo[i][0]=100+(i>=2?d:0);fifo[i][1]=100-(i>=2?d:0);}CHECK(apds_readGesture()==(d<APDS_GESTURE_SENSITIVITY?GESTURE_NONE:GESTURE_DOWN));}
 reset_model();count=4;regs[REG_GSTATUS]=1;memset(fifo,250,sizeof(fifo));CHECK(process_fifo_batch());CHECK(g_packet_count==4);
 reset_model();count=4;regs[REG_GSTATUS]=1;memset(fifo,251,sizeof(fifo));CHECK(process_fifo_batch());CHECK(g_packet_count==0);
 reset_model();count=4;regs[REG_GSTATUS]=1;memset(fifo,9,sizeof(fifo));CHECK(process_fifo_batch());CHECK(g_packet_count==0);
 reset_model();count=4;regs[REG_GSTATUS]=1;memset(fifo,10,sizeof(fifo));CHECK(process_fifo_batch());CHECK(g_packet_count==4);
 reset_model();load_axis(8,0,1);for(unsigned i=0;i<8;i++){fifo[i][2]=fifo[i][0];fifo[i][3]=fifo[i][1];}CHECK(apds_readGesture()==GESTURE_RIGHT); /* documented tie policy */
 reset_model();load_axis(8,0,1);for(unsigned i=6;i<8;i++){fifo[i][0]=40;fifo[i][1]=160;}CHECK(apds_readGesture()==GESTURE_NONE); /* return motion cancels */

 reset_model();CHECK(process_fifo_batch());CHECK(block_calls==0);
 for(unsigned n=1;n<=32;n++){reset_model();load_axis(n,0,1);CHECK(process_fifo_batch());CHECK(count==0);CHECK(bytes_read==4*n);CHECK(block_calls==(n+7)/8);CHECK(g_packet_count==n);CHECK(max_len<=32);}
 reset_model();forced_level=255;load_axis(32,0,1);CHECK(process_fifo_batch());CHECK(bytes_read==128&&block_calls==4);
 for(unsigned axis=0;axis<=2;axis+=2)for(int dir=-1;dir<=1;dir+=2){reset_model();load_axis(8,axis,dir);gesture_t g=apds_readGesture();gesture_t expected=axis==0?(dir>0?GESTURE_DOWN:GESTURE_UP):(dir>0?GESTURE_RIGHT:GESTURE_LEFT);CHECK(g==expected);CHECK(g_last_error==0);}
 for(unsigned n=1;n<4;n++){reset_model();load_axis(n,0,1);CHECK(apds_readGesture()==GESTURE_NONE);}
 for(unsigned val=0;val<=255;val++){reset_model();count=4;regs[REG_GSTATUS]=1;memset(fifo,val,sizeof(fifo));CHECK(apds_readGesture()==GESTURE_NONE);}
 reset_model();load_axis(4,0,1);fifo[0][0]=251;CHECK(apds_readGesture()==GESTURE_NONE);
 for(unsigned k=1;k<=4;k++){reset_model();load_axis(32,0,1);fail_block_at=k;CHECK(apds_readGesture()==GESTURE_NONE);CHECK(g_last_error==APDS_ERR_I2C);}
 reset_model();fail_read_reg=REG_GFLVL;regs[REG_GSTATUS]=1;CHECK(apds_readGesture()==GESTURE_NONE);CHECK(g_last_error==APDS_ERR_I2C);
 reset_model();load_axis(8,0,1);CHECK(apds_readGesture()==GESTURE_DOWN);load_axis(8,0,-1);CHECK(apds_readGesture()==GESTURE_UP);
 reset_model();load_axis(32,0,1);for(unsigned i=0;i<9;i++){offset=0;count=32;CHECK(process_fifo_batch());}CHECK(g_packet_count==255);
#if APDS_ENABLE_CALIBRATION
 for(unsigned n=0;n<65536;n++){unsigned x=isqrt((uint16_t)n);CHECK(x*x<=n&&(x+1)*(x+1)>n);}
 uint8_t a[]={255,0,128,1,200,2};sort_u8(a,6);for(unsigned i=1;i<6;i++)CHECK(a[i-1]<=a[i]);
#endif
}
static void reproduce(void) {
#if APDS_CLONE_A8_PROFILE
 reset_model();regs[REG_ID]=0xA8;CHECK(apds_init());CHECK(regs[REG_GCONF2]==((1u<<5)|(2u<<3)|APDS_GWTIME));
 g_discard_pending=1;CHECK(!apds_available());CHECK(regs[REG_GCONF2]==((1u<<5)|(2u<<3)|APDS_GWTIME));
#endif

 reset_model();g_sensor_id=0x9E;CHECK(configure_registers());CHECK(regs[REG_GCONF2]==((3u<<3)|APDS_GWTIME));
 reset_model();CHECK(configure_registers());CHECK(regs[REG_GCONF2]==((APDS_GGAIN<<5)|(APDS_GLDRIVE<<3)|APDS_GWTIME));
 reset_model();regs[REG_GCONF4]=0;apds_disableInterrupt();CHECK(regs[REG_GCONF4]==0);
 reset_model();apds_disableInterrupt();regs[REG_GSTATUS]=3;CHECK(!apds_available());CHECK(!(regs[REG_GCONF4]&2));CHECK(regs[REG_GSTATUS]==0);
 reset_model();load_axis(8,0,1);hold_valid=1;CHECK(apds_readGesture()==GESTURE_NONE);CHECK(g_last_error==APDS_ERR_TIMEOUT);
 reset_model();load_axis(8,0,1);overflow_after_read=1;CHECK(apds_readGesture()==GESTURE_NONE);CHECK(g_last_error==APDS_ERR_FIFO_OVERFLOW);
 reset_model();load_axis(16,0,1);fail_block_at=2;CHECK(apds_readGesture()==GESTURE_NONE);fail_block_at=0;CHECK(apds_readGesture()==GESTURE_NONE);CHECK(count==0);
 reset_model();load_axis(8,0,1);CHECK(apds_sleep());CHECK(apds_wakeup());CHECK(apds_readGesture()==GESTURE_NONE);
 CHECK(!apds_readProximity(NULL));CHECK(g_last_error==APDS_ERR_INVALID_ARG);
#if APDS_ENABLE_CALIBRATION
 reset_model();CHECK(apds_recalibrate());regs[REG_PDATA]=255;CHECK(apds_recalibrate());CHECK(regs[REG_GPENTH]==APDS_PROX_THRESHOLD);CHECK(regs[REG_GEXTH]==APDS_GESTURE_EXIT_TH);
 reset_model();pdata_n=32;for(unsigned i=0;i<32;i++)samples[i]=i<16?0:200;CHECK(apds_recalibrate());CHECK(regs[REG_GEXTH]<=regs[REG_GPENTH]);
#else
 CHECK(!apds_recalibrate());CHECK(g_last_error==APDS_ERR_UNSUPPORTED);
#endif
}

int main(void){test_api();test_fifo_math();reproduce();printf("RESULT checks=%u reproduced=%u harness_failures=%u own=%d cal=%d max=%d\n",checks,observations,failures,APDS_OWN_SYSTICK,APDS_ENABLE_CALIBRATION,APDS_CAL_PROX_MAX);return failures?1:0;}
