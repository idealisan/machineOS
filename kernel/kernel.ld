/* Machine OS kernel link script
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 镜像布局：0x7C00 起是引导扇区（由 stage 1 单独汇编并拼接），
 * 0x7E00 起是 stage 2 + C 内核，即本脚本描述的 payload。
 */

ENTRY(stage2_entry)

SECTIONS
{
    . = 0x7e00;

    .text : ALIGN(16)
    {
        *(.text.entry)
        *(.text*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata*)
    }

    .data : ALIGN(16)
    {
        *(.data*)
    }

    __bss_start = .;
    .bss : ALIGN(16)
    {
        *(COMMON)
        *(.bss*)
    }
    . = ALIGN(16);
    __bss_end = .;
    kernel_end = .;

    /* 用户程序固定在 0x200000，内核镜像绝不能长到那里去 */
    ASSERT(kernel_end <= 0x200000, "kernel image overlaps user page at 0x200000")

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
