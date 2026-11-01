/* 系统调用与键盘轮询
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 当前只有三个调用，够跑通"用户态 C 程序"这条链路：
 *   0 exit(status)        结束用户程序，回到深紫色桌面
 *   1 write(fd, buf, len) 把内容写到串口（fd 目前只有 1）
 *   2 read_key()          轮询 PS/2 键盘，返回扫描码，无按键返回 -1
 */

#include "io.h"
#include "uheap.h"
#include "vmm.h"

extern void sys_exit_to_kernel(void);

#define MSR_EFER  0xc0000080u
#define MSR_STAR  0xc0000081u
#define MSR_LSTAR 0xc0000082u
#define MSR_FMASK 0xc0000084u

/* 用户程序被加载到 2 MiB..4 MiB 这一段，只有它对外可见 */
#define USER_BASE 0x200000ul
#define USER_TOP  0x400000ul

static unsigned long msr_read(unsigned int msr)
{
    unsigned int lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((unsigned long)hi << 32) | lo;
}

static void msr_write(unsigned int msr, unsigned long value)
{
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"((unsigned int)value), "d"((unsigned int)(value >> 32)));
}

int kbd_poll(void)
{
    if (inb(0x64) & 0x01)
        return inb(0x60);
    return -1;
}

void kbd_flush(void)
{
    while (inb(0x64) & 0x01)
        (void)inb(0x60);
}

/* 逐个页查页表：必须存在且允许用户访问，否则内核不能碰这块内存 */
static int user_range_ok(unsigned long ptr, unsigned long len)
{
    return vmm_user_ok(ptr, len);
}

extern char syscall_entry[];

void syscall_init(void)
{
    /* EFER.SCE：允许 syscall/sysret */
    msr_write(MSR_EFER, msr_read(MSR_EFER) | 0x1);

    /* STAR：进入内核用 0x18:0x20，返回用户用 0x30(CS)/0x28(SS) */
    msr_write(MSR_STAR, ((unsigned long)0x20 << 48) | ((unsigned long)0x18 << 32));

    msr_write(MSR_LSTAR, (unsigned long)syscall_entry);

    /* 进入内核时清掉 IF/TF/DF/NT/AC/IOPL */
    msr_write(MSR_FMASK, 0x47700);
}

long syscall_dispatch(long number, long a1, long a2, long a3)
{
    (void)a3;

    switch (number) {
    case 0:
        sys_exit_to_kernel();
        return 0;

    case 1: {
        const char *buf = (const char *)a1;
        if (!user_range_ok((unsigned long)a1, (unsigned long)a2))
            return -1;
        for (long i = 0; i < a2; ++i)
            serial_putc(buf[i]);
        return a2;
    }

    case 2:
        return kbd_poll();

    case 3:
        return uheap_alloc((unsigned long)a1);

    case 4:
        return uheap_free((unsigned long)a1);

    default:
        return -1;
    }
}
