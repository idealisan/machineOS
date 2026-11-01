; Machine OS - stage 1 boot sector
; SPDX-License-Identifier: AGPL-3.0-or-later
;
; 由 BIOS 加载到 0x7C00，处于 16 位实模式。
; 目标：建立可预期的寄存器/段环境，输出启动标识，然后停机。
; 这一版不读磁盘、不进入保护模式，只验证引导路径本身。

[bits 16]
[org 0x7c00]

start:
    cli                     ; 关闭中断，避免 BIOS 遗留的中断在切换栈时触发
    xor ax, ax
    mov ds, ax              ; DS = 0
    mov es, ax              ; ES = 0
    mov ss, ax              ; SS = 0
    mov sp, 0x7c00          ; 栈从引导扇区下方向下生长，不会踩到自身
    sti
    cld                     ; 字符串指令统一向高地址方向

    mov si, banner
.print:
    lodsb                   ; AL = [DS:SI]，SI++
    test al, al
    jz .halt
    mov ah, 0x0e            ; BIOS 电传打字输出
    mov bx, 0x0007          ; 页 0，浅灰字
    int 0x10
    jmp .print

.halt:
    cli                     ; 没有可调度的工作，停机等待
.hang:
    hlt
    jmp .hang

banner:
    db 13, 10
    db "Machine OS", 13, 10
    db "stage 1: boot sector running", 13, 10
    db 0

    times 510-($-$$) db 0   ; 补足到 510 字节
    dw 0xaa55               ; 传统 BIOS 引导签名
