#include <stdint.h>
#include <stdio.h>

#include "sdi_log.h"

#define SDI_DATA0 (*(volatile uint32_t *)0xE00000F4u)
#define SDI_DATA1 (*(volatile uint32_t *)0xE00000F8u)

void sdi_vlog(const char *format, va_list args)
{
    char line[112];
    int length = vsnprintf(line, sizeof(line), format, args);
    if (length < 0 || (unsigned)length >= sizeof(line))
    {
        return;
    }

    /* Publish DATA0 last. Bound the wait without altering the driver's SysTick. */
    for (int offset = 0; offset < length;)
    {
        unsigned wait = 150000;
        while (SDI_DATA0 != 0 && wait > 0)
        {
            --wait;
        }
        if (SDI_DATA0 != 0)
        {
            return;
        }

        unsigned count = (unsigned)(length - offset);
        if (count > 7)
        {
            count = 7;
        }
        uint32_t words[2] = {count, 0};
        for (unsigned i = 1; i <= count; ++i)
        {
            words[i >> 2] |= (uint32_t)(uint8_t)line[offset++] <<
                             (8 * (i & 3));
        }
        SDI_DATA1 = words[1];
        SDI_DATA0 = words[0];
    }
}

void sdi_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    sdi_vlog(format, args);
    va_end(args);
}
