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

.globl syscall_entry
.type syscall_entry, @function
syscall_entry:
    /* 
       On entry:
       RIP = LSTAR (here)
       CS = STAR[47:32]
       SS = STAR[47:32] + 8
       RCX = Return RIP
       R11 = Return RFLAGS
       RSP = User RSP (unchanged!)
    */

    /* Swap GS to kernel GS to access per-cpu kernel stack */
    swapgs
    
    /* We need a temporary scratch space to save user RSP before we can load kernel RSP.
       Usually we use GS-relative access. 
       Let's assume %gs:0 holds the kernel stack top.
       But wait, we haven't set up GS base yet!
       
       Hack for now: We reuse the interrupt stack logic or just assume single core 
       and use a global variable? 
       No, `syscall` is tricky without GS.
       
       Let's stick to a simpler approach: 
       If we don't have per-cpu GS setup, we can't safely handle re-entrancy on multiple cores.
       But we are single core for now.
       We can store the user RSP in a global scratch variable.
    */
    mov %rsp, %gs:0x8   /* Save User RSP to GS:8 (scratch) */
    mov %gs:0x0, %rsp   /* Load Kernel RSP from GS:0 */

    /* Save generic registers */
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11 /* Saved RFLAGS */
    push %r10 
    push %r9
    push %r8
    push %rdi
    push %rsi
    push %rbp
    push %rbx
    push %rdx
    push %rcx /* Saved RIP */
    push %rax

    /* Construct struct context pointer equivalent? 
       Actually our syscall_handle takes (frame, syscall_id).
       The frame layout for syscall is different from interrupt frame.
       
       Layout pushed above:
       RAX (0), RCX (8), RDX (16), ...
    */
    
    /* For now, just call the C handler.
       Syscall ABI: System V AMD64
       Arg1 (RDI): Syscall Number (RAX)
       Arg2 (RSI): Arg1 (RDI)
       Arg3 (RDX): Arg2 (RSI)
       Arg4 (RCX): Arg3 (RDX)
       Arg5 (R8):  Arg4 (R10) -- Linux uses R10 instead of RCX because RCX is clobbered by syscall
       Arg6 (R9):  Arg5 (R8)
       
       We need to adapt to `syscall_handle` signature?
       Current `syscall_handle` (in syscall.c) probably assumes interrupt frame.
       Let's look at `syscall.c` later.
       
       Let's create a `syscall_entry_c` that matches the register state.
    */
    
    mov %rsp, %rdi  /* Pass stack pointer as context */
    call syscall_handler_fast /* New handler for fast path */
    
    /* Return path */
    pop %rax
    pop %rcx /* target RIP */
    pop %rdx
    pop %rbx
    pop %rbp
    pop %rsi
    pop %rdi
    pop %r8
    pop %r9
    pop %r10
    pop %r11 /* target RFLAGS */
    pop %r12
    pop %r13
    pop %r14
    pop %r15

    /* Restore User Stack */
    mov %gs:0x8, %rsp
    swapgs
    
    sysretq

