#include <kernel/log.h>
#include <kernel/panic.h>
#include <stdint.h>

#include <kernel/syscall.h>
#include <kernel/mmu.h>
#include <kernel/sched.h>
#include <kernel/idt.h>

/* These must match syscall.h struct layout and vectors.s stack layout */

void arm_sync_handler(struct syscall_regs *regs)
{
    uint64_t esr = 0;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    
    uint32_t ec = (esr >> 26) & 0x3F;
    
    /* SVC Instruction Execution in AArch64 state */
    if (ec == 0x15) {
        /* Syscall */
        log_info_hex("SVC entry, nr", regs->rax);
        struct interrupt_frame frame = {0}; /* Dummy */
        uint64_t ret = syscall_handle(regs, &frame);
        
        /* Advance ELR to skip SVC instruction (4 bytes) */
        regs->elr += 4;
        
        /* Put return value in X0 */
        regs->rdi = ret; /* rdi aliases to x0 in our struct */
        return;
    }
    
    /* Data Abort from same EL (0x25) or lower EL (0x24) */
    if (ec == 0x24 || ec == 0x25) {
        uint64_t far;
        __asm__ volatile("mrs %0, far_el1" : "=r"(far));
        
        /* Attempt to handle page fault */
        int fault_flags = 0;
        /* If WnR bit (bit 6) is set, it's a write */
        if (esr & (1 << 6)) {
            fault_flags |= MMU_FAULT_WRITE;
        }
        
        if (ec == 0x24) fault_flags |= MMU_FAULT_USER;
        
        if (mmu_handle_fault(far, fault_flags)) {
            return; /* Handled */
        }
        
        log_error("Unhandled Data Abort");
        log_info_hex("FAR", far);
        log_info_hex("ESR", esr);
        panic("Data Abort", esr);
    }
    
    /* Instruction Abort */
    if (ec == 0x20 || ec == 0x21) {
        uint64_t far;
        __asm__ volatile("mrs %0, far_el1" : "=r"(far));
        
        int fault_flags = MMU_FAULT_EXEC;
        if (ec == 0x20) fault_flags |= MMU_FAULT_USER;
        
        if (mmu_handle_fault(far, fault_flags)) {
            return; 
        }
        
        log_error("Unhandled Instruction Abort");
        log_info_hex("FAR", far);
        log_info_hex("ESR", esr);
        panic("Instruction Abort", esr);
    }

    log_error("Synchronous Exception!");
    log_info_hex("EC", ec);
    log_info_hex("ESR_EL1", esr);
    log_info_hex("ELR_EL1", regs->elr);
    
    panic("Synchronous Exception", esr);
}
