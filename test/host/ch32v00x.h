#ifndef MOCK_CH32_H
#define MOCK_CH32_H
#include <stdint.h>
typedef struct { volatile uint32_t CTLR, SR, CNT, CMP; } SysTick_Type;
extern SysTick_Type mock_tick;
extern uint32_t SystemCoreClock;
SysTick_Type *mock_systick_access(void);
#define SysTick (mock_systick_access())
#endif
