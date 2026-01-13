#pragma once

#include <stdint.h>

struct ramfs_file;

int ramfs_open(const char *path, struct ramfs_file **out);
int64_t ramfs_read(struct ramfs_file *file, uint64_t *offset, void *buf, uint64_t len);
int64_t ramfs_write(struct ramfs_file *file, uint64_t *offset, const void *buf, uint64_t len);
uint64_t ramfs_list(char *buf, uint64_t len);
