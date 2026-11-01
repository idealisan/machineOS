/* 中断描述符表：目前只装 CPU 异常，用于捕获用户态越权访问
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "idt.h"
#include "klog.h"

struct idt_entry {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char ist;
    unsigned char type_attr;
    unsigned short offset_mid;
    unsigned int offset_high;
    unsigned int reserved;
} __attribute__((packed));

struct idt_pointer {
    unsigned short limit;
    unsigned long base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_pointer idt_ptr;

extern void *isr_table[32];

void idt_init(void)
{
    for (int i = 0; i < 32; ++i) {
        unsigned long handler = (unsigned long)isr_table[i];

        idt[i].offset_low = (unsigned short)(handler & 0xffff);
        idt[i].selector = 0x18;                 /* 64 位内核代码段 */
        idt[i].ist = 0;
        idt[i].type_attr = 0x8e;                /* P=1, DPL=0, 中断门 */
        idt[i].offset_mid = (unsigned short)((handler >> 16) & 0xffff);
        idt[i].offset_high = (unsigned int)(handler >> 32);
        idt[i].reserved = 0;
    }

    idt_ptr.limit = (unsigned short)(sizeof(idt) - 1);
    idt_ptr.base = (unsigned long)idt;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
    kputs("idt: 32 exception gates installed\n");
}
