/* 极简物理内存管理：位图 + 4 KiB 帧
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 只做两件事：分配一帧、释放一帧。不支持超额分配、不支持交换、
 * 不做碎片整理，也不做页表映射——这层只管物理地址。
 *
 * 位图每位对应一个 4 KiB 帧：0 = 空闲，1 = 占用。
 */

#include "e820.h"
#include "klog.h"

#define FRAME_SIZE   4096ul
#define MAX_FRAMES   (1ul << 20)          /* 上限 4 GiB 物理内存 */
#define BITMAP_WORDS (MAX_FRAMES / 64)

extern char kernel_end[];                  /* 链接脚本给出，见 kernel/kernel.ld */

/* 用户程序与它的栈所在的 2 MiB 页，不能被当作空闲帧发出去 */
#define USER_PAGE_BASE 0x200000ul
#define USER_PAGE_END  0x400000ul

static unsigned long bitmap[BITMAP_WORDS];
static unsigned long total_frames;
static unsigned long free_frames;
static unsigned long next_frame;      /* 分配游标：避免每次都从头扫 */

static inline void frame_mark_used(unsigned long frame) { bitmap[frame / 64] |= 1ul << (frame % 64); }
static inline void frame_mark_free(unsigned long frame) { bitmap[frame / 64] &= ~(1ul << (frame % 64)); }
static inline int frame_is_used(unsigned long frame) { return (bitmap[frame / 64] >> (frame % 64)) & 1ul; }

static void reserve_range(unsigned long start, unsigned long end)
{
    unsigned long first = start / FRAME_SIZE;
    unsigned long last = (end + FRAME_SIZE - 1) / FRAME_SIZE;

    if (last > MAX_FRAMES)
        last = MAX_FRAMES;
    for (unsigned long f = first; f < last; ++f)
        frame_mark_used(f);
}

void pmm_init(void)
{
    /* 先假定全部占用，再按 E820 的可用区间放开 */
    for (unsigned long i = 0; i < BITMAP_WORDS; ++i)
        bitmap[i] = ~0ul;

    total_frames = 0;

    for (unsigned int i = 0; i < e820_count && i < E820_MAX_ENTRIES; ++i) {
        if (e820_entries[i].type != E820_TYPE_USABLE)
            continue;

        unsigned long long base = e820_entries[i].base;
        unsigned long long end = base + e820_entries[i].length;

        if (base >= (unsigned long long)MAX_FRAMES * FRAME_SIZE)
            continue;
        if (end > (unsigned long long)MAX_FRAMES * FRAME_SIZE)
            end = (unsigned long long)MAX_FRAMES * FRAME_SIZE;

        unsigned long first = (unsigned long)((base + FRAME_SIZE - 1) / FRAME_SIZE);
        unsigned long last = (unsigned long)(end / FRAME_SIZE);

        for (unsigned long f = first; f < last; ++f) {
            frame_mark_free(f);
            ++total_frames;
        }
    }

    /* 内核镜像（含位图本身）和用户页不能被分配出去 */
    reserve_range(0, (unsigned long)kernel_end);
    reserve_range(USER_PAGE_BASE, USER_PAGE_END);

    free_frames = 0;
    for (unsigned long f = 0; f < MAX_FRAMES; ++f)
        if (!frame_is_used(f))
            ++free_frames;

    next_frame = 0;
}

unsigned long pmm_alloc(void)
{
    if (free_frames == 0)
        return 0;

    /* 从游标处向后找，找不到再回到低位找一遍 */
    for (unsigned long pass = 0; pass < 2; ++pass) {
        unsigned long begin = (pass == 0) ? next_frame : 0;
        unsigned long end = (pass == 0) ? MAX_FRAMES : next_frame;

        for (unsigned long f = begin; f < end; ++f) {
            if (frame_is_used(f))
                continue;
            frame_mark_used(f);
            --free_frames;
            next_frame = f + 1;
            return f * FRAME_SIZE;
        }
    }
    return 0;
}

int pmm_free(unsigned long address)
{
    if (address == 0 || (address % FRAME_SIZE) != 0)
        return -1;

    unsigned long f = address / FRAME_SIZE;
    if (f >= MAX_FRAMES || !frame_is_used(f))
        return -1;

    frame_mark_free(f);
    ++free_frames;
    if (f < next_frame)
        next_frame = f;
    return 0;
}

unsigned long pmm_total_frames(void) { return total_frames; }
unsigned long pmm_free_frames(void) { return free_frames; }

/* 启动时自检：分配几帧、释放、确认计数回到原值 */
void pmm_selftest(void)
{
    unsigned long before = free_frames;
    unsigned long frames[3];

    kputs("pmm: total=");
    kputdec(total_frames);
    kputs(" free=");
    kputdec(free_frames);
    kputs("\n");

    for (int i = 0; i < 3; ++i) {
        frames[i] = pmm_alloc();
        kputs("pmm: alloc ");
        kputhex(frames[i]);
        kputs("\n");
    }

    for (int i = 0; i < 3; ++i)
        pmm_free(frames[i]);

    kputs("pmm: free after release=");
    kputdec(free_frames);
    kputs(free_frames == before ? " (ok)\n" : " (MISMATCH)\n");

    /* 负向检查：非对齐地址、重复释放都必须被拒绝 */
    kputs("pmm: reject unaligned=");
    kputs(pmm_free(0x123) != 0 ? "ok" : "FAIL");
    kputs(" double-free=");
    kputs(pmm_free(frames[0]) != 0 ? "ok" : "FAIL");
    kputs("\n");
}
