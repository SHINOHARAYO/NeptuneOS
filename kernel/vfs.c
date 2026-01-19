#include "kernel/vfs.h"
#include "kernel/fat.h"
#include "kernel/fs.h"
#include "kernel/ramfs.h"
#include "kernel/syscall.h"

#include "kernel/heap.h"

#include <stdint.h>

enum vfs_backend {
    VFS_BACKEND_MEMFS = 0,
    VFS_BACKEND_RAMFS,
    VFS_BACKEND_LIST,
    VFS_BACKEND_FAT,
    VFS_BACKEND_PIPE,
};

#define VFS_LIST_PATH_MAX 64
#define VFS_PATH_MAX 128

// Internal pipe definition from pipe.c
struct pipe;
extern struct pipe *pipe_alloc_struct(void);
extern int64_t pipe_read_impl(struct pipe *p, void *buf, uint64_t len);
extern int64_t pipe_write_impl(struct pipe *p, const void *buf, uint64_t len);
extern void pipe_close_impl(struct pipe *p, int is_writer);

struct vfs_file {
    enum vfs_backend backend;
    uint64_t offset;
    const struct memfs_file *mem;
    struct ramfs_file *ram;
    struct fat_file *fat;
    struct pipe *pipe; /* For pipe backend */
    int is_pipe_writer; /* To track close */
    char list_path[VFS_LIST_PATH_MAX];
    int refcount;
};

static int starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) {
        return 0;
    }
    while (*prefix) {
        if (*s != *prefix) {
            return 0;
        }
        ++s;
        ++prefix;
    }
    return 1;
}

static uint64_t str_len(const char *s)
{
    uint64_t len = 0;
    while (s && s[len]) {
        ++len;
    }
    return len;
}

static int copy_trimmed(const char *src, char *dst, uint64_t dst_len)
{
    if (!src || !dst || dst_len == 0) {
        return -1;
    }
    uint64_t len = str_len(src);
    while (len > 0 && src[len - 1] == '/') {
        --len;
    }
    if (len == 0 || len + 1 > dst_len) {
        return -1;
    }
    for (uint64_t i = 0; i < len; ++i) {
        dst[i] = src[i];
    }
    dst[len] = '\0';
    return 0;
}

static int normalize_path(const char *path, char *out, uint64_t out_len, int *out_dir)
{
    if (!path || !out || out_len < 2) {
        return -1;
    }
    if (path[0] != '/') {
        return -1;
    }
    uint64_t len = str_len(path);
    int want_dir = (len > 1 && path[len - 1] == '/');
    if (out_dir) {
        *out_dir = want_dir;
    }
    uint64_t out_pos = 0;
    out[out_pos++] = '/';
    const char *p = path;
    while (*p == '/') {
        ++p;
    }
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') {
            ++p;
        }
        uint64_t seg_len = (uint64_t)(p - start);
        if (seg_len == 1 && start[0] == '.') {
            /* skip */
        } else if (seg_len == 2 && start[0] == '.' && start[1] == '.') {
            if (out_pos > 1) {
                if (out[out_pos - 1] == '/') {
                    --out_pos;
                }
                while (out_pos > 1 && out[out_pos - 1] != '/') {
                    --out_pos;
                }
            }
        } else if (seg_len > 0) {
            if (out_pos > 1 && out[out_pos - 1] != '/') {
                if (out_pos + 1 >= out_len) {
                    return -1;
                }
                out[out_pos++] = '/';
            }
            if (out_pos + seg_len >= out_len) {
                return -1;
            }
            for (uint64_t i = 0; i < seg_len; ++i) {
                out[out_pos++] = start[i];
            }
        }
        while (*p == '/') {
            ++p;
        }
    }
    if (out_pos == 0) {
        out_pos = 1;
        out[0] = '/';
    }
    out[out_pos] = '\0';
    return 0;
}

static struct vfs_file *vfs_alloc(enum vfs_backend backend)
{
    struct vfs_file *file = (struct vfs_file *)kalloc_zero(sizeof(*file), 16);
    if (!file) {
        return NULL;
    }
    file->backend = backend;
    file->offset = 0;
    file->mem = NULL;
    file->ram = NULL;
    file->fat = NULL;
    file->list_path[0] = '\0';
    file->list_path[0] = '\0';
    file->refcount = 1;
    return file;
}

int pipe_create(struct vfs_file **reader, struct vfs_file **writer)
{
    if (!reader || !writer) return -1;
    
    struct pipe *p = pipe_alloc_struct();
    if (!p) return SYSCALL_ENOMEM;
    
    struct vfs_file *r = vfs_alloc(VFS_BACKEND_PIPE);
    struct vfs_file *w = vfs_alloc(VFS_BACKEND_PIPE);
    
    if (!r || !w) {
        /* cleanup */
        if (r) kfree(r);
        if (w) kfree(w);
        kfree(p); /* assumes pipe_alloc returns fresh alloc with no refs */
        return SYSCALL_ENOMEM;
    }
    
    r->pipe = p;
    r->is_pipe_writer = 0;
    
    w->pipe = p;
    w->is_pipe_writer = 1;
    
    *reader = r;
    *writer = w;
    return 0;
}

int vfs_open(const char *path, struct vfs_file **out)
{
    if (!path || !out) {
        return SYSCALL_EINVAL;
    }
    if (path[0] != '/') {
        return SYSCALL_EINVAL;
    }

    char norm_path[VFS_PATH_MAX];
    int want_dir = 0;
    const char *use_path = path;

    if (starts_with(path, "/dev/ls")) {
        const char *suffix = path + 7;
        if (*suffix && *suffix != '/') {
            return SYSCALL_EINVAL;
        }
        struct vfs_file *file = vfs_alloc(VFS_BACKEND_LIST);
        if (!file) {
            return SYSCALL_ENOMEM;
        }
        if (*suffix == '/' && suffix[1] != '\0') {
            if (normalize_path(suffix, file->list_path, sizeof(file->list_path), NULL) != 0) {
                vfs_close(file);
                return SYSCALL_EINVAL;
            }
        }
        *out = file;
        return SYSCALL_OK;
    }

    if (normalize_path(path, norm_path, sizeof(norm_path), &want_dir) != 0) {
        return SYSCALL_EINVAL;
    }
    use_path = norm_path;

    const struct memfs_file *mem = memfs_lookup(use_path);
    if (mem) {
        struct vfs_file *file = vfs_alloc(VFS_BACKEND_MEMFS);
        if (!file) {
            return SYSCALL_ENOMEM;
        }
        file->mem = mem;
        *out = file;
        return SYSCALL_OK;
    }

    if (starts_with(use_path, "/disk/")) {
        const char *fat_path = use_path + 6;
        struct fat_file *fat_file = (struct fat_file *)kalloc_zero(sizeof(*fat_file), 16);
        if (!fat_file) {
            return SYSCALL_ENOMEM;
        }
        if (want_dir) {
            char trimmed[VFS_LIST_PATH_MAX];
            if (copy_trimmed(fat_path, trimmed, sizeof(trimmed)) != 0) {
                kfree(fat_file);
                return SYSCALL_EINVAL;
            }
            if (fat_mkdir(trimmed) != 0 || fat_open_dir(trimmed, fat_file) != 0) {
                kfree(fat_file);
                return SYSCALL_ENOENT;
            }
        } else {
            if (fat_open(fat_path, fat_file) != 0) {
                if (fat_create(fat_path, fat_file) != 0) {
                    kfree(fat_file);
                    return SYSCALL_ENOENT;
                }
            }
        }
        struct vfs_file *file = vfs_alloc(VFS_BACKEND_FAT);
        if (!file) {
            kfree(fat_file);
            return SYSCALL_ENOMEM;
        }
        file->fat = fat_file;
        *out = file;
        return SYSCALL_OK;
    }

    if (starts_with(use_path, "/bin")) {
        return SYSCALL_ENOENT;
    }

    struct ramfs_file *ram = NULL;
    if (ramfs_open(use_path, &ram) != 0) {
        return SYSCALL_ENOMEM;
    }
    struct vfs_file *file = vfs_alloc(VFS_BACKEND_RAMFS);
    if (!file) {
        return SYSCALL_ENOMEM;
    }
    file->ram = ram;
    *out = file;
    return SYSCALL_OK;
}

int64_t vfs_read(struct vfs_file *file, void *buf, uint64_t len)
{
    if (!file || !buf) {
        return -SYSCALL_EINVAL;
    }
    if (len == 0) {
        return 0;
    }
    if (file->backend == VFS_BACKEND_MEMFS) {
        uint64_t read = memfs_read(file->mem, file->offset, buf, len);
        file->offset += read;
        return (int64_t)read;
    }
    if (file->backend == VFS_BACKEND_RAMFS) {
        return ramfs_read(file->ram, &file->offset, buf, len);
    }
    if (file->backend == VFS_BACKEND_LIST) {
        char listing[4096];
        uint64_t total = 0;
        if (file->list_path[0] != '\0' && starts_with(file->list_path, "/disk")) {
            const char *fat_path = file->list_path + 5;
            if (*fat_path == '/') {
                fat_path++;
            }
            total += fat_list_dir(fat_path, listing + total, sizeof(listing) - total);
        } else {
            total += memfs_list(listing + total, sizeof(listing) - total);
            // total += ramfs_list(listing + total, sizeof(listing) - total);
            // total += fat_list(listing + total, sizeof(listing) - total);
        }
        if (file->offset >= total) {
            return 0;
        }
        uint64_t remaining = total - file->offset;
        if (len > remaining) {
            len = remaining;
        }
        uint8_t *dst = (uint8_t *)buf;
        for (uint64_t i = 0; i < len; ++i) {
            dst[i] = (uint8_t)listing[file->offset + i];
        }
        file->offset += len;
        return (int64_t)len;
    }
    if (file->backend == VFS_BACKEND_FAT) {
        int64_t read = fat_read(file->fat, &file->offset, buf, len);
        if (read < 0) {
            return -SYSCALL_EIO;
        }
        return read;
    }
    if (file->backend == VFS_BACKEND_PIPE) {
        return pipe_read_impl(file->pipe, buf, len);
    }
    return -SYSCALL_EBADF;
}

int64_t vfs_write(struct vfs_file *file, const void *buf, uint64_t len)
{
    if (!file || !buf) {
        return -SYSCALL_EINVAL;
    }
    if (len == 0) {
        return 0;
    }
    if (file->backend == VFS_BACKEND_MEMFS) {
        return -SYSCALL_EIO;
    }
    if (file->backend == VFS_BACKEND_RAMFS) {
        return ramfs_write(file->ram, &file->offset, buf, len);
    }
    if (file->backend == VFS_BACKEND_FAT) {
        int64_t wrote = fat_write(file->fat, &file->offset, buf, len);
        if (wrote < 0) {
            return -SYSCALL_EIO;
        }
        return wrote;
    }
    if (file->backend == VFS_BACKEND_PIPE) {
        return pipe_write_impl(file->pipe, buf, len);
    }
    return -SYSCALL_EBADF;
}

void vfs_close(struct vfs_file *file)
{
    if (!file) {
        return;
    }
    if (file->refcount > 1) {
        file->refcount--;
        return;
    }
    if (file->backend == VFS_BACKEND_FAT && file->fat) {
        kfree(file->fat);
    }
    if (file->backend == VFS_BACKEND_PIPE && file->pipe) {
        pipe_close_impl(file->pipe, file->is_pipe_writer);
    }
    kfree(file);
}

struct vfs_file *vfs_dup(struct vfs_file *file)
{
    if (!file) return NULL;
    file->refcount++;
    return file;
}
