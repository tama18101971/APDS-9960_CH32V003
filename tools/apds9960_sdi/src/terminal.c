#include <stdbool.h>
#include <stdint.h>

#include "apds9960.h"
#include "debug.h"
#include "i2c.h"
#include "int_config.h"
#include "sdi_log.h"

#define I2C_HZ 100000u
#define COMMAND_MAGIC 0xA5000000u
#define COMMAND_MASK 0xFF0000FCu

/* One aligned SRAM word written by WCH-Link over SDI. Bits 8–23 are the
 * command ID, bit 0 starts the test, bit 1 selects hardware interrupts. */
volatile uint32_t apds_terminal_command;

static volatile uint32_t s_edges;
static uint32_t s_handled_edges;
static uint16_t s_ack;
static bool s_running;
static bool s_interrupt;
static unsigned s_error;
static unsigned s_raw_error;

void apds_exti_callback(void)
{
    if (g_apds_int_flag)
    {
        g_apds_int_flag = 0;
        ++s_edges;
    }
}

static void report_state(void)
{
    sdi_log("\r\nSTATE v=1 addr=%08lX ack=%u run=%u int=%u irq=%lu err=%u raw=%u\r\n",
            (unsigned long)(uintptr_t)&apds_terminal_command, (unsigned)s_ack,
            (unsigned)s_running, (unsigned)s_interrupt, (unsigned long)s_edges,
            s_error, s_raw_error);
}

static void disable_exti(void)
{
    apds_exti_disable();
    apds_clear_exti();
    NVIC_ClearPendingIRQ(EXTI7_0_IRQn);
}

static bool stop_sensor(void)
{
    if (apds_shutdown())
    {
        return true;
    }
    s_error = apds_getLastError();
    s_raw_error = apds_getLastI2CStatus();
    return false;
}

static bool init_bus(void)
{
    unsigned status = i2c_init(I2C_HZ);
    if (status == I2C_OK)
    {
        return true;
    }
    s_error = APDS_ERR_I2C;
    s_raw_error = status;
    return false;
}

static bool start_sensor(bool interrupt)
{
    if (!init_bus())
    {
        return false;
    }

    if (apds_init())
    {
        if (!interrupt)
        {
            return true;
        }

        /* Make the driver change GIEN while the gesture engine is idle.
         * The first availability check after wake applies the cached policy. */
        if (apds_sleep())
        {
            apds_enableInterrupt();
            if (apds_getLastError() == APDS_ERR_NONE && apds_wakeup())
            {
                (void)apds_available();
                if (apds_getLastError() == APDS_ERR_NONE)
                {
                    return true;
                }
            }
        }
    }
    s_error = apds_getLastError();
    s_raw_error = apds_getLastI2CStatus();
    (void)apds_shutdown();
    return false;
}

static void handle_command(uint32_t command)
{
    uint16_t tag = (uint16_t)(command >> 8);
    if ((command & COMMAND_MASK) != COMMAND_MAGIC || tag == 0 || tag == s_ack)
    {
        return;
    }

    bool run = (command & 1u) != 0;
    bool interrupt = (command & 2u) != 0;
    s_error = APDS_ERR_NONE;
    s_raw_error = 0;
    if (run != s_running || (run && interrupt != s_interrupt))
    {
        disable_exti();
        bool stopped = !s_running || stop_sensor();
        s_running = stopped && run && start_sensor(interrupt);
        if (s_running && interrupt)
        {
            s_handled_edges = s_edges;
            apds_exti_enable();
        }
    }
    s_interrupt = interrupt;
    s_ack = tag;
    report_state();
}

static void poll_gesture(void)
{
    static const char *const directions[] = {"NONE", "LEFT", "RIGHT", "UP", "DOWN"};
    uint32_t edges = s_edges;
    bool from_irq = edges != s_handled_edges;
    if (s_interrupt && !from_irq &&
        GPIO_ReadInputDataBit(APDS_INT_PORT, APDS_INT_PIN) == Bit_SET)
    {
        return;
    }
    s_handled_edges = edges;

    bool available = apds_available();
    gesture_t gesture = available ? apds_readGesture() : GESTURE_NONE;
    unsigned error = apds_getLastError();
    if (gesture != GESTURE_NONE && error == APDS_ERR_NONE)
    {
        const char *source = s_interrupt ? (from_irq ? "EXTI" : "PIN") : "POLL";
        sdi_log("GESTURE %s source=%s\r\n", directions[gesture], source);
    }
    if (error != APDS_ERR_NONE &&
        (error != s_error || apds_getLastI2CStatus() != s_raw_error))
    {
        s_raw_error = apds_getLastI2CStatus();
        sdi_log("FAULT err=%u raw=%u\r\n", error, s_raw_error);
    }
    s_error = error;
    if (error == APDS_ERR_NONE)
    {
        s_raw_error = 0;
    }
    else if (error == APDS_ERR_SENSOR_HANG ||
             (error == APDS_ERR_I2C && s_raw_error == I2C_ERR_RECOVERY))
    {
        disable_exti();
        s_running = false;
        (void)apds_shutdown();
        report_state();
    }
}

int main(void)
{
    SystemCoreClockUpdate();
    SDI_Printf_Enable();
    apds_exti_init();
    disable_exti();
    if (init_bus())
    {
        (void)stop_sensor();
    }
    report_state();

    unsigned ticks = 0;
    for (;;)
    {
        uint32_t command = apds_terminal_command;
        if (command != 0)
        {
            apds_terminal_command = 0;
            handle_command(command);
        }
        if (s_running)
        {
            poll_gesture();
        }
        if (++ticks == 700)
        {
            report_state();
            ticks = 0;
        }
        Delay_Ms(s_error != APDS_ERR_NONE && s_running ? 10 : 1);
    }
}
