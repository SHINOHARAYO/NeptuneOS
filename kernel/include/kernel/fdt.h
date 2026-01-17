#pragma once

#include <stdint.h>

/* FDT is Big Endian */

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

#define FDT_MAGIC 0xd00dfeed
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

static inline uint32_t be32_to_cpu(uint32_t v) {
    return ((v & 0xFF000000) >> 24) |
           ((v & 0x00FF0000) >> 8)  |
           ((v & 0x0000FF00) << 8)  |
           ((v & 0x000000FF) << 24);
}

static inline uint64_t be64_to_cpu(uint64_t v) {
    uint32_t h = v >> 32;
    uint32_t l = v & 0xFFFFFFFF;
    return ((uint64_t)be32_to_cpu(l) << 32) | be32_to_cpu(h);
}

/* Parse FDT to find memory size. Returns 1 if found, 0 otherwise. */
int fdt_get_memory(uint64_t fdt_addr, uint64_t *start, uint64_t *size);
