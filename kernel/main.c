/* Machine OS - C 语言入口
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 约定：System V AMD64 ABI（与 Linux 上 C 编译器一致），
 * 但系统调用接口将来由本内核自行定义。
 */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_ATTR   0x0f              /* 黑底白字 */

static volatile unsigned short *const vga = (volatile unsigned short *)0xb8000;
static unsigned row;
static unsigned col;

static void clear_screen(void)
{
    for (unsigned i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i)
        vga[i] = (unsigned short)((VGA_ATTR << 8) | ' ');
    row = 0;
    col = 0;
}

static void scroll(void)
{
    for (unsigned i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; ++i)
        vga[i] = vga[i + VGA_WIDTH];
    for (unsigned i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; ++i)
        vga[i] = (unsigned short)((VGA_ATTR << 8) | ' ');
    row = VGA_HEIGHT - 1;
}

static void put_char(char c)
{
    if (c == '\n') {
        col = 0;
        if (++row == VGA_HEIGHT)
            scroll();
        return;
    }
    vga[row * VGA_WIDTH + col] = (unsigned short)((VGA_ATTR << 8) | (unsigned char)c);
    if (++col == VGA_WIDTH) {
        col = 0;
        if (++row == VGA_HEIGHT)
            scroll();
    }
}

static void put_string(const char *s)
{
    while (*s)
        put_char(*s++);
}

static void put_hex(unsigned long value)
{
    static const char digits[] = "0123456789abcdef";
    put_string("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        put_char(digits[(value >> shift) & 0xf]);
}

/* 供后续验证长模式与页表状态：读 CR0 / CR3 / CR4 */
static unsigned long read_cr0(void) { unsigned long v; __asm__ volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static unsigned long read_cr3(void) { unsigned long v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static unsigned long read_cr4(void) { unsigned long v; __asm__ volatile("mov %%cr4, %0" : "=r"(v)); return v; }

void kernel_main(void)
{
    clear_screen();

    put_string("Machine OS\n");
    put_string("stage 3: kernel_main() running in 64-bit long mode\n");
    put_string("ABI: System V AMD64 (C calling convention)\n\n");

    put_string("cr0 = ");
    put_hex(read_cr0());
    put_string("  (PG|PE set => paging + protected mode)\n");
    put_string("cr3 = ");
    put_hex(read_cr3());
    put_string("  (PML4 physical address)\n");
    put_string("cr4 = ");
    put_hex(read_cr4());
    put_string("  (PAE set)\n\n");

    put_string("stack, bss and page tables are ready.\n");
    put_string("nothing to schedule yet - CPU halted.\n");

    for (;;)
        __asm__ volatile("hlt");
}
