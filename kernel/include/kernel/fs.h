#pragma once

#include <stdint.h>

struct memfs_file {
    const char *path;
    const uint8_t *data;
    uint64_t size;
};

const struct memfs_file *memfs_lookup(const char *path);
uint64_t memfs_read(const struct memfs_file *file, uint64_t offset, void *buf, uint64_t len);
uint64_t memfs_list(char *buf, uint64_t len);
