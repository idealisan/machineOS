/* 任务状态段：只为了给 ring3 -> ring0 的异常提供内核栈
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * syscall 走的是自己换栈，不需要 TSS；但异常不会帮我们换栈，
 * 必须由 CPU 依据 TSS.RSP0 切到内核栈。
 *
 * 注意 64 位 TSS 的布局：开头是 4 字节保留字段，RSP0 位于偏移 4。
 * 不能用 unsigned long 数组直接映射，否则 RSP0 会落在偏移 8 上，
 * CPU 读到的就是 0，切栈时直接 #SS。
 */

#include "tss.h"
#include "klog.h"

#define TSS_SELECTOR 0x38
#define TSS_SIZE     104

struct tss64 {
    unsigned int reserved0;
    unsigned long rsp0;
    unsigned long rsp1;
    unsigned long rsp2;
    unsigned long reserved1;
    unsigned long ist[7];
    unsigned long reserved2;
    unsigned short reserved3;
    unsigned short iomap_base;
} __attribute__((packed));

static struct tss64 tss;

/* 异常处理专用栈，和 syscall 栈分开，避免异常发生在系统调用中途时互相踩踏 */
static unsigned char exception_stack[16384] __attribute__((aligned(16)));

extern unsigned char gdt[];      /* stage 2 里的 GDT，见 boot/stage2.asm */

static void write_tss_descriptor(void)
{
    unsigned long base = (unsigned long)&tss;
    unsigned long limit = sizeof(tss) - 1;
    unsigned char *entry = gdt + TSS_SELECTOR;   /* 选择子 0x38 = 表内第 7 项 */

    entry[0] = (unsigned char)(limit & 0xff);
    entry[1] = (unsigned char)((limit >> 8) & 0xff);
    entry[2] = (unsigned char)(base & 0xff);
    entry[3] = (unsigned char)((base >> 8) & 0xff);
    entry[4] = (unsigned char)((base >> 16) & 0xff);
    entry[5] = 0x89;                             /* 可用 64 位 TSS */
    entry[6] = (unsigned char)((limit >> 16) & 0x0f);
    entry[7] = (unsigned char)((base >> 24) & 0xff);
    entry[8] = (unsigned char)((base >> 32) & 0xff);
    entry[9] = (unsigned char)((base >> 40) & 0xff);
    entry[10] = (unsigned char)((base >> 48) & 0xff);
    entry[11] = (unsigned char)((base >> 56) & 0xff);
    entry[12] = 0;
    entry[13] = 0;
    entry[14] = 0;
    entry[15] = 0;
}

void tss_init(void)
{
    unsigned char *raw = (unsigned char *)&tss;
    for (unsigned int i = 0; i < sizeof(tss); ++i)
        raw[i] = 0;

    tss.rsp0 = (unsigned long)(exception_stack + sizeof(exception_stack));
    tss.iomap_base = TSS_SIZE;                   /* 没有 IO 位图 */

    write_tss_descriptor();

    unsigned short selector = TSS_SELECTOR;
    __asm__ volatile("ltr %0" : : "r"(selector));

    kputs("tss: rsp0=");
    kputhex(tss.rsp0);
    kputs("\n");
}
