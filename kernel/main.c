/* Machine OS - C 语言入口
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 约定：System V AMD64 ABI（与 Linux 上 C 编译器一致），
 * 但系统调用接口将来由本内核自行定义。
 *
 * 当前目标：在 VBE 1024x768x24 直接色模式下渲染纯色背景。
 */

extern unsigned char vbe_ok;
extern unsigned char vbe_mode_block[256];

/* VBE 模式信息块字段偏移（VESA BIOS Extension 3.0 规范） */
#define VBE_BYTES_PER_SCANLINE 0x10
#define VBE_XRES               0x12
#define VBE_YRES               0x14
#define VBE_BPP                0x19
#define VBE_RED_POS            0x20
#define VBE_GREEN_POS          0x22
#define VBE_BLUE_POS           0x24
#define VBE_PHYS_BASE          0x28

/* 背景色：深紫 #300050 */
#define BG_R 0x30
#define BG_G 0x00
#define BG_B 0x50

#define TEXT_BUFFER ((unsigned char *)0xb8000)

/* 仅在 VBE 失败、仍停留在文本模式时使用 */
static void put_string(const char *s)
{
    static unsigned int cell;

    for (; *s; ++s) {
        if (*s == '\n')
            cell = (cell / 80 + 1) * 80;
        else {
            TEXT_BUFFER[cell * 2] = (unsigned char)*s;
            TEXT_BUFFER[cell * 2 + 1] = 0x0f;
            ++cell;
        }
    }
}

static unsigned int load16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int load32(const unsigned char *p)
{
    return load16(p) | (load16(p + 2) << 16);
}

void kernel_main(void)
{
    if (!vbe_ok) {
        put_string("Machine OS: VBE 1024x768x24 unavailable, staying in text mode\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    const unsigned char *raw = vbe_mode_block;

    unsigned int width  = load16(raw + VBE_XRES);
    unsigned int height = load16(raw + VBE_YRES);
    unsigned int pitch  = load16(raw + VBE_BYTES_PER_SCANLINE);

    unsigned char rpos = raw[VBE_RED_POS];
    unsigned char gpos = raw[VBE_GREEN_POS];
    unsigned char bpos = raw[VBE_BLUE_POS];

    unsigned char *fb = (unsigned char *)(unsigned long)load32(raw + VBE_PHYS_BASE);

    unsigned int pixel = ((unsigned int)BG_R << rpos) |
                         ((unsigned int)BG_G << gpos) |
                         ((unsigned int)BG_B << bpos);

    /* 24bpp：每像素 3 字节；扫描行之间可能有填充，所以按 pitch 而不是 width*3 跳行 */
    for (unsigned int y = 0; y < height; ++y) {
        unsigned char *row = fb + (unsigned long)y * pitch;
        for (unsigned int x = 0; x < width; ++x) {
            unsigned char *px = row + (unsigned long)x * 3;
            px[0] = (unsigned char)pixel;
            px[1] = (unsigned char)(pixel >> 8);
            px[2] = (unsigned char)(pixel >> 16);
        }
    }

    for (;;)
        __asm__ volatile("hlt");
}
