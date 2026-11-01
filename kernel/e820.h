/* BIOS E820 内存地图（由 stage 2 在实模式下采集）
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef MACHINE_OS_E820_H
#define MACHINE_OS_E820_H

struct e820_entry {
    unsigned long long base;
    unsigned long long length;
    unsigned int type;      /* 1 = 可用内存 */
    unsigned int attr;      /* ACPI 3.0 扩展属性 */
};

#define E820_TYPE_USABLE 1
#define E820_MAX_ENTRIES 32

extern unsigned int e820_count;
extern struct e820_entry e820_entries[];

#endif
