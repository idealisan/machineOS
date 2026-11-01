; Machine OS - stage 2：从 16 位实模式一路进入 64 位长模式
; SPDX-License-Identifier: AGPL-3.0-or-later
;
; 由 stage 1 加载到 0x7E00 并跳入，此时为 16 位实模式、CS=0。
; 流程：A20 → GDT → 保护模式(32) → 清 BSS → 页表 → 长模式(64) → 设栈 → call kernel_main
;
; 清 BSS 放在建立页表之前，因为页表自身就在 .bss 里：
; 先清零再填表，就不会把刚填好的页表又刷掉。

[bits 16]
section .text.entry
global stage2_entry
extern kernel_main
extern __bss_start
extern __bss_end

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
    jc .fail

    ; ---- 2. 加载 GDT ----
    lgdt [gdt_desc]

    ; ---- 3. 进入 32 位保护模式 ----
    mov eax, cr0
    or eax, 0x01                 ; CR0.PE
    mov cr0, eax
    jmp 0x08:pm_entry            ; 远跳刷新 CS

.fail:
    mov si, msg_fail
    call print16
    cli
.hang:
    hlt
    jmp .hang

; 通过 0xFFFF:0x0510 与 0x0000:0x0500 是否别名来判断 A20 是否生效
; 返回 CF=0 表示已开启
a20_check:
    push ds
    push es
    pusha
    xor ax, ax
    mov ds, ax                   ; DS:0000
    mov ax, 0xffff
    mov es, ax                   ; ES:FFFF
    mov byte [0x0500], 0x00
    mov byte [es:0x0510], 0xff
    cmp byte [0x0500], 0xff      ; 若回绕，说明 A20 关闭
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

msg_stage2: db "Machine OS: stage 2 (A20, GDT, protected mode)", 13, 10, 0
msg_fail:   db "Machine OS: A20 failed", 13, 10, 0

align 4
gdt:
    dq 0x0000000000000000        ; 0x00 空描述符
    dq 0x00cf9a000000ffff        ; 0x08 32 位代码：基址 0，上限 4G，D=1
    dq 0x00cf92000000ffff        ; 0x10 32 位数据
    dq 0x00af9a000000ffff        ; 0x18 64 位代码：L=1，D=0
    dq 0x00af92000000ffff        ; 0x20 64 位数据
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

; ---------------------------------------------------------------- 32 位
[bits 32]
pm_entry:
    cli                          ; 进入保护模式后不再需要 BIOS 服务，
                                 ; 在 IDT 装好之前绝不能响应中断
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7c00

    ; ---- 4. BSS 清零（页表就住在 BSS 里，所以必须在前）----
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    add ecx, 3
    shr ecx, 2
    xor eax, eax
    rep stosd

    ; ---- 5. 确认 CPU 支持长模式 ----
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29            ; EDX.LM
    jz .no_long_mode

    ; ---- 6. 建立恒等映射：1 个 PML4 → 1 个 PDPT → 512 个 2M 页（覆盖前 1G）----
    mov eax, pdpt
    or eax, 0x03                 ; 存在 + 可写
    mov [pml4], eax
    mov dword [pml4 + 4], 0

    mov eax, pd
    or eax, 0x03
    mov [pdpt], eax
    mov dword [pdpt + 4], 0

    mov ecx, 0
    mov eax, 0x83                ; 存在 + 可写 + 2M 页
.fill_pd:
    mov [pd + ecx * 8], eax
    mov dword [pd + ecx * 8 + 4], 0
    add eax, 0x200000
    inc ecx
    cmp ecx, 512
    jb .fill_pd

    ; ---- 7. 打开 PAE、LME，再开分页进入长模式 ----
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
    mov dword [0xb8000], 0x4f4e4f4e   ; 屏幕上打 "NONO"（红字）
    cli
    hlt
    jmp $

; ---------------------------------------------------------------- 64 位
[bits 64]
long_entry:
    cli                          ; 同上：IDT 尚未建立，IF 必须为 0
    cld
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; ---- 8. 设置栈，满足 System V AMD64 ABI 的 16 字节对齐 ----
    mov rsp, stack_top
    xor rbp, rbp

    ; ---- 9. 进入 C ----
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------- 数据
section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

align 4096
pml4: resb 4096
pdpt: resb 4096
pd:   resb 4096
