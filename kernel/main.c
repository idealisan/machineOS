/* Machine OS - C 语言入口
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 协作式的最小流程：
 *   深紫色桌面（轮询键盘）→ 按下 A 启动用户程序 → 用户程序画红方块
 *   → 用户程序里按下 B 调用 exit → 回到深紫色桌面。
 *
 * 没有调度、没有内存管理：用户程序固定放在 2 MiB..4 MiB 那一页，
 * 由 stage 2 直接把该页的权限设为用户可访问。
 */

#include "io.h"
#include "pmm.h"

extern unsigned char vbe_ok;
extern unsigned char vbe_mode_block[256];
extern unsigned char user_image_start[];
extern unsigned char user_image_end[];

extern void enter_user(void (*entry)(void *), unsigned long user_stack, void *arg);
extern void syscall_init(void);
extern int kbd_poll(void);
extern void kbd_flush(void);

/* VBE 模式信息块字段偏移（VESA BIOS Extension 3.0 规范） */
#define VBE_BYTES_PER_SCANLINE 0x10
#define VBE_XRES               0x12
#define VBE_YRES               0x14
#define VBE_RED_POS            0x20
#define VBE_GREEN_POS          0x22
#define VBE_BLUE_POS           0x24
#define VBE_PHYS_BASE          0x28

#define BG_R 0x30
#define BG_G 0x00
#define BG_B 0x50

/* 用户程序所在的 2 MiB 页 */
#define USER_ENTRY 0x200000ul
#define USER_TOP   0x400000ul
#define USER_INFO  0x3ff000ul   /* boot_info 放这里，用户程序从 rdi 收到它的地址 */
#define USER_STACK 0x3ff000ul   /* 栈向下生长，不会踩到上面的 boot_info */

struct boot_info {
    unsigned int fb;
    unsigned int pitch;
    unsigned int width;
    unsigned int height;
    unsigned int red;
};

static unsigned int load16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int load32(const unsigned char *p)
{
    return load16(p) | (load16(p + 2) << 16);
}

static unsigned char *framebuffer(void)
{
    return (unsigned char *)(unsigned long)load32(vbe_mode_block + VBE_PHYS_BASE);
}

static unsigned int pitch(void) { return load16(vbe_mode_block + VBE_BYTES_PER_SCANLINE); }
static unsigned int width(void) { return load16(vbe_mode_block + VBE_XRES); }
static unsigned int height(void) { return load16(vbe_mode_block + VBE_YRES); }

static void fill_screen(unsigned int pixel)
{
    unsigned char *fb = framebuffer();
    unsigned int p = pitch();

    for (unsigned int y = 0; y < height(); ++y) {
        unsigned char *row = fb + (unsigned long)y * p;
        for (unsigned int x = 0; x < width(); ++x) {
            unsigned char *px = row + (unsigned long)x * 3;
            px[0] = (unsigned char)pixel;
            px[1] = (unsigned char)(pixel >> 8);
            px[2] = (unsigned char)(pixel >> 16);
        }
    }
}

static unsigned int make_pixel(unsigned int r, unsigned int g, unsigned int b)
{
    return (r << vbe_mode_block[VBE_RED_POS]) |
           (g << vbe_mode_block[VBE_GREEN_POS]) |
           (b << vbe_mode_block[VBE_BLUE_POS]);
}

static void copy_user_image(void)
{
    unsigned char *dst = (unsigned char *)USER_ENTRY;
    unsigned long size = (unsigned long)(user_image_end - user_image_start);

    for (unsigned long i = 0; i < size; ++i)
        dst[i] = user_image_start[i];
}

static void launch_user(void)
{
    struct boot_info *info = (struct boot_info *)USER_INFO;

    copy_user_image();

    info->fb = load32(vbe_mode_block + VBE_PHYS_BASE);
    info->pitch = pitch();
    info->width = width();
    info->height = height();
    info->red = make_pixel(0xff, 0x00, 0x00);

    syscall_init();
    kbd_flush();
    serial_putc('['); serial_putc('u'); serial_putc('s'); serial_putc('e'); serial_putc('r');
    serial_putc(']'); serial_putc(10);

    enter_user((void (*)(void *))USER_ENTRY, USER_STACK, info);

    serial_putc('['); serial_putc('b'); serial_putc('a'); serial_putc('c'); serial_putc('k');
    serial_putc(']'); serial_putc(10);
}

void kernel_main(void)
{
    serial_init();

    /* 全部屏蔽：当前不处理任何硬件中断，键盘交给轮询 */
    outb(0x21, 0xff);
    outb(0xa1, 0xff);

    pmm_init();
    pmm_selftest();

    if (!vbe_ok) {
        serial_putc('n'); serial_putc('o'); serial_putc(' '); serial_putc('v'); serial_putc('b');
        serial_putc('e'); serial_putc(10);
        for (;;)
            __asm__ volatile("hlt");
    }

    const unsigned int purple = make_pixel(BG_R, BG_G, BG_B);
    fill_screen(purple);

    serial_putc('['); serial_putc('i'); serial_putc('d'); serial_putc('l'); serial_putc('e');
    serial_putc(']'); serial_putc(10);
    kbd_flush();

    for (;;) {
        int key = kbd_poll();
        if (key < 0) {
            __asm__ volatile("pause");
            continue;
        }
        if (key == 0x1e) {   /* A 键按下 */
            launch_user();
            fill_screen(purple);   /* 用户程序退出后清掉它画的红方块 */
            kbd_flush();
        }
    }
}
