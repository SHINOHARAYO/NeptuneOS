.section .text
.globl isr_syscall
.type isr_syscall, @function

.extern syscall_handle

isr_syscall:
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rdi
    push %rsi
    push %rbp
    push %rbx
    push %rdx
    push %rcx
    push %rax

    mov %rsp, %rdi
    lea 120(%rsp), %rsi
    call syscall_handle
    mov %rax, 0(%rsp)

    pop %rax
    pop %rcx
    pop %rdx
    pop %rbx
    pop %rbp
    pop %rsi
    pop %rdi
    pop %r8
    pop %r9
    pop %r10
    pop %r11
    pop %r12
    pop %r13
    pop %r14
    pop %r15

    iretq
