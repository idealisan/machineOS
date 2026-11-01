/* 物理内存管理接口
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef MACHINE_OS_PMM_H
#define MACHINE_OS_PMM_H

void pmm_init(void);
unsigned long pmm_alloc(void);          /* 返回 4 KiB 对齐的物理地址，失败返回 0 */
int pmm_free(unsigned long address);    /* 0 = 成功，-1 = 参数非法或重复释放 */
unsigned long pmm_total_frames(void);
unsigned long pmm_free_frames(void);
void pmm_selftest(void);

#endif
