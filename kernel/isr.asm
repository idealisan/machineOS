; Machine OS - 异常入口与统一栈帧
; SPDX-License-Identifier: AGPL-3.0-or-later
;
; 每个异常都压 [vector][error] 两个 qword，后面才是 CPU 压的
; [rip][cs][rflags][rsp][ss]，这样 C 侧可以用同一个结构体读栈帧。
; 有错误码的异常（8/10/11/12/13/14/17/21/29/30）不补 0，其余补 0。

[bits 64]
section .text

extern exception_dispatch
global isr_table

%macro ISR_NOERR 1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr%1:
    push qword %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

isr_common:
    cli
    mov rdi, rsp            ; 指向 trap_frame
    and rsp, ~0xf           ; 对齐到 16 字节，保证 C 调用约定
    call exception_dispatch
.halt:
    cli
    hlt
    jmp .halt

section .rodata
align 8
isr_table:
    dq isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7
    dq isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15
    dq isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dq isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
