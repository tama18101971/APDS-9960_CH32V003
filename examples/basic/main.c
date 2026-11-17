/*
 * main.c — Пример использования драйвера APDS9960
 *
 * Поддерживает два режима работы:
 *   APDS_INT_MODE=0 — Polling (опрос через apds_available())
 *   APDS_INT_MODE=1 — Interrupt (INT → PC3, EXTI3 falling-edge)
 *
 * Подключение:
 *   PC1 = SDA, PC2 = SCL (I2C)
 *   PC3 = INT (APDS9960, input pull-up)
 *   PD5 = TX (UART 115200)
 */

#include <ch32v00x.h>
#include <debug.h>
#include "i2c.h"
#include "apds9960.h"
#include "apds9960_log.h"
#ifndef APDS_EXAMPLE_I2C_HZ
#define APDS_EXAMPLE_I2C_HZ 400000u
#endif

#if APDS_INT_MODE == 1
#include "int_config.h"
#endif

#if APDS_INT_MODE == 1 && APDS_PROVIDE_EXTI_ISR == 0
/* Example ownership of the shared vector; applications handle other lines here. */
void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI7_0_IRQHandler(void) {
    apds_handle_exti();
    apds_clear_exti();
}
#endif

/* Обработчики прерываний (обязательны для CH32V003) */
void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/* Вывод имени распознанного жеста в UART (общий код для polling/interrupt режимов) */
static void print_gesture(gesture_t g) {
    switch (g) {
        case GESTURE_LEFT:  apds_log("LEFT\r\n");  break;
        case GESTURE_RIGHT: apds_log("RIGHT\r\n"); break;
        case GESTURE_UP:    apds_log("UP\r\n");    break;
        case GESTURE_DOWN:  apds_log("DOWN\r\n");  break;
        default: break;
    }
}

/* Дренаж FIFO: глубина аппаратного FIFO датчика — 32 пакета, поэтому число
 * проходов ограничено. Без ограничения залипший GVALID даёт вечный цикл. */
static void drain_fifo(void) {
    uint8_t guard = 8;
    while (guard-- && apds_available()) {
        apds_readGesture();
    }
}

/* Диагностика отказа датчика и попытка восстановления. */
static void handle_sensor_failure(void) {
    apds_log("SENSOR HANG (i2c=%d), re-init...\r\n", apds_getLastI2CStatus());
    if (apds_getLastI2CStatus() == I2C_ERR_RECOVERY) (void)i2c_init(APDS_EXAMPLE_I2C_HZ);
    if (apds_init()) {
        apds_log("Sensor recovered.\r\n");
        drain_fifo();
    } else {
        apds_log("Re-init failed (err=%d)\r\n", apds_getLastError());
    }
}

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    apds_log("APDS9960 gesture driver init...\r\n");
#ifdef APDS_HEALTH_TEST
    extern void apds_health_check(void);
    apds_health_check();
#endif

    uint8_t i2c_st = i2c_init(APDS_EXAMPLE_I2C_HZ);
    if (i2c_st != I2C_OK) {
        apds_log("ERROR: i2c_init failed (%d)\r\n", i2c_st);
        while (1) {}
    }

    uint8_t chip_id = 0;
    i2c_read_register(0x39, 0x92, &chip_id);
    apds_log("CHIP ID=0x%02X\r\n", chip_id);
    if (!apds_init()) {
        apds_log("ERROR: APDS9960 not responding! err=%d i2c=%d\r\n",
               apds_getLastError(), apds_getLastI2CStatus());

        /* Автосканирование шины для диагностики: сенсор на 0x39? */
        apds_log("Scanning I2C bus...\r\n");
        uint8_t found = 0;
        for (uint8_t a = 1; a < 0x7F; a++) {
            if (i2c_probe_address(a, NULL, NULL) == I2C_OK) {
                apds_log("  device found at 0x%02X\r\n", a);
                found++;
            }
        }
        if (found == 0) {
            apds_log("  no devices on the bus — check wiring/power/pull-ups\r\n");
        }

        while (1) {}
    }

#if APDS_INT_MODE == 1
    apds_log("Mode: INTERRUPT (INT=PC3)\r\n");
    apds_exti_init();
    apds_enableInterrupt();
#else
    apds_log("Mode: POLLING\r\n");
#endif

#ifdef APDS_HW_TEST
    uint8_t gc2=0, gs=0, level=0, en=0;
    i2c_read_register(0x39, 0xA3, &gc2);
    i2c_read_register(0x39, 0x80, &en);
    apds_log("HW CONFIG id=%02X gc2=%02X en=%02X\r\n", chip_id, gc2, en);
    Delay_Ms(500);
    i2c_read_register(0x39, 0xAF, &gs);
    i2c_read_register(0x39, 0xAE, &level);
    apds_log("HW BEFORE gs=%02X level=%u\r\n", gs, level);
    (void)apds_available();
    uint8_t recovery_error=apds_getLastError();
    i2c_read_register(0x39, 0xAF, &gs);
    i2c_read_register(0x39, 0xAE, &level);
    apds_log("HW RECOVERY err=%u gs=%02X level=%u\r\n", recovery_error, gs, level);
    bool sleep_ok=apds_sleep(), wake_ok=apds_wakeup();
    apds_log("HW SLEEP=%u WAKE=%u\r\n", sleep_ok, wake_ok);
#endif
    apds_log("APDS9960 ready.\r\n");

    Delay_Ms(200);

    /* Сброс шумовых данных из FIFO */
    drain_fifo();

#if APDS_INT_MODE == 1
    /* Bounded flag/INT-level polling avoids the check-then-WFI race.
     * This example trades CPU sleep for guaranteed periodic recovery attempts.
     * Completed gestures are delivered once; the next gesture is not discarded. */
    while (1) {
        for (uint8_t wait = 0; wait < 20 && g_apds_int_flag == 0 &&
             GPIO_ReadInputDataBit(APDS_INT_PORT, APDS_INT_PIN) == Bit_SET; ++wait) {
            Delay_Ms(1);
        }
        g_apds_int_flag = 0;

        /* Чтение GSTATUS в apds_clearInterrupt() здесь избыточно: следующий
         * apds_available() читает GSTATUS сам, а GINT в сенсоре сбрасывается
         * опустошением FIFO внутри apds_readGesture(), а не чтением GSTATUS
         * (см. apds9960.h). Прежняя пара вызовов давала лишнюю I2C-
         * транзакцию и гонку двух последовательных чтений GSTATUS. */

        if (apds_available()) {
            gesture_t g = apds_readGesture();
            print_gesture(g);
            /* Completed gestures have already drained their own FIFO. */
        } else if (apds_getLastError() == APDS_ERR_SENSOR_HANG ||
                   apds_getLastError() == APDS_ERR_I2C) {
            handle_sensor_failure();
        }
    }

#else
    /* ========================================================================
     * POLLING MODE (оригинальный код)
     * ======================================================================== */
    while (1) {
        if (apds_available()) {
            gesture_t g = apds_readGesture();
            print_gesture(g);

        } else if (apds_getLastError() == APDS_ERR_SENSOR_HANG ||
                   apds_getLastError() == APDS_ERR_I2C) {
            handle_sensor_failure();
        }
    }
#endif
}

void NMI_Handler(void) {}
void HardFault_Handler(void) { while (1) {} }
