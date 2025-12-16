#pragma once

#include <stdint.h>

#define HIGHER_HALF_BASE 0xFFFFFFFF80000000ULL

static inline uint64_t phys_to_higher_half(uint64_t phys)
{
    return phys + HIGHER_HALF_BASE;
}

static inline uint64_t higher_half_to_phys(uint64_t virt)
{
    return virt - HIGHER_HALF_BASE;
}

static inline void *phys_to_virt(uint64_t phys)
{
    return (void *)phys_to_higher_half(phys);
}

static inline uint64_t virt_to_phys(const void *virt)
{
    return higher_half_to_phys((uint64_t)virt);
}
