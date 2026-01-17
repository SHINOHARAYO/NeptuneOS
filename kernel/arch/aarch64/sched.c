#include <kernel/sched.h>
#include <kernel/heap.h>
#include <kernel/mem.h>
#include <arch/processor.h>
#include <arch/context.h>
#include <stddef.h>

void arch_thread_setup(struct thread *t, void (*trampoline)(void))
{
    /* Initialize stack top */
    uint64_t stack_top = (uint64_t)t->stack + STACK_SIZE;
    
    /* Align stack to 16 bytes */
    stack_top &= ~0xF;

    /* Initialize context */
    t->ctx.sp = stack_top;
    t->ctx.lr = (uint64_t)trampoline;
    t->ctx.fp = 0;
    
    /* Zero other registers? Not strictly needed for kernel threads */
}

extern void enter_user_aarch64(uint64_t entry, uint64_t stack, uint64_t pml4);

void arch_enter_user(uint64_t entry, uint64_t stack, uint64_t pml4)
{
    enter_user_aarch64(entry, stack, pml4);
    __builtin_unreachable();
}

void arch_thread_switch(struct thread *next)
{
    /* No per-cpu kernel stack update needed yet for simple threading.
       But if we want syscalls to work later, we will need it.
       For now, just a stub or trivial implementation. */
    (void)next;
}
