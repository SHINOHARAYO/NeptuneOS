#include "kernel/fs.h"

#include <stddef.h>

extern const uint8_t user_image_hello[];
extern const uint64_t user_image_hello_len;
extern const uint8_t user_image_shell[];
extern const uint64_t user_image_shell_len;

static struct memfs_file memfs_files[2];
static int memfs_ready = 0;

static void memfs_init(void)
{
    if (memfs_ready) {
        return;
    }
    memfs_files[0].path = "/bin/hello";
    memfs_files[0].data = user_image_hello;
    memfs_files[0].size = user_image_hello_len;
    memfs_files[1].path = "/bin/shell";
    memfs_files[1].data = user_image_shell;
    memfs_files[1].size = user_image_shell_len;
    memfs_ready = 1;
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

const struct memfs_file *memfs_lookup(const char *path)
{
    if (!path) {
        return NULL;
    }
    memfs_init();
    for (size_t i = 0; i < sizeof(memfs_files) / sizeof(memfs_files[0]); ++i) {
        if (streq(memfs_files[i].path, path)) {
            return &memfs_files[i];
        }
    }
    return NULL;
}

uint64_t memfs_read(const struct memfs_file *file, uint64_t offset, void *buf, uint64_t len)
{
    if (!file || !buf || len == 0) {
        return 0;
    }
    memfs_init();
    if (offset >= file->size) {
        return 0;
    }
    uint64_t remaining = file->size - offset;
    if (len > remaining) {
        len = remaining;
    }
    const uint8_t *src = file->data + offset;
    uint8_t *dst = (uint8_t *)buf;
    for (uint64_t i = 0; i < len; ++i) {
        dst[i] = src[i];
    }
    return len;
}
