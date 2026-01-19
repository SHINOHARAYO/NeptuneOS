#include "kernel/sched.h"
#include <stdint.h>
#include <stddef.h>

/* Defines x86-specific thread setup logic */
void arch_thread_setup(struct thread *thread, void (*trampoline)(void))
{
    if (!thread || !thread->stack) return;

    /* Align stack to 16 bytes and leave space for initial frame */
    uint64_t stack_top = (uint64_t)thread->stack + STACK_SIZE;
    stack_top = (stack_top & ~0xFULL) - 8;
    *(uint64_t *)stack_top = 0; /* Dummy return address for alignment */
    
    thread->ctx.rsp = stack_top;
    thread->ctx.rip = (uint64_t)trampoline;
    thread->ctx.rflags = 0x2; /* IF=0, bit 1 always 1 */
}

#include "kernel/cpu.h"
#include "kernel/gdt.h"

void arch_thread_switch(struct thread *next)
{
    if (!next || !next->stack) return;
    
    uint64_t stack_top = (uint64_t)next->stack + STACK_SIZE;
    /* Ensure alignment is maintained as established in setup */
    stack_top = (stack_top & ~0xFULL) - 8; 
    /* Actually we want the very top for TSS/MSR, not the aligned initial value. 
       Wait, TSS RSP0 is where the CPU jumps on interrupt. It should be 16-byte aligned.
       sched_create aligns it: (stack_top & ~0xFULL) - 8.
       Wait, -8 is for the dummy return address.
       For TSS, we probably want the 16-byte aligned top.
    */
    uint64_t tss_top = (uint64_t)next->stack + STACK_SIZE;
    tss_top &= ~0xFULL;
    
    gdt_set_kernel_stack(tss_top);
    cpu_set_kernel_stack(tss_top);
}

void arch_enter_user(uint64_t entry, uint64_t user_stack, uint64_t pml4_phys)
{
    uint64_t rsp0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp0));
    gdt_set_kernel_stack(rsp0);

    if (pml4_phys) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
    }

    __asm__ volatile(
        "swapgs\n"
        "pushq %[user_ss]\n"
        "pushq %[user_rsp]\n"
        "pushfq\n"
        "popq %%rax\n"
        "orq $0x200, %%rax\n"
        "pushq %%rax\n"
        "pushq %[user_cs]\n"
        "pushq %[user_rip]\n"
        "iretq\n"
        :
        : [user_ss] "i"(GDT_USER_DATA | 0x3),
          [user_rsp] "r"(user_stack),
          [user_cs] "i"(GDT_USER_CODE | 0x3),
          [user_rip] "r"(entry)
        : "rax", "memory");

    __builtin_unreachable();
}
