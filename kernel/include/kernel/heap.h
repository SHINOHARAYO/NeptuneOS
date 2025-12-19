#pragma once

#include <stddef.h>
#include <stdint.h>

#define KHEAP_MAX_SLAB_CLASSES 8

struct kheap_stats {
    uint64_t total_allocs;
    uint64_t total_frees;
    uint64_t slab_allocs[KHEAP_MAX_SLAB_CLASSES];
    uint64_t slab_reuses[KHEAP_MAX_SLAB_CLASSES];
    uint64_t large_allocs;
    uint64_t large_reuses;
    uint64_t free_slab_bytes;
    uint64_t free_large_bytes;
};

void kheap_init(void);
void *kalloc(size_t size, size_t align);
void *kalloc_zero(size_t size, size_t align);
void kalloc_enable_frees(void);
void kfree(void *ptr); /* no-op for bump allocator */
void kheap_get_stats(struct kheap_stats *out);
void kheap_dump_stats(void);
int kheap_ready(void);
int kheap_verify(void);
