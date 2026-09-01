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

#if APDS_INT_MODE == 1
#include "int_config.h"
#endif

/* Обработчики прерываний (обязательны для CH32V003) */
void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/* Вывод имени распознанного жеста в UART (общий код для polling/interrupt режимов) */
static void print_gesture(gesture_t g) {
    switch (g) {
        case GESTURE_LEFT:  printf("LEFT\r\n");  break;
        case GESTURE_RIGHT: printf("RIGHT\r\n"); break;
        case GESTURE_UP:    printf("UP\r\n");    break;
        case GESTURE_DOWN:  printf("DOWN\r\n");  break;
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
    printf("SENSOR HANG (i2c=%d), re-init...\r\n", apds_getLastI2CStatus());
    if (apds_init()) {
        printf("Sensor recovered.\r\n");
        drain_fifo();
    } else {
        printf("Re-init failed (err=%d)\r\n", apds_getLastError());
    }
}

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    printf("APDS9960 gesture driver init...\r\n");

    uint8_t i2c_st = i2c_init(400000);
    if (i2c_st != I2C_OK) {
        printf("ERROR: i2c_init failed (%d)\r\n", i2c_st);
        while (1) {}
    }

    if (!apds_init()) {
        printf("ERROR: APDS9960 not responding! err=%d i2c=%d\r\n",
               apds_getLastError(), apds_getLastI2CStatus());

        /* Автосканирование шины для диагностики: сенсор на 0x39? */
        printf("Scanning I2C bus...\r\n");
        uint8_t found = 0;
        for (uint8_t a = 1; a < 0x7F; a++) {
            if (i2c_probe_address(a, NULL, NULL) == I2C_OK) {
                printf("  device found at 0x%02X\r\n", a);
                found++;
            }
        }
        if (found == 0) {
            printf("  no devices on the bus — check wiring/power/pull-ups\r\n");
        }

        while (1) {}
    }

#if APDS_INT_MODE == 1
    printf("Mode: INTERRUPT (INT=PC3)\r\n");
    apds_exti_init();
    apds_enableInterrupt();
#else
    printf("Mode: POLLING\r\n");
#endif

    printf("APDS9960 ready.\r\n");

    Delay_Ms(200);

    /* Сброс шумовых данных из FIFO */
    drain_fifo();

#if APDS_INT_MODE == 1
    /* ========================================================================
     * INTERRUPT MODE
     *
     * Main loop засыпает (__WFI) пока ISR не установит g_apds_int_flag.
     * Прерывание срабатывает при GVALID=1 (falling edge на INT pin).
     * После пробуждения: читаем жест через существующий API.
     *
     * Искусственный кулдаун после жеста не требуется: apds_readGesture()
     * теперь ждёт настоящего завершения жеста (GVALID=0) по дедлайну
     * SysTick вместо фиксированного числа итераций, поэтому "хвост"
     * одного физического взмаха руки больше не декодируется как отдельный
     * второй жест. Оставляем лишь безусловный дренаж FIFO — обычно no-op.
     * ======================================================================== */
    while (1) {
        while (g_apds_int_flag == 0) {
            __WFI();
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
            drain_fifo();
        } else if (apds_getLastError() == APDS_ERR_SENSOR_HANG) {
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

            if (g != GESTURE_NONE) {
                drain_fifo();
            }
        } else if (apds_getLastError() == APDS_ERR_SENSOR_HANG) {
            handle_sensor_failure();
        }
    }
#endif
}

void NMI_Handler(void) {}
void HardFault_Handler(void) { while (1) {} }
