#include <arch/processor.h>
#include <kernel/cpu.h>
#include <kernel/heap.h>
#include <kernel/log.h>
#include <stddef.h>

#define MSR_GS_BASE 0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

extern void syscall_entry(void);

static struct cpu_data *bsp_cpu_data = NULL;

void cpu_init(void)
{
    /* Allocate per-cpu data for BSP */
    bsp_cpu_data = (struct cpu_data *)kalloc_zero(sizeof(struct cpu_data), 16);
    if (!bsp_cpu_data) {
        log_error("Failed to allocate BSP CPU data");
        return;
    }
    
    /* Set up stack (reuse current stack top? No, we need a fresh kernel stack for syscalls usually,
       or we grab it from the current thread?
       For simplicity, let's allocate a dedicated syscall stack for BSP for now, 
       or better, update it on context switch!
       
       Actually, `sched.c` should update `cpu_data->kernel_stack` when switching threads.
       For now, let's give it a safe stack.
    */
    uint8_t *stack = (uint8_t *)kalloc_zero(16384, 16);
    bsp_cpu_data->kernel_stack = (uint64_t)(stack + 16384);
    bsp_cpu_data->cpu_id = 0;

    /* Set GS Base MSRs */
    /* When in kernel, GS points to bsp_cpu_data. */
    arch_wrmsr(MSR_GS_BASE, (uint64_t)bsp_cpu_data);
    
    /* When in user, swapgs will load this value: */
    arch_wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)bsp_cpu_data);
    
    /* NOTE: We are currently in kernel. So GS should be valid.
       However, we haven't enabled FS/GS segments in GDT usually. 
       In 64-bit mode, segment limits are ignored but base is MSR driven.
    */
}

void cpu_set_kernel_stack(uint64_t stack_top)
{
    if (bsp_cpu_data) {
        bsp_cpu_data->kernel_stack = stack_top;
    }
}

void syscall_enable(void) 
{
    /* 1. Enable SCE (System Call Extensions) in EFER */
    uint64_t efer = arch_rdmsr(MSR_EFER);
    efer |= 1; /* Bit 0 = SCE */
    arch_wrmsr(MSR_EFER, efer);

    /* 2. Set STAR (Segment Selector) */
    uint64_t star = 0;
    star |= ((uint64_t)0x08 << 32); /* Kernel CS Base */
    star |= ((uint64_t)0x10 << 48); /* User CS Base (base for calculation) */
    arch_wrmsr(MSR_STAR, star);

    /* 3. Set LSTAR (Target RIP) */
    arch_wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* 4. Set SFMASK (RFLAGS mask) */
    arch_wrmsr(MSR_SFMASK, 0x200); /* Mask IF (clear it on entry) */
}

/* Struct matching the pushed stack in syscall_entry */
struct syscall_frame {
    uint64_t rax;
    uint64_t rcx; /* rip */
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11; /* rflags */
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};

#include <kernel/syscall.h>

/* Forward declaration of generic handler */
/* extern uint64_t syscall_handle(struct syscall_regs *regs, struct interrupt_frame *frame); declared in syscall.h */

/* Shim: Called from assembly with pointer to stack frame in RDI */
void syscall_handler_fast(struct syscall_frame *frame)
{
     /* The layout of syscall_frame matches syscall_regs exactly.
        We pass NULL for interrupt_frame as generic handler doesn't use it for syscalls 
        (it uses regs->rax etc).
     */
     struct syscall_regs *regs = (struct syscall_regs *)frame;
     
     /* Handle the syscall */
     uint64_t result = syscall_handle(regs, NULL);
     
     /* Store result in RAX for return */
     frame->rax = result;
}

