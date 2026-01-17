#pragma once
#include <stdint.h>

#define PSCI_0_2_FN_BASE            0x84000000
#define PSCI_0_2_FN64_BASE          0xC4000000

#define PSCI_0_2_FN_SYSTEM_OFF      (PSCI_0_2_FN_BASE + 0x0008)
#define PSCI_0_2_FN_SYSTEM_RESET    (PSCI_0_2_FN_BASE + 0x0009)

#define PSCI_0_2_FN64_CPU_ON        (PSCI_0_2_FN64_BASE + 0x0003)

void psci_system_off(void);
void psci_system_reset(void);
int psci_cpu_on(uint64_t target_cpu, uint64_t entry_point, uint64_t context_id);
