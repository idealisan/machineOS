/* 用户地址空间的分页管理：建立映射、设置权限、校验用户指针
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 用户窗口是 0x200000..0x400000（stage 2 把这一整个 2 MiB 大页标为 User）。
 * 启动时把它拆成 4 KiB 页表，并按区域给不同权限：
 *
 *   0x200000..图像末尾   代码：可读可执行，不可写（复制完成后再收紧）
 *   0x3FF000             栈与 boot_info：可读写，不可执行（NX）
 *   0x300000..0x3F0000  堆：由 uheap 按需映射，可读写，不可执行（NX）
 *   其余                 不映射，访问即 #PF
 *
 * 帧缓冲单独在 0xFD000000 的 2 MiB 页里，可读写、不可执行。
 */

#include "vmm.h"
#include "pmm.h"
#include "klog.h"

#define PAGE_SIZE        4096ul
#define USER_BASE        0x200000ul
#define USER_LIMIT       0x400000ul
#define USER_PDE_INDEX   1                    /* 2 MiB..4 MiB */
#define STACK_PAGE       0x3ff000ul
#define STACK_GUARD_PAGE 0x3fe000ul   /* 栈从 0x3FF000 向下长，第一页其实是 0x3FE000 */

#define PTE_PRESENT      0x1ul
#define PTE_WRITE        0x2ul
#define PTE_USER         0x4ul
#define PTE_NX           (1ul << 63)

extern unsigned char user_image_start[];
extern unsigned char user_image_end[];

static unsigned long *user_pt;
static unsigned long image_pages;

static inline unsigned long read_cr3(void)
{
    unsigned long value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void flush_tlb(void)
{
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
}

static inline unsigned long *next_table(unsigned long entry)
{
    return (unsigned long *)(entry & 0x000ffffffffff000ul);
}

static inline unsigned long *pte_of(unsigned long address)
{
    return &user_pt[(address - USER_BASE) / PAGE_SIZE];
}

void vmm_init(void)
{
    image_pages = ((unsigned long)(user_image_end - user_image_start) + PAGE_SIZE - 1) / PAGE_SIZE;

    unsigned long *pml4 = (unsigned long *)read_cr3();
    unsigned long *pdpt = next_table(pml4[0]);
    unsigned long *pd = next_table(pdpt[0]);
    unsigned long *pde = &pd[USER_PDE_INDEX];

    unsigned long frame = pmm_alloc();
    if (frame == 0) {
        kputs("vmm: no frame for user page table\n");
        return;
    }

    user_pt = (unsigned long *)frame;
    for (unsigned long i = 0; i < 512; ++i)
        user_pt[i] = 0;                       /* 默认全不映射 */

    unsigned long code_base = USER_BASE;
    for (unsigned long i = 0; i < image_pages; ++i)
        user_pt[(code_base - USER_BASE) / PAGE_SIZE + i] =
            (code_base + i * PAGE_SIZE) | PTE_PRESENT | PTE_WRITE | PTE_USER;

    user_pt[(STACK_GUARD_PAGE - USER_BASE) / PAGE_SIZE] =
        STACK_GUARD_PAGE | PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_NX;
    user_pt[(STACK_PAGE - USER_BASE) / PAGE_SIZE] =
        STACK_PAGE | PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_NX;

    *pde = frame | PTE_PRESENT | PTE_WRITE | PTE_USER;
    flush_tlb();

    kputs("vmm: user page table at ");
    kputhex((unsigned long)user_pt);
    kputs(", code pages=");
    kputdec(image_pages);
    kputs("\n");
}

void vmm_protect_user_code(void)
{
    if (user_pt == 0)
        return;

    for (unsigned long i = 0; i < image_pages; ++i)
        user_pt[i] &= ~PTE_WRITE;

    flush_tlb();
    kputs("vmm: user code is now read-only\n");
}

int vmm_map_page(unsigned long address, unsigned long physical, unsigned long flags)
{
    if (user_pt == 0 || address < USER_BASE || address >= USER_LIMIT)
        return -1;
    if ((address % PAGE_SIZE) || (physical % PAGE_SIZE))
        return -1;

    *pte_of(address) = physical | PTE_PRESENT | flags;
    flush_tlb();
    return 0;
}

int vmm_unmap_page(unsigned long address)
{
    if (user_pt == 0 || address < USER_BASE || address >= USER_LIMIT)
        return -1;
    if (address % PAGE_SIZE)
        return -1;

    *pte_of(address) = 0;
    flush_tlb();
    return 0;
}

int vmm_query(unsigned long address, unsigned long *physical)
{
    if (user_pt == 0 || address < USER_BASE || address >= USER_LIMIT)
        return -1;

    unsigned long entry = *pte_of(address);
    if ((entry & PTE_PRESENT) == 0)
        return -1;

    if (physical)
        *physical = entry & 0x000ffffffffff000ul;
    return 0;
}

int vmm_user_ok(unsigned long address, unsigned long length)
{
    if (user_pt == 0 || length == 0)
        return 0;
    if (address < USER_BASE || address + length < address || address + length > USER_LIMIT)
        return 0;

    unsigned long first = (address - USER_BASE) / PAGE_SIZE;
    unsigned long last = (address + length - 1 - USER_BASE) / PAGE_SIZE;

    for (unsigned long i = first; i <= last; ++i) {
        unsigned long entry = user_pt[i];
        if ((entry & PTE_PRESENT) == 0 || (entry & PTE_USER) == 0)
            return 0;
    }
    return 1;
}
