#include "apds9960_log.h"
#include <ch32v00x.h>
#include <stdarg.h>
#include <stdint.h>
static void put_char(char c) {
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) {}
    USART_SendData(USART1, (uint8_t)c);
}
void apds_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    while (*format) {
        if (*format++ != '%') { put_char(format[-1]); continue; }
        if (*format == '%') { put_char(*format++); continue; }
        uint8_t width = 0;
        if (*format == '0' && format[1] == '2') { width=2; format+=2; }
        char spec = *format;
        if (!spec) break;
        ++format;
        uint32_t value;
        if (spec == 'd') {
            int v=va_arg(args, int);
            value=(uint32_t)v;
            if (v<0) { put_char('-'); value=0u-value; }
        } else { value=va_arg(args, unsigned int); }
        uint8_t base=spec=='X'?16:10;
        char digits[10]; uint8_t count=0;
        do {
            uint8_t digit=value%base;
            digits[count++]=(char)(digit<10?'0'+digit:'A'+digit-10);
            value/=base;
        } while (value);
        while (count<width) digits[count++]='0';
        while (count) put_char(digits[--count]);
    }
    va_end(args);
}
