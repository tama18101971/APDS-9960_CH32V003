#ifndef APDS9960_LOG_H
#define APDS9960_LOG_H
/* Bounded integer-only UART logger; requires USART_Printf_Init first.
 * Supports %d, %u, %X, %02X and %%. No heap or stdio state. */
void apds_log(const char *format, ...);
#endif
