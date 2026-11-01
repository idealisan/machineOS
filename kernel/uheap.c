/* 用户堆：用 pmm 的物理帧给用户态提供可写内存
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 页表由 vmm 统一管理，这里只负责"取帧 → 映射 → 记账 → 释放"。
 * 仍然极简：不做换页、不做回收、不合并空闲块，分配表固定 32 项。
 */

#include "uheap.h"
#include "vmm.h"
#include "pmm.h"
#include "klog.h"

#define PAGE_SIZE  4096ul
#define HEAP_BASE  0x300000ul
#define HEAP_LIMIT 0x3f0000ul
#define MAX_ALLOCS 32

struct allocation {
    unsigned long base;
    unsigned long pages;
};

static unsigned long heap_next = HEAP_BASE;
static struct allocation allocs[MAX_ALLOCS];

static void zero_page(unsigned long address)
{
    unsigned char *p = (unsigned char *)address;
    for (unsigned long i = 0; i < PAGE_SIZE; ++i)
        p[i] = 0;
}

void uheap_init(void)
{
    for (int i = 0; i < MAX_ALLOCS; ++i) {
        allocs[i].base = 0;
        allocs[i].pages = 0;
    }
    kputs("uheap: window 0x300000-0x3f0000\n");
}

long uheap_alloc(unsigned long size)
{
    if (size == 0)
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
        if (frame == 0) {                     /* 物理内存不足：回滚，不超额分配 */
            for (unsigned long j = 0; j < i; ++j)
                vmm_unmap_page(base + j * PAGE_SIZE);
            kputs("uheap: out of memory\n");
            return -1;
        }
        zero_page(frame);                     /* 新页清零，避免泄漏内核残留数据 */
        vmm_map_page(base + i * PAGE_SIZE, frame, VMM_WRITE | VMM_USER | VMM_NX);
    }

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

/* 回收某个槽位占用的页：先取帧号，再解映射，最后还给 pmm */
static void release_slot(int slot)
{
    unsigned long base = allocs[slot].base;
    unsigned long pages = allocs[slot].pages;

    for (unsigned long j = 0; j < pages; ++j) {
        unsigned long va = base + j * PAGE_SIZE;
        unsigned long frame = 0;

        if (vmm_query(va, &frame) == 0)
            pmm_free(frame);
        vmm_unmap_page(va);
    }

    allocs[slot].base = 0;
    allocs[slot].pages = 0;
}

/* 用户程序被终止后调用：把它的所有分配收回，并让窗口从头开始 */
void uheap_reset(void)
{
    for (int i = 0; i < MAX_ALLOCS; ++i) {
        if (allocs[i].base != 0)
            release_slot(i);
    }
    heap_next = HEAP_BASE;

    kputs("uheap: reset, pmm_free=");
    kputdec(pmm_free_frames());
    kputs("\n");
}

long uheap_free(unsigned long base)
{
    if ((base % PAGE_SIZE) != 0)
        return -1;

    for (int i = 0; i < MAX_ALLOCS; ++i) {
        if (allocs[i].base != base)
            continue;

        release_slot(i);

        kputs("uheap: free ");
        kputhex(base);
        kputs(" pmm_free=");
        kputdec(pmm_free_frames());
        kputs("\n");
        return 0;
    }
    return -1;
}
