/* 用户地址空间分页管理
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef MACHINE_OS_VMM_H
#define MACHINE_OS_VMM_H

#define VMM_WRITE 0x2ul
#define VMM_USER  0x4ul
#define VMM_NX    (1ul << 63)

void vmm_init(void);
void vmm_protect_user_code(void);
int vmm_map_page(unsigned long address, unsigned long physical, unsigned long flags);
int vmm_unmap_page(unsigned long address);
int vmm_query(unsigned long address, unsigned long *physical);
int vmm_user_ok(unsigned long address, unsigned long length);

#endif
