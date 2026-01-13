#pragma once

#include <stdint.h>

struct block_device;

struct fat_file {
    uint32_t start_cluster;
    uint32_t size;
    uint32_t dir_sector;
    uint16_t dir_offset;
    uint8_t is_dir;
    uint8_t attr;
};

int fat_init(struct block_device *dev);
int fat_open(const char *path, struct fat_file *out);
int fat_open_dir(const char *path, struct fat_file *out);
int fat_create(const char *path, struct fat_file *out);
int fat_mkdir(const char *path);
int64_t fat_read(struct fat_file *file, uint64_t *offset, void *buf, uint64_t len);
int64_t fat_write(struct fat_file *file, uint64_t *offset, const void *buf, uint64_t len);
uint64_t fat_list(char *buf, uint64_t len);
uint64_t fat_list_dir(const char *path, char *buf, uint64_t len);
