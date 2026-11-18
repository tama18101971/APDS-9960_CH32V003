#include <stdbool.h>
#include <stdint.h>

#include "debug.h"
#include "i2c.h"
#include "sdi_log.h"

#define APDS_ADDRESS 0x39u

enum
{
    REG_ENABLE = 0x80,
    REG_ATIME = 0x81,
    REG_WTIME = 0x83,
    REG_PPULSE = 0x8E,
    REG_CONTROL = 0x8F,
    REG_CONFIG2 = 0x90,
    REG_ID = 0x92,
    REG_STATUS = 0x93,
    REG_COLOR = 0x94,
    REG_PROXIMITY = 0x9C,
    REG_GEXTH = 0xA1,
    REG_GCONF1 = 0xA2,
    REG_GCONF2 = 0xA3,
    REG_GPULSE = 0xA6,
    REG_GCONF3 = 0xAA,
    REG_GCONF4 = 0xAB,
    REG_GFLVL = 0xAE,
    REG_GSTATUS = 0xAF,
    REG_GFIFO = 0xFC,
    STATUS_READY = 0x03, /* Both AVALID and PVALID */
    ID_READS = 32,
    SAMPLES = 100
};

static bool write_checked(uint8_t reg, uint8_t value)
{
    uint8_t actual = 0;
    unsigned status = i2c_write_register(APDS_ADDRESS, reg, value);
    if (status == I2C_OK)
    {
        status = i2c_read_register(APDS_ADDRESS, reg, &actual);
    }
    if (status != I2C_OK || actual != value)
    {
        sdi_log("REG reg=%02X wrote=%02X read=%02X error=%u\r\n",
                (unsigned)reg, (unsigned)value, (unsigned)actual, status);
        return false;
    }
    return true;
}

static bool check_id(void)
{
    uint8_t id = 0;
    unsigned matches = 0;
    unsigned error = 0;
    for (unsigned i = 0; i < ID_READS; ++i)
    {
        uint8_t value = 0;
        error = i2c_read_register(APDS_ADDRESS, REG_ID, &value);
        if (error != I2C_OK || (i != 0 && value != id))
        {
            break;
        }
        id = value;
        ++matches;
        Delay_Ms(2);
    }
    bool known = id == 0xAB || id == 0x9C || id == 0xA8 || id == 0x9E;
    sdi_log("ID value=%02X matches=%u error=%u known=%u\r\n",
            (unsigned)id, matches, error, (unsigned)known);
    return known && matches == ID_READS;
}

static unsigned read_sample(unsigned index)
{
    uint8_t status = 0;
    uint8_t proximity = 0;
    uint8_t rgbc[8];
    unsigned error = i2c_read_register(APDS_ADDRESS, REG_STATUS, &status);
    if (error == I2C_OK && (status & STATUS_READY) != STATUS_READY)
    {
        sdi_log("WAIT n=%u status=%02X\r\n", index, (unsigned)status);
        return 0;
    }
    if (error == I2C_OK)
    {
        error = i2c_read_register(APDS_ADDRESS, REG_PROXIMITY, &proximity);
    }
    if (error == I2C_OK)
    {
        error = i2c_read_buffer(APDS_ADDRESS, REG_COLOR, rgbc, sizeof(rgbc));
    }
    if (error != I2C_OK)
    {
        sdi_log("ERROR n=%u i2c=%u\r\n", index, error);
        return error;
    }

    unsigned counts[4];
    for (unsigned i = 0; i < 4; ++i)
    {
        counts[i] = ((unsigned)rgbc[2 * i + 1] << 8) | rgbc[2 * i];
    }
    sdi_log("SAMPLE n=%u status=%02X p=%u c=%u r=%u g=%u b=%u\r\n",
            index, (unsigned)status, (unsigned)proximity,
            counts[0], counts[1], counts[2], counts[3]);
    return 0;
}

static bool check_gesture_fifo(void)
{
    /* Force the gesture engine for a raw FIFO check independent of motion.
     * GEXTH=0 prevents automatic exit. Use 1x gain and 12.5 mA LED drive. */
    if (!write_checked(REG_ENABLE, 0x01) ||
        !write_checked(REG_GEXTH, 0x00) ||
        !write_checked(REG_GCONF1, 0x00) ||
        !write_checked(REG_GCONF2, 0x1D) ||
        !write_checked(REG_GPULSE, 0xC9) ||
        !write_checked(REG_GCONF3, 0x00) ||
        !write_checked(REG_ENABLE, 0x45))
    {
        return false;
    }

    unsigned error = i2c_write_register(APDS_ADDRESS, REG_GCONF4, 0x05);
    unsigned frames = 0;
    unsigned overflow = 0;
    uint8_t minimum[4] = {255, 255, 255, 255};
    uint8_t maximum[4] = {0, 0, 0, 0};
    for (unsigned i = 0; i < 100 && error == I2C_OK && !overflow; ++i)
    {
        Delay_Ms(20);
        uint8_t status = 0;
        error = i2c_read_register(APDS_ADDRESS, REG_GSTATUS, &status);
        overflow = (status & 0x02u) != 0;
        if (error != I2C_OK || overflow || !(status & 0x01u))
        {
            continue;
        }

        uint8_t levels = 0;
        error = i2c_read_register(APDS_ADDRESS, REG_GFLVL, &levels);
        for (unsigned j = 0; j < levels && j < 8 && error == I2C_OK; ++j)
        {
            uint8_t channels[4];
            error = i2c_read_buffer(APDS_ADDRESS, REG_GFIFO, channels,
                                    sizeof(channels));
            if (error != I2C_OK)
            {
                break;
            }
            ++frames;
            for (unsigned channel = 0; channel < 4; ++channel)
            {
                uint8_t value = channels[channel];
                if (value < minimum[channel])
                {
                    minimum[channel] = value;
                }
                if (value > maximum[channel])
                {
                    maximum[channel] = value;
                }
            }
        }
    }
    sdi_log("GESTURE frames=%u error=%u overflow=%u\r\n",
            frames, error, overflow);
    if (frames != 0)
    {
        sdi_log("UDLR u=%u..%u d=%u..%u l=%u..%u r=%u..%u\r\n",
                (unsigned)minimum[0], (unsigned)maximum[0],
                (unsigned)minimum[1], (unsigned)maximum[1],
                (unsigned)minimum[2], (unsigned)maximum[2],
                (unsigned)minimum[3], (unsigned)maximum[3]);
    }
    return frames != 0 && error == I2C_OK && !overflow;
}

static void run_diagnostic(void)
{
    sdi_log("\r\nAPDS9960 sdi=1 address=39 hz=100000\r\n");
    unsigned status = i2c_init(100000);
    sdi_log("I2C init=%u sda=%u scl=%u\r\n", status,
            (unsigned)GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_1),
            (unsigned)GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_2));
    if (status != I2C_OK || !check_id())
    {
        sdi_log("DONE stage=bus\r\n");
        return;
    }

    const bool registers_ok = write_checked(REG_ENABLE, 0x00) &&
                              write_checked(REG_WTIME, 0xFE) &&
                              write_checked(REG_WTIME, 0xFF) &&
                              write_checked(REG_ATIME, 0xDB) &&
                              write_checked(REG_CONTROL, 0xC4) &&
                              write_checked(REG_CONFIG2, 0x01) &&
                              write_checked(REG_PPULSE, 0x47) &&
                              write_checked(REG_ENABLE, 0x01);
    Delay_Ms(10);
    if (!registers_ok || !write_checked(REG_ENABLE, 0x07))
    {
        status = i2c_write_register(APDS_ADDRESS, REG_ENABLE, 0x00);
        sdi_log("DONE stage=config stop=%u\r\n", status);
        return;
    }
    sdi_log("CONFIG verified=1\r\n");

    unsigned errors = 0;
    for (unsigned i = 1; i <= SAMPLES; ++i)
    {
        Delay_Ms(200);
        if (read_sample(i) != I2C_OK)
        {
            ++errors;
        }
    }
    const bool fifo_ok = errors == 0 && check_gesture_fifo();
    status = i2c_write_register(APDS_ADDRESS, REG_ENABLE, 0x00);
    sdi_log("DONE stage=complete errors=%u fifo=%u stop=%u\r\n",
            errors, (unsigned)fifo_ok, status);
}

int main(void)
{
    SystemCoreClockUpdate();
    SDI_Printf_Enable();
    for (;;)
    {
        run_diagnostic();
        Delay_Ms(1500);
    }
}
