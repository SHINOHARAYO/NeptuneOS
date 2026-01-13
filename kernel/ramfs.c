#include "kernel/ramfs.h"
#include "kernel/heap.h"

#include <stdint.h>

#define RAMFS_MAX_FILES 32
#define RAMFS_PATH_MAX 64
#define RAMFS_GROW_STEP 64

struct ramfs_file {
    char path[RAMFS_PATH_MAX];
    uint8_t *data;
    uint64_t size;
    uint64_t capacity;
    uint8_t used;
};

static struct ramfs_file ramfs_files[RAMFS_MAX_FILES];

static uint64_t str_len(const char *s)
{
    uint64_t len = 0;
    while (s && s[len]) {
        ++len;
    }
    return len;
}

static int streq(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

static void copy_path(char *dst, const char *src)
{
    uint64_t i = 0;
    for (; src[i] && i + 1 < RAMFS_PATH_MAX; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int ramfs_expand(struct ramfs_file *file, uint64_t need)
{
    if (!file) {
        return -1;
    }
    if (need <= file->capacity) {
        return 0;
    }
    uint64_t new_cap = file->capacity;
    if (new_cap == 0) {
        new_cap = RAMFS_GROW_STEP;
    }
    while (new_cap < need) {
        new_cap += RAMFS_GROW_STEP;
    }
    uint8_t *new_data = (uint8_t *)kalloc(new_cap, 16);
    if (!new_data) {
        return -1;
    }
    for (uint64_t i = 0; i < new_cap; ++i) {
        new_data[i] = 0;
    }
    for (uint64_t i = 0; i < file->size; ++i) {
        new_data[i] = file->data[i];
    }
    if (file->data) {
        kfree(file->data);
    }
    file->data = new_data;
    file->capacity = new_cap;
    return 0;
}

int ramfs_open(const char *path, struct ramfs_file **out)
{
    if (!path || !out) {
        return -1;
    }
    if (path[0] != '/') {
        return -1;
    }
    if (str_len(path) >= RAMFS_PATH_MAX) {
        return -1;
    }
    for (uint64_t i = 0; i < RAMFS_MAX_FILES; ++i) {
        if (ramfs_files[i].used && streq(ramfs_files[i].path, path)) {
            *out = &ramfs_files[i];
            return 0;
        }
    }
    for (uint64_t i = 0; i < RAMFS_MAX_FILES; ++i) {
        if (!ramfs_files[i].used) {
            ramfs_files[i].used = 1;
            ramfs_files[i].data = NULL;
            ramfs_files[i].size = 0;
            ramfs_files[i].capacity = 0;
            copy_path(ramfs_files[i].path, path);
            *out = &ramfs_files[i];
            return 0;
        }
    }
    return -1;
}

int64_t ramfs_read(struct ramfs_file *file, uint64_t *offset, void *buf, uint64_t len)
{
    if (!file || !offset || !buf || len == 0) {
        return 0;
    }
    if (*offset >= file->size) {
        return 0;
    }
    uint64_t remaining = file->size - *offset;
    if (len > remaining) {
        len = remaining;
    }
    uint8_t *dst = (uint8_t *)buf;
    for (uint64_t i = 0; i < len; ++i) {
        dst[i] = file->data[*offset + i];
    }
    *offset += len;
    return (int64_t)len;
}

int64_t ramfs_write(struct ramfs_file *file, uint64_t *offset, const void *buf, uint64_t len)
{
    if (!file || !offset || !buf || len == 0) {
        return 0;
    }
    uint64_t end = *offset + len;
    if (end < *offset) {
        return -1;
    }
    if (ramfs_expand(file, end) != 0) {
        return -1;
    }
    const uint8_t *src = (const uint8_t *)buf;
    for (uint64_t i = 0; i < len; ++i) {
        file->data[*offset + i] = src[i];
    }
    if (end > file->size) {
        file->size = end;
    }
    *offset = end;
    return (int64_t)len;
}

uint64_t ramfs_list(char *buf, uint64_t len)
{
    if (!buf || len == 0) {
        return 0;
    }
    uint64_t written = 0;
    for (uint64_t i = 0; i < RAMFS_MAX_FILES; ++i) {
        if (!ramfs_files[i].used) {
            continue;
        }
        const char *path = ramfs_files[i].path;
        for (uint64_t j = 0; path[j] != '\0'; ++j) {
            if (written + 1 >= len) {
                return written;
            }
            buf[written++] = path[j];
        }
        if (written + 1 >= len) {
            return written;
        }
        buf[written++] = '\n';
    }
    return written;
}
