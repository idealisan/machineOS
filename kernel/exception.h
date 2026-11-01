/* 异常处理
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef MACHINE_OS_EXCEPTION_H
#define MACHINE_OS_EXCEPTION_H

struct trap_frame;

void exception_dispatch(struct trap_frame *frame);

/* 在 entry.asm 里：从用户态异常返回到内核的调用点 */
void sys_exit_to_kernel(void);

#endif
