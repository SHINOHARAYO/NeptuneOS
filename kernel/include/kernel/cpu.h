#pragma once
#include <stdint.h>
#include <stddef.h>

/* Per-CPU data structure structure used by syscall entry */
struct cpu_data {
    uint64_t kernel_stack; /* Offset 0 */
    uint64_t user_rsp;     /* Offset 8 (scratch) */
    struct thread *current_thread; /* Offset 16 */
    int cpu_id;
};


void cpu_init(void);
struct cpu_data *cpu_get_current(void);
void cpu_set_kernel_stack(uint64_t stack_top);
