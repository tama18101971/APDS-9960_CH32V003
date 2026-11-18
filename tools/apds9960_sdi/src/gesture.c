#include <stdbool.h>
#include <stdint.h>

#include "apds9960.h"
#include "apds9960_regs.h"
#include "debug.h"
#include "i2c.h"
#include "sdi_log.h"

#define I2C_HZ 100000u

#ifdef APDS9960_DEBUG
void __wrap_apds_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    sdi_vlog(format, args);
    va_end(args);
}
#endif

static bool report_config(void)
{
    const uint8_t registers[] = {
        REG_ID, REG_GPENTH, REG_GEXTH, REG_CONTROL, REG_CONFIG2, REG_GCONF2
    };
    unsigned values[sizeof(registers)];
    for (unsigned i = 0; i < sizeof(registers); ++i)
    {
        uint8_t value;
        unsigned status = i2c_read_register(APDS9960_I2C_ADDR, registers[i],
                                            &value);
        if (status != I2C_OK)
        {
            sdi_log("FAULT config=%02X raw=%u\r\n",
                    (unsigned)registers[i], status);
            return false;
        }
        values[i] = value;
    }
    sdi_log("READY id=%02X enter=%u exit=%u control=%02X config2=%02X gconf2=%02X hz=%u\r\n",
            values[0], values[1], values[2], values[3], values[4], values[5],
            I2C_HZ);
    return true;
}

static void recognition_loop(void)
{
    static const char *const names[] = {"NONE", "LEFT", "RIGHT", "UP", "DOWN"};
    unsigned t = 0;
    unsigned low = 255;
    unsigned high = 0;
    for (;;)
    {
        bool ready = apds_available();
        unsigned error = apds_getLastError();
        if (ready)
        {
            gesture_t gesture = apds_readGesture();
            error = apds_getLastError();
            sdi_log("RESULT %s error=%u raw=%u\r\n", names[gesture],
                    error, error ? (unsigned)apds_getLastI2CStatus() : 0u);
        }
        else if (error != APDS_ERR_NONE)
        {
            sdi_log("FAULT poll=%u raw=%u\r\n",
                    error, (unsigned)apds_getLastI2CStatus());
        }
        if (error == APDS_ERR_SENSOR_HANG ||
            (error == APDS_ERR_I2C && apds_getLastI2CStatus() == I2C_ERR_RECOVERY))
        {
            return;
        }
        if (error == APDS_ERR_NONE && (t & 7u) == 0)
        {
            uint8_t proximity;
            if (!apds_readProximity(&proximity))
            {
                sdi_log("FAULT proximity=%u raw=%u\r\n",
                        (unsigned)apds_getLastError(),
                        (unsigned)apds_getLastI2CStatus());
            }
            else
            {
                if (proximity < low)
                {
                    low = proximity;
                }
                if (proximity > high)
                {
                    high = proximity;
                }
            }
        }
        if ((++t & 511u) == 0 && low <= high)
        {
            sdi_log("PROX low=%u high=%u\r\n", low, high);
            low = 255;
            high = 0;
        }
        Delay_Ms(error == APDS_ERR_NONE ? 1 : 20);
    }
}

int main(void)
{
    SystemCoreClockUpdate();
    SDI_Printf_Enable();
    Delay_Ms(1000);
    for (;;)
    {
        unsigned status = i2c_init(I2C_HZ);
        if (status != I2C_OK)
        {
            sdi_log("FAULT init=%u sda=%u scl=%u\r\n", status,
                    (unsigned)GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_1),
                    (unsigned)GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_2));
        }
        else if (!apds_init())
        {
            sdi_log("FAULT apds_init=%u raw=%u\r\n",
                    (unsigned)apds_getLastError(),
                    (unsigned)apds_getLastI2CStatus());
        }
        else if (report_config())
        {
            recognition_loop();
        }
        Delay_Ms(1000);
    }
}
