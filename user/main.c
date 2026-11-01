/* 用户态测试程序
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 用途：
 *   1. 画一个居中的红方块（证明自己真的跑起来了）
 *   2. 正常模式：alloc → 填入整数 → 求和校验 → free
 *   3. 测试模式 1..3：分别尝试写代码页、读内核内存、执行堆页，
 *      三种都应该被内核的页权限检查拦下并终止本程序
 *
 * 结果通过 write 系统调用打到串口。
 */

struct boot_info {
    unsigned int test;      /* 0 = 正常流程，1..3 = 越权访问测试 */
    unsigned int fb;
    unsigned int pitch;
    unsigned int width;
    unsigned int height;
    unsigned int red;
};

#define SYS_EXIT     0
#define SYS_WRITE    1
#define SYS_READ_KEY 2
#define SYS_ALLOC    3
#define SYS_FREE     4

/* 约定：内核只破坏 rax（返回值）、rcx、r11 */
static long sys3(long n, long a, long b, long c)
{
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

static unsigned long text_len(const char *s)
{
    unsigned long n = 0;
    while (s[n])
        ++n;
    return n;
}

static void put_str(const char *s)
{
    sys3(SYS_WRITE, (long)s, (long)text_len(s), 0);
}

static void put_hex(unsigned long value)
{
    static const char digits[] = "0123456789abcdef";
    char buf[17];

    for (int i = 0; i < 16; ++i)
        buf[i] = digits[(value >> (60 - i * 4)) & 0xf];
    buf[16] = 0;
    put_str("0x");
    put_str(buf);
}

static void put_dec(unsigned long value)
{
    char buf[21];
    int i = 0;

    if (value == 0) {
        put_str("0");
        return;
    }
    while (value && i < 20) {
        buf[i++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (i > 0) {
        char one[2] = { buf[--i], 0 };
        put_str(one);
    }
}

static void sys_exit(int status)
{
    sys3(SYS_EXIT, status, 0, 0);
    for (;;)
        ;
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

#define COUNT 2048

static void memory_test(void)
{
    long region = sys3(SYS_ALLOC, 8192, 0, 0);

    put_str("u:alloc=");
    if (region < 0) {
        put_str("FAILED\n");
        return;
    }
    put_hex((unsigned long)region);
    put_str("\n");

    volatile int *nums = (volatile int *)region;
    for (int i = 0; i < COUNT; ++i)
        nums[i] = i;

    unsigned long sum = 0;
    for (int i = 0; i < COUNT; ++i)
        sum += (unsigned long)nums[i];

    put_str("u:filled=");
    put_dec(COUNT);
    put_str(" entries, first=");
    put_dec((unsigned long)nums[0]);
    put_str(" last=");
    put_dec((unsigned long)nums[COUNT - 1]);
    put_str(" sum=");
    put_dec(sum);
    put_str("\n");

    unsigned long expected = (unsigned long)COUNT * (COUNT - 1) / 2;
    put_str("u:verify=");
    put_str(sum == expected ? "ok" : "MISMATCH");
    put_str("\n");

    if (sys3(SYS_FREE, region, 0, 0) == 0)
        put_str("u:free=ok\n");
    else
        put_str("u:free=FAILED\n");
}

/* 三种越权访问：每一种都应该被内核捕获，程序被终止 */
static void permission_tests(struct boot_info *info)
{
    switch (info->test) {
    case 1:
        put_str("u:test=write to code page\n");
        *(volatile int *)0x200000 = 123;
        break;

    case 2: {
        put_str("u:test=read kernel memory\n");
        volatile int value = *(volatile int *)0x100000;
        put_dec((unsigned long)value);
        put_str("\n");
        break;
    }

    case 3: {
        put_str("u:test=execute heap page\n");
        long region = sys3(SYS_ALLOC, 4096, 0, 0);
        put_hex((unsigned long)region);
        put_str("\n");
        ((void (*)(void))region)();
        break;
    }

    default:
        return;
    }

    put_str("u:test=NOT BLOCKED (this is a bug)\n");
}

__attribute__((section(".text.start"), noreturn))
void _start(struct boot_info *info)
{
    unsigned int size_w = 256;
    unsigned int size_h = 192;
    unsigned int x = (info->width - size_w) / 2;
    unsigned int y = (info->height - size_h) / 2;

    fill_rect(info, x, y, size_w, size_h);

    put_str("u:start\n");

    if (info->test != 0) {
        permission_tests(info);
        sys_exit(0);
    }

    memory_test();
    put_str("u:ready, press B to exit\n");

    for (;;) {
        if (sys3(SYS_READ_KEY, 0, 0, 0) == 0x30)   /* B 键按下 */
            sys_exit(0);
    }
}
