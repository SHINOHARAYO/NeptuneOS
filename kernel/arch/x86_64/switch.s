.section .text
.globl context_switch
.type context_switch, @function
context_switch:
    mov %r15, 0x00(%rdi)
    mov %r14, 0x08(%rdi)
    mov %r13, 0x10(%rdi)
    mov %r12, 0x18(%rdi)
    mov %rbx, 0x20(%rdi)
    mov %rbp, 0x28(%rdi)
    mov %rsp, 0x30(%rdi)
    lea 1f(%rip), %rax
    mov %rax, 0x38(%rdi)

    mov 0x00(%rsi), %r15
    mov 0x08(%rsi), %r14
    mov 0x10(%rsi), %r13
    mov 0x18(%rsi), %r12
    mov 0x20(%rsi), %rbx
    mov 0x28(%rsi), %rbp
    mov 0x30(%rsi), %rsp
    mov 0x38(%rsi), %rax
    jmp *%rax
1:
    ret
