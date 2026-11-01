/* 用户态示例程序
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 由内核加载到 0x200000 并跳入，做两件事：
 *   1. 在画面中央画一个红色方块（不占满全屏）
 *   2. 循环轮询键盘，按下 B 就调用 exit 回到内核
 */

struct boot_info {
    unsigned int fb;       /* 帧缓冲物理地址（已映射为可访问）*/
    unsigned int pitch;    /* 每行字节数 */
    unsigned int width;
    unsigned int height;
    unsigned int red;      /* 已按 VBE 掩码打包好的红色像素值 */
};

#define SYS_EXIT     0
#define SYS_WRITE    1
#define SYS_READ_KEY 2

/* 约定：内核只破坏 rax（返回值）、rcx、r11，其余寄存器原样返回 */
static long sys3(long n, long a, long b, long c)
{
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

static void sys_exit(int status)
{
    sys3(SYS_EXIT, status, 0, 0);
    for (;;)
        ;
}

static int sys_read_key(void)
{
    return (int)sys3(SYS_READ_KEY, 0, 0, 0);
}

static void fill_rect(struct boot_info *info, unsigned int x0, unsigned int y0,
                      unsigned int w, unsigned int h)
{
    unsigned char *fb = (unsigned char *)(unsigned long)info->fb;

    for (unsigned int y = y0; y < y0 + h && y < info->height; ++y) {
        unsigned char *row = fb + (unsigned long)y * info->pitch;
        for (unsigned int x = x0; x < x0 + w && x < info->width; ++x) {
            unsigned char *px = row + (unsigned long)x * 3;
            px[0] = (unsigned char)info->red;
            px[1] = (unsigned char)(info->red >> 8);
            px[2] = (unsigned char)(info->red >> 16);
        }
    }
}

__attribute__((section(".text.start"), noreturn))
void _start(struct boot_info *info)
{
    unsigned int size_w = 256;
    unsigned int size_h = 192;
    unsigned int x = (info->width - size_w) / 2;
    unsigned int y = (info->height - size_h) / 2;

    fill_rect(info, x, y, size_w, size_h);

    for (;;) {
        if (sys_read_key() == 0x30)   /* B 键按下 */
            sys_exit(0);
    }
}
