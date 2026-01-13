#pragma once

#include <stdint.h>

struct vfs_file;

int vfs_open(const char *path, struct vfs_file **out);
int64_t vfs_read(struct vfs_file *file, void *buf, uint64_t len);
int64_t vfs_write(struct vfs_file *file, const void *buf, uint64_t len);
void vfs_close(struct vfs_file *file);
