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

int main(void) {
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    printf("APDS9960 gesture driver init...\r\n");

    i2c_init(100000);

    if (!apds_init()) {
        printf("ERROR: APDS9960 not responding!\r\n");
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
    while (apds_available()) {
        apds_readGesture();
    }

#if APDS_INT_MODE == 1
    /* ========================================================================
     * INTERRUPT MODE
     *
     * Main loop засыпает (__WFI) пока ISR не установит g_apds_int_flag.
     * Прерывание срабатывает при GVALID=1 (falling edge на INT pin).
     * После пробуждения: читаем жест через существующий API.
     * ======================================================================== */
    while (1) {
        while (g_apds_int_flag == 0) {
            __WFI();
        }
        g_apds_int_flag = 0;

        apds_clearInterrupt();

        if (apds_available()) {
            gesture_t g = apds_readGesture();

            switch (g) {
                case GESTURE_LEFT:  printf("LEFT\r\n");  break;
                case GESTURE_RIGHT: printf("RIGHT\r\n"); break;
                case GESTURE_UP:    printf("UP\r\n");    break;
                case GESTURE_DOWN:  printf("DOWN\r\n");  break;
                default: break;
            }
        }
    }

#else
    /* ========================================================================
     * POLLING MODE (оригинальный код)
     * ======================================================================== */
    uint32_t cooldown_end = 0;

    while (1) {
        if (cooldown_end != 0) {
            if ((int32_t)(SysTick->CNT - cooldown_end) < 0) {
                while (apds_available()) apds_readGesture();
                continue;
            }
            cooldown_end = 0;
        }

        if (apds_available()) {
            gesture_t g = apds_readGesture();

            switch (g) {
                case GESTURE_LEFT:  printf("LEFT\r\n");  break;
                case GESTURE_RIGHT: printf("RIGHT\r\n"); break;
                case GESTURE_UP:    printf("UP\r\n");    break;
                case GESTURE_DOWN:  printf("DOWN\r\n");  break;
                default: break;
            }

            if (g != GESTURE_NONE) {
                cooldown_end = SysTick->CNT - (48000 * 300);
                while (apds_available()) apds_readGesture();
            }
        }
    }
#endif
}

void NMI_Handler(void) {}
void HardFault_Handler(void) { while (1) {} }
