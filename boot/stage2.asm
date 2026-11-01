; Machine OS - stage 2：实模式 → 32 位保护模式 → 64 位长模式
; SPDX-License-Identifier: AGPL-3.0-or-later
;
; 由 stage 1 加载到 0x7E00 并跳入，此时为 16 位实模式、CS=0。
; 流程：A20 → VBE 图形模式 → GDT → 保护模式(32) → 清 BSS → 页表
;       → 长模式(64) → 设栈 → call kernel_main
;
; 清 BSS 放在建立页表之前，因为页表自身就在 .bss 里：
; 先清零再填表，就不会把刚填好的页表又刷掉。

[bits 16]
section .text.entry
global stage2_entry
global vbe_ok
global vbe_mode_block
extern kernel_main
extern __bss_start
extern __bss_end

VBE_MODE_W equ 1024
VBE_MODE_H equ 768
VBE_MODE_BPP equ 24

stage2_entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    cld

    mov si, msg_stage2
    call print16

    ; ---- 1. 打开 A20 ----
    in al, 0x92                  ; PS/2 控制端口 A 的快速 A20 门
    test al, 0x02
    jnz .a20_done
    or al, 0x02
    and al, 0xfe                 ; bit0 是快速复位，必须清零
    out 0x92, al
.a20_done:
    call a20_check
    jc .fail_a20

    ; ---- 2. 设置 VBE 图形模式（只是尝试，失败则内核退回文本模式）----
    call vbe_setup

    ; ---- 3. 加载 GDT ----
    lgdt [gdt_desc]

    ; ---- 4. 进入 32 位保护模式 ----
    mov eax, cr0
    or eax, 0x01                 ; CR0.PE
    mov cr0, eax
    jmp 0x08:pm_entry            ; 远跳刷新 CS

.fail_a20:
    mov si, msg_fail
    call print16
    cli
.hang:
    hlt
    jmp .hang

; ---------------------------------------------------------------- VBE
; 扫描 VBE 模式表，找到 1024x768x24 直接色且支持线性帧缓冲的模式。
; 找到后把模式信息块留在 vbe_mode_block 里，供 C 内核读取。
vbe_setup:
    mov dword [vbe_ctrl], 0x32454256   ; 'VBE2'，请求 BIOS 填充 VBE 2.0 字段
    mov ax, 0x4f00
    mov di, vbe_ctrl
    int 0x10
    cmp ax, 0x004f
    jne .not_found

    ; 模式表是 far pointer：偏移在 0x0E，段在 0x10
    mov ax, [vbe_ctrl + 0x10]
    mov fs, ax
    mov ax, [vbe_ctrl + 0x0e]
    mov [vbe_cursor], ax

.next:
    mov si, [vbe_cursor]
    mov cx, [fs:si]
    cmp cx, 0xffff               ; 0xFFFF 表示列表结束
    je .not_found
    add word [vbe_cursor], 2

    mov ax, 0x4f01
    mov di, vbe_mode_block
    int 0x10
    cmp ax, 0x004f
    jne .next

    test byte [vbe_mode_block + 0x00], 0x80   ; ModeAttributes：支持线性帧缓冲
    jz .next
    cmp word [vbe_mode_block + 0x12], VBE_MODE_W
    jne .next
    cmp word [vbe_mode_block + 0x14], VBE_MODE_H
    jne .next
    cmp byte [vbe_mode_block + 0x19], VBE_MODE_BPP
    jne .next
    cmp byte [vbe_mode_block + 0x1b], 6       ; MemoryModel：6 = 直接色
    jne .next

    ; 命中：用 0x4000 打开线性帧缓冲
    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4f02
    int 0x10
    cmp ax, 0x004f
    jne .not_found

    mov byte [vbe_ok], 1
    ret

.not_found:
    mov byte [vbe_ok], 0
    ret

; 通过 0xFFFF:0x0510 与 0x0000:0x0500 是否别名来判断 A20 是否生效
a20_check:
    push ds
    push es
    pusha
    xor ax, ax
    mov ds, ax
    mov ax, 0xffff
    mov es, ax
    mov byte [0x0500], 0x00
    mov byte [es:0x0510], 0xff
    cmp byte [0x0500], 0xff
    je .closed
    mov byte [0x0500], 0xff
    mov byte [es:0x0510], 0x00
    cmp byte [0x0500], 0x00
    je .closed
    popa
    pop es
    pop ds
    clc
    ret
.closed:
    popa
    pop es
    pop ds
    stc
    ret

print16:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    jmp print16
.done:
    ret

msg_stage2: db "Machine OS: stage 2 (A20, VBE, GDT, protected mode)", 13, 10, 0
msg_fail:   db "Machine OS: A20 failed", 13, 10, 0

; ---------------------------------------------------------------- 32 位
[bits 32]
pm_entry:
    cli                          ; 不再需要 BIOS 服务；IDT 装好之前绝不响应中断
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7c00

    ; ---- 5. BSS 清零（页表就在 BSS 里，所以必须在前）----
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    add ecx, 3
    shr ecx, 2
    xor eax, eax
    rep stosd

    ; ---- 6. 确认 CPU 支持长模式 ----
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29            ; EDX.LM
    jz .no_long_mode

    ; ---- 7. 恒等映射前 4 GiB ----
    ; PML4[0] → PDPT → 4 个 PD，每个 PD 覆盖 1 GiB，共 512×4 个 2 MiB 页。
    ; 映射到 4 GiB 是因为 VBE 线性帧缓冲通常位于 0xFD000000 一带。
    mov eax, pdpt
    or eax, 0x03                 ; 存在 + 可写
    mov [pml4], eax
    mov dword [pml4 + 4], 0

    xor ebx, ebx
.pdpt_loop:
    mov eax, pd
    mov ecx, ebx
    shl ecx, 12                  ; 每个 PD 占 4 KiB
    add eax, ecx
    or eax, 0x03
    mov [pdpt + ebx * 8], eax
    mov dword [pdpt + ebx * 8 + 4], 0
    inc ebx
    cmp ebx, 4
    jb .pdpt_loop

    mov edi, pd
    mov eax, 0x83                ; 存在 + 可写 + 2 MiB 页
    mov ecx, 2048                ; 4 个 PD，共 2048 项
.fill_pd:
    mov [edi], eax
    mov dword [edi + 4], 0
    add edi, 8
    add eax, 0x200000
    dec ecx
    jnz .fill_pd

    ; ---- 8. 打开 PAE、LME，再开分页进入长模式 ----
    mov eax, cr4
    or eax, 1 << 5               ; CR4.PAE
    mov cr4, eax

    mov eax, pml4
    mov cr3, eax

    mov ecx, 0xc0000080          ; IA32_EFER
    rdmsr
    or eax, 1 << 8               ; EFER.LME
    wrmsr

    mov eax, cr0
    or eax, 0x80000000           ; CR0.PG（PE 已置位）→ 进入长模式
    mov cr0, eax

    jmp 0x18:long_entry

.no_long_mode:
    mov dword [0xb8000], 0x4f4e4f4e   ; 屏幕上打 "NONO"
    cli
    hlt
    jmp $

; ---------------------------------------------------------------- 64 位
[bits 64]
long_entry:
    cli                          ; IDT 尚未建立，IF 必须为 0
    cld
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; ---- 9. 设置栈，满足 System V AMD64 ABI 的 16 字节对齐 ----
    mov rsp, stack_top
    xor rbp, rbp

    ; ---- 10. 进入 C ----
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------- 数据
section .data

align 4
gdt:
    dq 0x0000000000000000        ; 0x00 空描述符
    dq 0x00cf9a000000ffff        ; 0x08 32 位代码
    dq 0x00cf92000000ffff        ; 0x10 32 位数据
    dq 0x00af9a000000ffff        ; 0x18 64 位代码
    dq 0x00af92000000ffff        ; 0x20 64 位数据
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

align 16
vbe_ctrl:      times 512 db 0    ; VBE 控制器信息（模式表指针在这里）
align 16
vbe_mode_block: times 256 db 0   ; 命中的模式信息块，C 内核按 VBE 规范偏移读取
vbe_ok:        db 0              ; 1 = 已切换到目标图形模式
vbe_cursor:    dw 0

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

align 4096
pml4: resb 4096
pdpt: resb 4096
pd:   resb 4096 * 4
