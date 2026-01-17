#include <arch/psci.h>
#include <arch/processor.h>

/* 
   We use SMC (Secure Monitor Call) invocation. 
   QEMU (and many ARM64 platforms) implement PSCI via SMC or HVC.
   SMC #0 is the standard calling convention if EL3 is present (QEMU default with valid secure world).
   If we are running at EL1/EL2, HVC might be used if started by a hypervisor.
   Standard approach: try SMC.
*/

static void psci_call(uint64_t fid, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    asm volatile(
        "mov x0, %0\n"
        "mov x1, %1\n"
        "mov x2, %2\n"
        "mov x3, %3\n"
        "smc #0\n"
        : 
        : "r"(fid), "r"(arg1), "r"(arg2), "r"(arg3)
        : "x0", "x1", "x2", "x3", "memory"
    );
}

void psci_system_off(void)
{
    psci_call(PSCI_0_2_FN_SYSTEM_OFF, 0, 0, 0);
    /* Should not return */
    for(;;) arch_halt();
}

void psci_system_reset(void)
{
    psci_call(PSCI_0_2_FN_SYSTEM_RESET, 0, 0, 0);
    /* Should not return */
    for(;;) arch_halt();
}

int psci_cpu_on(uint64_t target_cpu, uint64_t entry_point, uint64_t context_id)
{
    register uint64_t x0 asm("x0") = PSCI_0_2_FN64_CPU_ON;
    register uint64_t x1 asm("x1") = target_cpu;
    register uint64_t x2 asm("x2") = entry_point;
    register uint64_t x3 asm("x3") = context_id;
    
    asm volatile(
        "smc #0\n"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3)
        : "memory"
    );
    
    return (int)x0;
}
