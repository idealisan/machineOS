; Machine OS - 用户态进出与系统调用入口
; SPDX-License-Identifier: AGPL-3.0-or-later
;
; 协作式：同一时刻只有一个用户程序，内核在 enter_user() 处等待它退出。
; syscall/sysret 不切换栈，所以入口第一件事就是换到内核栈。
;
; 寄存器约定：syscall 只允许破坏 rax（返回值）、rcx、r11。
; 内核内部要挪用 r8/r9/r10，因此必须自己保存，否则会悄悄改坏用户程序
; 一直放在这些寄存器里的值。

[bits 64]

global enter_user
global syscall_entry
global sys_exit_to_kernel
extern syscall_dispatch

section .bss
align 16
user_rsp_save:   resq 1
kernel_rsp_save: resq 1
align 16
syscall_stack_bottom:
    resb 16384
syscall_stack_top:

section .text

; void enter_user(void (*entry)(void *), unsigned long user_stack, void *arg)
;   rdi = 入口地址, rsi = 用户栈顶, rdx = 传给入口的参数
enter_user:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [kernel_rsp_save], rsp       ; 用户程序退出后回到这里

    push qword 0x2b                 ; SS：用户数据，RPL=3
    push rsi                        ; RSP
    push qword 0x2                  ; RFLAGS：IF=0，内核尚未建立 IDT
    push qword 0x33                 ; CS：用户代码，RPL=3
    push rdi                        ; RIP
    mov rdi, rdx                    ; 第一个参数
    iretq

; 用户程序退出或异常结束时回到这里（rsp 已由 sys_exit_to_kernel 恢复）
user_return:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; syscall 入口：rax=编号, rdi/rsi/rdx=参数，返回 rax
syscall_entry:
    mov [user_rsp_save], rsp
    mov rsp, syscall_stack_top

    push rcx                        ; 用户 RIP（syscall 硬件保存）
    push r11                        ; 用户 RFLAGS
    push r8
    push r9
    push r10
    push rax                        ; 仅用于 16 字节对齐，返回值会覆盖它

    mov r8, rdx                     ; 重排成 C 函数的参数顺序
    mov r9, rsi
    mov r10, rdi
    mov rdi, rax
    mov rsi, r10
    mov rdx, r9
    mov rcx, r8
    call syscall_dispatch

    add rsp, 8                      ; 丢掉对齐用的占位，不能 pop 到 rax 里，
                                    ; 否则会把系统调用的返回值覆盖掉
    pop r10
    pop r9
    pop r8
    pop r11
    pop rcx
    mov rsp, [user_rsp_save]
    o64 sysret

; 结束用户态，回到 enter_user() 之后的调用者
sys_exit_to_kernel:
    mov rsp, [kernel_rsp_save]
    jmp user_return
