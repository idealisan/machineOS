/* 最小化内核日志：直接写 COM1
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef MACHINE_OS_KLOG_H
#define MACHINE_OS_KLOG_H

#include "io.h"

static inline void kputs(const char *text)
{
    while (*text)
        serial_putc(*text++);
}

static inline void kputhex(unsigned long value)
{
    static const char digits[] = "0123456789abcdef";

    kputs("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 0xf]);
}

static inline void kputdec(unsigned long value)
{
    char buf[21];
    int i = 0;

    if (value == 0) {
        serial_putc('0');
        return;
    }
    while (value && i < 20) {
        buf[i++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (i > 0)
        serial_putc(buf[--i]);
}

#endif
