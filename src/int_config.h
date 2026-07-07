#ifndef INT_CONFIG_H
#define INT_CONFIG_H

/*
 * int_config.h — Настройка внешнего прерывания APDS9960 (INT → PC3)
 *
 * Использует EXTI_Line3 (PC3) с falling-edge триггером.
 * При GVALID=1 (данные жеста готовы) INT pin датчика становится низким,
 * EXTI генерирует прерывание, ISR устанавливает флаг g_apds_int_flag.
 *
 * Все EXTI 0-7 на CH32V003 делят один вектор: EXTI7_0_IRQHandler.
 */

#include <ch32v00x.h>
#include <stdint.h>

/* ============================================================================
 * НАСТРОЙКА ПИНА INT (APDS9960 → CH32V003)
 * ============================================================================ */

#ifndef APDS_INT_PORT
#define APDS_INT_PORT           GPIOC
#endif
#ifndef APDS_INT_PIN
#define APDS_INT_PIN            GPIO_Pin_3
#endif
#ifndef APDS_INT_LINE
#define APDS_INT_LINE           EXTI_Line3
#endif
#ifndef APDS_INT_PORT_SOURCE
#define APDS_INT_PORT_SOURCE    GPIO_PortSourceGPIOC
#endif
#ifndef APDS_INT_PIN_SOURCE
#define APDS_INT_PIN_SOURCE     GPIO_PinSource3
#endif

/* ============================================================================
 * ФЛАГ ПРЕРЫВАНИЯ (доступен из ISR и main loop)
 * ============================================================================ */

/* Флаг устанавливается ISR при falling edge (GVALID=1).
 * Main loop сбрасывает после обработки. */
extern volatile uint8_t g_apds_int_flag;

/* ============================================================================
 * API
 * ============================================================================ */

/* Инициализация EXTI3 + NVIC.
 * PC3: Input Pull-Up (INT open-drain active-low).
 * Falling-edge trigger (GVALID=1 → INT low). */
void apds_exti_init(void);

/* Включение прерывания в NVIC */
void apds_exti_enable(void);

/* Отключение прерывания в NVIC */
void apds_exti_disable(void);

#endif /* INT_CONFIG_H */
