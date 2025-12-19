#pragma once

#include <stdint.h>

void mem_init(uint64_t multiboot_info);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t addr);
uint64_t pmm_total_bytes(void);
uint64_t pmm_used_bytes(void);
uint64_t pmm_max_phys_addr(void);
