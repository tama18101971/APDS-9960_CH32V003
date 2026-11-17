/* Optional on-device APDS health diagnostics. No I2C implementation access. */
#ifdef APDS_HEALTH_TEST
#include <ch32v00x.h>
#include <debug.h>
#include "i2c.h"
#include "apds9960.h"
#include "apds9960_log.h"
#include "apds9960_regs.h"
void apds_health_check(void) {
    for (uint8_t pass=0;pass<2;++pass) {
        uint32_t speed=pass?100000u:400000u;
        uint8_t st=i2c_init(speed);
        apds_log("HEALTH speed=%u init=%u\r\n",speed,st);
        if (st) {
            apds_log("HEALTH LINES SDA=%u SCL=%u\r\n",
                     GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_1),
                     GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_2));
            continue;
        }
        st=i2c_write_register(0x39,REG_ENABLE,0);
        apds_log("HEALTH disable=%u\r\n",st);
        Delay_Ms(10);
        uint8_t errors[7]={0},good=0,last_id=0,bad_id=0;
        for (uint8_t i=0;i<100;++i) {
            uint8_t id=0;
            st=i2c_read_register(0x39,REG_ID,&id);
            if (st<7) ++errors[st];
            if (!st) { ++good;last_id=id;if(id!=0x9E)++bad_id; }
            Delay_Ms(2);
        }
        apds_log("HEALTH ID good=%u bad=%u last=%02X errors=%u,%u,%u,%u,%u,%u\r\n",
                 good,bad_id,last_id,errors[1],errors[2],errors[3],errors[4],errors[5],errors[6]);
        uint8_t matches=0,werr=0,rerr=0;
        for (uint8_t i=0;i<32;++i) {
            uint8_t value=(i&1)?0xFE:0xFF, actual=0;
            st=i2c_write_register(0x39,REG_WTIME,value);
            if(st)++werr;
            st=i2c_read_register(0x39,REG_WTIME,&actual);
            if(st)++rerr;
            else if(actual==value)++matches;
            Delay_Ms(2);
        }
        apds_log("HEALTH WTIME match=%u/32 write_errors=%u read_errors=%u\r\n",matches,werr,rerr);
        bool ok=apds_init();
        apds_log("HEALTH APDS init=%u err=%u raw=%u\r\n",ok,apds_getLastError(),apds_getLastI2CStatus());
        const uint8_t addresses[]={REG_ENABLE,REG_CONTROL,REG_CONFIG1,REG_CONFIG2,REG_GCONF1,REG_GCONF2,REG_GCONF4,REG_GPENTH,REG_GEXTH,REG_STATUS,REG_PDATA,REG_GSTATUS,REG_GFLVL};
        for(uint8_t i=0;i<sizeof(addresses);++i){
            uint8_t value=0;st=i2c_read_register(0x39,addresses[i],&value);
            apds_log("HEALTH reg=%02X value=%02X status=%u\r\n",addresses[i],value,st);
        }
        if(ok){
            uint16_t passed=0;
            uint8_t gs_st=0,id_st=0,probe_id=0;
            for(uint16_t n=0;n<500;++n){
                uint8_t probe_gs=0;
                gs_st=i2c_read_register(0x39,REG_GSTATUS,&probe_gs);
                id_st=i2c_read_register(0x39,REG_ID,&probe_id);
                if(gs_st || id_st || probe_id!=0x9E) break;
                ++passed;
                Delay_Ms(20);
            }
            apds_log("HEALTH ACTIVE passed=%u/500 GS_status=%u ID_status=%u ID=%02X\r\n",passed,gs_st,id_st,probe_id);
            Delay_Ms(500);
            uint8_t gs=0,level=0;
            uint8_t s1=i2c_read_register(0x39,REG_GSTATUS,&gs);
            uint8_t s2=i2c_read_register(0x39,REG_GFLVL,&level);
            apds_log("HEALTH FIFO gs=%02X level=%u statuses=%u,%u\r\n",gs,level,s1,s2);
            (void)apds_available();
            apds_log("HEALTH service err=%u raw=%u\r\n",apds_getLastError(),apds_getLastI2CStatus());
        }
    }
    bool stopped=apds_shutdown();
    apds_log("HEALTH SHUTDOWN=%u\r\n",stopped);
    apds_log("HEALTH DONE\r\n");
    while(1){}
}
#endif
