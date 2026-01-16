#include <kernel/log.h>
#include <kernel/panic.h>
#include <stdint.h>

void arm_sync_handler(void)
{
    uint64_t esr, elr, far;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));

    log_error("Synchronous Exception!");
    log_info_hex("ESR_EL1", esr);
    log_info_hex("ELR_EL1", elr);
    log_info_hex("FAR_EL1", far);
    
    panic("Synchronous Exception", esr);
}
