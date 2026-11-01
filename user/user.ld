/* 用户程序链接脚本：扁平二进制，固定在 0x200000 加载
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

ENTRY(_start)

SECTIONS
{
    . = 0x200000;

    .text : ALIGN(16)
    {
        *(.text.start)
        *(.text*)
    }

    .rodata : ALIGN(16) { *(.rodata*) }
    .data   : ALIGN(16) { *(.data*) }
    .bss    : ALIGN(16) { *(COMMON) *(.bss*) }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
