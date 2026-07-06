/*
 * int_config.c — Реализация EXTI для APDS9960 (INT → PC3)
 */

#include "int_config.h"
#include <ch32v00x.h>

/* Флаг прерывания: ISR ставит =1, main loop сбрасывает =0 */
volatile uint8_t g_apds_int_flag = 0;

void apds_exti_init(void) {
    /* Включаем AFIO clock (обязательно для EXTI маппинга) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    /* PC3 — вход с подтяжкой к VCC (INT open-drain active-low) */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin   = APDS_INT_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(APDS_INT_PORT, &gpio);

    /* Маппинг PC3 → EXTI_Line3 */
    GPIO_EXTILineConfig(APDS_INT_PORT_SOURCE, APDS_INT_PIN_SOURCE);

    /* EXTI: falling-edge trigger (INT low при GVALID=1) */
    EXTI_InitTypeDef exti = {0};
    exti.EXTI_Line    = APDS_INT_LINE;
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Falling;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    /* NVIC: EXTI7_0_IRQn (общий для EXTI 0-7) */
    NVIC_InitTypeDef nvic = {0};
    nvic.NVIC_IRQChannel                   = EXTI7_0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority       = 0;
    nvic.NVIC_IRQChannelCmd               = ENABLE;
    NVIC_Init(&nvic);
}

void apds_exti_enable(void) {
    NVIC_EnableIRQ(EXTI7_0_IRQn);
}

void apds_exti_disable(void) {
    NVIC_DisableIRQ(EXTI7_0_IRQn);
}

/* ============================================================================
 * ISR: EXTI7_0_IRQHandler
 *
 * Все EXTI 0-7 делят один вектор на CH32V003.
 * Проверяем EXTI_Line3 и сбрасываем pending bit.
 * ============================================================================ */
void EXTI7_0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void EXTI7_0_IRQHandler(void) {
    if (EXTI_GetITStatus(APDS_INT_LINE) != RESET) {
        g_apds_int_flag = 1;
        EXTI_ClearITPendingBit(APDS_INT_LINE);
    }
}
