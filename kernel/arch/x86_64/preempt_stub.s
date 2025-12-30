.section .text
.globl sched_preempt_trampoline
.type sched_preempt_trampoline, @function

.extern sched_preempt_pending
.extern sched_preempt_target
.extern sched_yield

sched_preempt_trampoline:
    pushq sched_preempt_target(%rip)
    pushfq
    cli

    push %rax
    push %rcx
    push %rdx
    push %rbx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    movb $0, sched_preempt_pending(%rip)
    call sched_yield

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rbx
    pop %rdx
    pop %rcx
    pop %rax

    popfq
    ret
