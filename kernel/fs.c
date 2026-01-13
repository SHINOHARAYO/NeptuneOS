#include "kernel/fs.h"

#include <stddef.h>

extern const uint8_t user_image_hello[];
extern const uint64_t user_image_hello_len;
extern const uint8_t user_image_shell[];
extern const uint64_t user_image_shell_len;
extern const uint8_t user_image_init[];
extern const uint64_t user_image_init_len;
extern const uint8_t user_image_echo[];
extern const uint64_t user_image_echo_len;

static struct memfs_file memfs_files[4];
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
    memfs_files[2].path = "/bin/init";
    memfs_files[2].data = user_image_init;
    memfs_files[2].size = user_image_init_len;
    memfs_files[3].path = "/bin/echo";
    memfs_files[3].data = user_image_echo;
    memfs_files[3].size = user_image_echo_len;
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

uint64_t memfs_list(char *buf, uint64_t len)
{
    if (!buf || len == 0) {
        return 0;
    }
    memfs_init();
    uint64_t written = 0;
    for (size_t i = 0; i < sizeof(memfs_files) / sizeof(memfs_files[0]); ++i) {
        const char *path = memfs_files[i].path;
        if (!path) {
            continue;
        }
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
