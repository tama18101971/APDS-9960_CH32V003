#ifndef SDI_LOG_H
#define SDI_LOG_H

#include <stdarg.h>

void sdi_log(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
void sdi_vlog(const char *format, va_list args);

#endif
