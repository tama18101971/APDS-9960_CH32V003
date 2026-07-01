/*
 * main.c — Пример использования драйвера APDS9960
 *
 * Инициализирует I2C (100 кГц) и датчик APDS9960.
 * В бесконечном цикле опрашивает жесты и выводит их по UART (115200 бод).
 *
 * Подключение UART: PD5 (TX) -> USB-UART адаптер
 */

#include <ch32v00x.h>
#include <debug.h>
#include "i2c.h"
#include "apds9960.h"

/* Обработчики прерываний (обязательны для CH32V003) */
void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

int main(void) {
    /* Обновление частоты системного тактирования (48 МГц по умолчанию) */
    SystemCoreClockUpdate();

    /* Инициализация таймера задержек (Delay_Init, Delay_Ms) */
    Delay_Init();

    /* Инициализация USART1 на 115200 бод (PD5=TX, PD6=RX)
     * Функция из debug.h: настраивает GPIO и переопределяет printf на USART1 */
    USART_Printf_Init(115200);
    printf("APDS9960 gesture driver init...\r\n");

    /* Инициализация I2C1 на 100 кГц (Standard Mode)
     * Пины: PC1 = SDA, PC2 = SCL (Open-Drain) */
    i2c_init(100000);

    /* Инициализация датчика APDS9960
     * Включает: проверку ID, настройку всех регистров,
     * включение proximity + gesture + wait режимов */
    if (!apds_init()) {
        printf("ERROR: APDS9960 not responding!\r\n");
        while (1) {} /* Останов — датчик не найден */
    }

    printf("APDS9960 ready.\r\n");

    /* Основной цикл опроса жестов */
    while (1) {
        /* Проверяем наличие данных жеста (GVALID в GSTATUS) */
        if (apds_available()) {
            /* Читаем жест (блокирующая функция, ~10-60 мс) */
            gesture_t g = apds_readGesture();

            /* Вывод результата по UART */
            switch (g) {
                case GESTURE_LEFT:  printf("LEFT\r\n");  break;
                case GESTURE_RIGHT: printf("RIGHT\r\n"); break;
                case GESTURE_UP:    printf("UP\r\n");    break;
                case GESTURE_DOWN:  printf("DOWN\r\n");  break;
                default: break; /* GESTURE_NONE — ничего не выводим */
            }
        }
    }
}

/* Обработчик NMI (Non-Maskable Interrupt) — пустой */
void NMI_Handler(void) {}

/* Обработчик HardFault — аварийная остановка */
void HardFault_Handler(void) {
    while (1) {}
}
