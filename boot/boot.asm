; Machine OS - stage 1 (MBR boot sector)
; SPDX-License-Identifier: AGPL-3.0-or-later
;
; 职责只有一个：把磁盘上紧随其后的 payload 读到 0x7E00，然后跳过去。
; payload 由 stage2（模式切换）+ C 内核组成，链接基址固定为 0x7E00。
;
; 构建时通过 -DSTAGE2_SECTORS=n 写入需要读取的扇区数。

[bits 16]
[org 0x7c00]

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 1
%endif

LOAD_OFFSET equ 0x7e00           ; payload 加载位置（段 0，偏移 0x7E00）
MAX_OFFSET  equ 0xf800           ; 单个段内不越过 64K 边界

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    cld
    mov [drive], dl              ; BIOS 传入的引导盘号

    mov si, msg_boot
    call print

    ; ---- 用 int 13h AH=42h（LBA 扩展读）逐扇区加载 payload ----
    mov word [remain], STAGE2_SECTORS

.read:
    cmp word [remain], 0
    je .loaded

    mov ah, 0x42
    mov dl, [drive]
    mov si, dap
    int 0x13
    jc .error

    inc dword [dap + 8]          ; 下一个 LBA
    add word [dap + 4], 512      ; 下一个目标偏移
    cmp word [dap + 4], MAX_OFFSET
    ja .error
    dec word [remain]
    jmp .read

.loaded:
    mov si, msg_ok
    call print
    jmp 0x0000:LOAD_OFFSET       ; 交给 stage2

.error:
    mov si, msg_err
    call print
    cli
.hang:
    hlt
    jmp .hang

; DS:SI 处的以 0 结尾字符串，用 BIOS 电传打字输出
print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

msg_boot: db "Machine OS: loading...", 13, 10, 0
msg_ok:   db "Machine OS: jumping to stage 2", 13, 10, 0
msg_err:  db "Machine OS: disk read failed", 13, 10, 0

drive:  db 0
remain: dw 0

align 4
dap:                             ; BIOS 磁盘地址包
    db 0x10, 0                   ; 包长度、保留
    dw 1                         ; 每次读 1 个扇区
    dw LOAD_OFFSET               ; 目标偏移
    dw 0                         ; 目标段
    dq 1                         ; 起始 LBA（0 是引导扇区）

    times 510-($-$$) db 0
    dw 0xaa55
