#pragma once

#include <stdint.h>
#include <stddef.h>

struct vfs_file;

int pipe_create(struct vfs_file **reader, struct vfs_file **writer);
int64_t pipe_read(struct vfs_file *file, void *buf, uint64_t len);
int64_t pipe_write(struct vfs_file *file, const void *buf, uint64_t len);
void pipe_close(struct vfs_file *file);
