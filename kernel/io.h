/* 端口 I/O 与串口输出
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef MACHINE_OS_IO_H
#define MACHINE_OS_IO_H

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void serial_init(void)
{
    outb(0x3f9, 0x00);   /* 关闭串口中断 */
    outb(0x3fb, 0x80);   /* 允许设置波特率除数 */
    outb(0x3f8, 0x01);   /* 115200 / 1 = 115200 */
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x03);   /* 8N1 */
}

static inline void serial_putc(char c)
{
    while ((inb(0x3fd) & 0x20) == 0)
        ;
    outb(0x3f8, (unsigned char)c);
}

#endif
