/* 异常分发：打印现场，用户态出错就结束该程序，内核出错就停机
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "klog.h"
#include "exception.h"

struct trap_frame {
    unsigned long vector;
    unsigned long error;
    unsigned long rip;
    unsigned long cs;
    unsigned long rflags;
    unsigned long rsp;
    unsigned long ss;
};

static const char *const names[32] = {
    "divide error", "debug", "NMI", "breakpoint", "overflow", "bound range",
    "invalid opcode", "device not available", "double fault", "coprocessor segment",
    "invalid TSS", "segment not present", "stack fault", "general protection",
    "page fault", "reserved", "x87 floating point", "alignment check",
    "machine check", "SIMD floating point", "virtualization", "control protection",
    "reserved", "reserved", "reserved", "reserved", "reserved", "reserved",
    "hypervisor injection", "VMM communication", "security", "reserved",
};

static unsigned long read_cr2(void)
{
    unsigned long value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

void exception_dispatch(struct trap_frame *frame)
{
    kputs("[fault] ");
    kputs(frame->vector < 32 ? names[frame->vector] : "unknown");
    kputs(" vector=");
    kputdec(frame->vector);
    kputs(" error=");
    kputhex(frame->error);
    kputs(" rip=");
    kputhex(frame->rip);
    kputs(" cs=");
    kputhex(frame->cs);

    if (frame->vector == 14) {
        kputs(" cr2=");
        kputhex(read_cr2());
    }
    kputs("\n");

    if ((frame->cs & 3) == 3) {
        kputs("[fault] from user mode, terminating program\n");
        sys_exit_to_kernel();
    }

    kputs("[fault] from kernel, halted\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}
