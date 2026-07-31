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

/*
 * Владелец общего вектора EXTI0..7.
 *
 * 1 (по умолчанию): библиотека определяет strong EXTI7_0_IRQHandler.
 * Это обязательно для NoneOS-SDK: его weak fallback-handler зациклен, и
 * weak обработчик библиотеки не гарантированно вытесняет этот fallback.
 *
 * 0: приложение само определяет EXTI7_0_IRQHandler и вызывает
 * apds_handle_exti() / apds_clear_exti() для линии APDS9960.
 */
#ifndef APDS_PROVIDE_EXTI_ISR
#define APDS_PROVIDE_EXTI_ISR   1
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

/* ============================================================================
 * Обработка прерывания APDS9960 — проверяет EXTI_Line3, ставит флаг.
 * НЕ сбрасывает pending bit — вызывайте apds_clear_exti() после.
 * ============================================================================ */
void apds_handle_exti(void);

/* ============================================================================
 * Сброс pending bit EXTI_Line3
 * ============================================================================ */
void apds_clear_exti(void);

/* ============================================================================
 * USER CALLBACK — вызывается из EXTI7_0_IRQHandler библиотеки
 *
 * Переопределите эту функцию в своём коде, чтобы обработать другие EXTI
 * линии (0-7 делят один вектор на CH32V003). Для полной замены обработчика
 * установите APDS_PROVIDE_EXTI_ISR=0 для всей сборки.
 * ============================================================================ */
void apds_exti_callback(void);

#endif /* INT_CONFIG_H */
