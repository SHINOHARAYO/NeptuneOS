#pragma once

#include <stdint.h>

struct block_device;

struct block_device {
    const char *name;
    uint64_t sector_size;
    uint64_t sectors;
    int (*read)(struct block_device *dev, uint64_t lba, uint64_t count, void *buf);
    int (*write)(struct block_device *dev, uint64_t lba, uint64_t count, const void *buf);
};

void block_init(void);
struct block_device *block_get_default(void);
struct block_device *block_get_ramdisk(void);
void block_set_default(struct block_device *dev);
int block_read(struct block_device *dev, uint64_t lba, uint64_t count, void *buf);
int block_write(struct block_device *dev, uint64_t lba, uint64_t count, const void *buf);
