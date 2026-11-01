/* 用户堆：用 pmm 的物理帧给用户态提供可写内存
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 用户窗口固定在 0x300000..0x3F0000（落在 stage 2 标记为 User 的那个 2 MiB 页里）。
 * 第一次分配时把该 2 MiB 大页拆成 4 KiB 页表：原有内容保持恒等映射，
 * 之后每页由 pmm 分配一帧再挂上去，所以用户拿到的是真正独立的物理页。
 *
 * 仍然极简：不做换页、不做回收、不合并空闲块；释放只是把页表项清掉、
 * 把帧还给 pmm。分配表只有 32 项，超了就拒绝。
 */

#include "uheap.h"
#include "pmm.h"
#include "klog.h"

#define PAGE_SIZE     4096ul
#define HEAP_BASE     0x300000ul
#define HEAP_LIMIT    0x3f0000ul
#define USER_PAGE_PDE 1                  /* 2 MiB..4 MiB 对应的 PDE 索引 */
#define MAX_ALLOCS    32
#define PTE_FLAGS     0x7ul              /* 存在 + 可写 + 用户可访问 */

struct allocation {
    unsigned long base;
    unsigned long pages;
};

static unsigned long *user_pt;
static unsigned long heap_next = HEAP_BASE;
static struct allocation allocs[MAX_ALLOCS];

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

void uheap_init(void)
{
    unsigned long *pml4 = (unsigned long *)read_cr3();
    unsigned long *pdpt = next_table(pml4[0]);
    unsigned long *pd = next_table(pdpt[0]);
    unsigned long *pde = &pd[USER_PAGE_PDE];

    for (int i = 0; i < MAX_ALLOCS; ++i)
        allocs[i].base = 0;

    if (*pde & 0x80) {                   /* PS=1：当前还是 2 MiB 大页 */
        unsigned long frame = pmm_alloc();
        if (frame == 0) {
            kputs("uheap: no frame for page table\n");
            return;
        }

        unsigned long *pt = (unsigned long *)frame;
        for (unsigned long i = 0; i < 512; ++i)
            pt[i] = (0x200000ul + i * PAGE_SIZE) | PTE_FLAGS;

        *pde = frame | PTE_FLAGS;        /* 指向页表，同时清掉 PS */
        flush_tlb();
    }

    user_pt = next_table(pde[0]);
    kputs("uheap: 4 KiB page table at ");
    kputhex((unsigned long)user_pt);
    kputs(", window 0x300000-0x3f0000\n");
}

long uheap_alloc(unsigned long size)
{
    if (user_pt == 0 || size == 0)
        return -1;

    unsigned long pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (heap_next + pages * PAGE_SIZE > HEAP_LIMIT)
        return -1;

    int slot = -1;
    for (int i = 0; i < MAX_ALLOCS; ++i) {
        if (allocs[i].base == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;

    unsigned long base = heap_next;

    for (unsigned long i = 0; i < pages; ++i) {
        unsigned long frame = pmm_alloc();
        if (frame == 0) {                /* 物理内存不足：回滚，绝不超额分配 */
            for (unsigned long j = 0; j < i; ++j) {
                unsigned long *pte = &user_pt[(base >> 12) + j];
                pmm_free(*pte & 0x000ffffffffff000ul);
                *pte = 0;
            }
            flush_tlb();
            return -1;
        }
        user_pt[(base >> 12) + i] = frame | PTE_FLAGS;
    }
    flush_tlb();

    allocs[slot].base = base;
    allocs[slot].pages = pages;
    heap_next = base + pages * PAGE_SIZE;

    kputs("uheap: alloc ");
    kputhex(base);
    kputs(" pages=");
    kputdec(pages);
    kputs(" pmm_free=");
    kputdec(pmm_free_frames());
    kputs("\n");

    return (long)base;
}

long uheap_free(unsigned long base)
{
    if (user_pt == 0 || (base % PAGE_SIZE) != 0)
        return -1;

    for (int i = 0; i < MAX_ALLOCS; ++i) {
        if (allocs[i].base != base)
            continue;

        unsigned long pages = allocs[i].pages;
        for (unsigned long j = 0; j < pages; ++j) {
            unsigned long *pte = &user_pt[(base >> 12) + j];
            unsigned long frame = *pte & 0x000ffffffffff000ul;
            *pte = 0;
            pmm_free(frame);
        }
        flush_tlb();

        allocs[i].base = 0;
        allocs[i].pages = 0;

        kputs("uheap: free ");
        kputhex(base);
        kputs(" pmm_free=");
        kputdec(pmm_free_frames());
        kputs("\n");
        return 0;
    }
    return -1;
}
