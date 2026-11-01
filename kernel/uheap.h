/* 用户堆接口
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef MACHINE_OS_UHEAP_H
#define MACHINE_OS_UHEAP_H

void uheap_init(void);
long uheap_alloc(unsigned long size);       /* 返回用户虚拟地址，失败返回 -1 */
long uheap_free(unsigned long base);        /* 0 = 成功，-1 = 非法或未登记 */

#endif
