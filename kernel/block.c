#include "kernel/block.h"
#include "kernel/ata.h"

#include <stddef.h>
#include <stdint.h>

#define RAMDISK_SECTORS 8192
#define RAMDISK_SECTOR_SIZE 512
#define BLOCK_CACHE_ENTRIES 16

static uint8_t ramdisk_data[RAMDISK_SECTORS * RAMDISK_SECTOR_SIZE];
static struct block_device ramdisk_dev;
static struct block_device *default_dev = NULL;

struct block_cache_entry {
    struct block_device *dev;
    uint64_t lba;
    uint8_t data[RAMDISK_SECTOR_SIZE];
    uint8_t valid;
};

static struct block_cache_entry block_cache[BLOCK_CACHE_ENTRIES];

static void block_cache_reset(void)
{
    for (uint64_t i = 0; i < BLOCK_CACHE_ENTRIES; ++i) {
        block_cache[i].dev = NULL;
        block_cache[i].lba = 0;
        block_cache[i].valid = 0;
    }
}

static struct block_cache_entry *block_cache_lookup(struct block_device *dev, uint64_t lba)
{
    uint64_t idx = lba % BLOCK_CACHE_ENTRIES;
    struct block_cache_entry *entry = &block_cache[idx];
    if (entry->valid && entry->dev == dev && entry->lba == lba) {
        return entry;
    }
    return NULL;
}

static void block_cache_store(struct block_device *dev, uint64_t lba, const void *buf)
{
    uint64_t idx = lba % BLOCK_CACHE_ENTRIES;
    struct block_cache_entry *entry = &block_cache[idx];
    const uint8_t *src = (const uint8_t *)buf;
    for (uint64_t i = 0; i < RAMDISK_SECTOR_SIZE; ++i) {
        entry->data[i] = src[i];
    }
    entry->dev = dev;
    entry->lba = lba;
    entry->valid = 1;
}

static void ramdisk_seed_fat16(void)
{
    const uint16_t bytes_per_sector = RAMDISK_SECTOR_SIZE;
    const uint8_t sectors_per_cluster = 1;
    const uint16_t reserved = 1;
    const uint8_t fats = 1;
    const uint16_t root_entries = 128;
    const uint16_t total_sectors = RAMDISK_SECTORS;
    const uint16_t root_dir_sectors = (root_entries * 32 + bytes_per_sector - 1) / bytes_per_sector;
    uint16_t fat_size = 1;
    for (;;) {
        uint32_t data_sectors = total_sectors - reserved - (uint32_t)fats * fat_size - root_dir_sectors;
        uint32_t clusters = data_sectors / sectors_per_cluster;
        uint16_t needed = (uint16_t)(((clusters + 2) * 2 + bytes_per_sector - 1) / bytes_per_sector);
        if (needed == fat_size) {
            break;
        }
        fat_size = needed;
    }

    uint8_t *b = ramdisk_data;
    for (uint64_t i = 0; i < sizeof(ramdisk_data); ++i) {
        b[i] = 0;
    }
    b[0] = 0xEB;
    b[1] = 0x3C;
    b[2] = 0x90;
    b[3] = 'N';
    b[4] = 'E';
    b[5] = 'P';
    b[6] = 'T';
    b[7] = 'U';
    b[8] = 'N';
    b[9] = 'E';
    b[10] = 'O';
    b[11] = (uint8_t)(bytes_per_sector & 0xFF);
    b[12] = (uint8_t)(bytes_per_sector >> 8);
    b[13] = sectors_per_cluster;
    b[14] = (uint8_t)(reserved & 0xFF);
    b[15] = (uint8_t)(reserved >> 8);
    b[16] = fats;
    b[17] = (uint8_t)(root_entries & 0xFF);
    b[18] = (uint8_t)(root_entries >> 8);
    b[19] = (uint8_t)(total_sectors & 0xFF);
    b[20] = (uint8_t)(total_sectors >> 8);
    b[21] = 0xF8;
    b[22] = (uint8_t)(fat_size & 0xFF);
    b[23] = (uint8_t)(fat_size >> 8);
    b[24] = 1;
    b[26] = 1;
    b[36] = 0x29;
    b[39] = 'N';
    b[40] = 'E';
    b[41] = 'P';
    b[42] = 'T';
    b[43] = 'U';
    b[44] = 'N';
    b[45] = 'E';
    b[46] = 'O';
    b[47] = 'S';
    b[48] = ' ';
    b[49] = ' ';
    b[50] = ' ';
    b[51] = 'F';
    b[52] = 'A';
    b[53] = 'T';
    b[54] = '1';
    b[55] = '6';
    b[56] = ' ';
    b[57] = ' ';
    b[58] = ' ';
    b[510] = 0x55;
    b[511] = 0xAA;

    uint32_t fat_start = reserved * bytes_per_sector;
    b[fat_start + 0] = 0xF8;
    b[fat_start + 1] = 0xFF;
    b[fat_start + 2] = 0xFF;
    b[fat_start + 3] = 0xFF;
    b[fat_start + 4] = 0xFF;
    b[fat_start + 5] = 0xFF;

    uint32_t root_start = (reserved + fat_size) * bytes_per_sector;
    const char *name = "README  TXT";
    for (int i = 0; i < 11; ++i) {
        b[root_start + i] = (uint8_t)name[i];
    }
    b[root_start + 11] = 0x20;
    b[root_start + 26] = 0x02;
    b[root_start + 28] = 0x1A;
    b[root_start + 29] = 0x00;

    uint32_t data_start = (reserved + fat_size + root_dir_sectors) * bytes_per_sector;
    const char *msg = "NeptuneOS FAT16 volume\\n";
    for (uint32_t i = 0; msg[i] != '\0'; ++i) {
        b[data_start + i] = (uint8_t)msg[i];
    }
}

static int ramdisk_read(struct block_device *dev, uint64_t lba, uint64_t count, void *buf)
{
    if (!dev || !buf || count == 0) {
        return -1;
    }
    if (lba >= dev->sectors || count > dev->sectors - lba) {
        return -1;
    }
    uint64_t offset = lba * dev->sector_size;
    uint64_t bytes = count * dev->sector_size;
    uint8_t *dst = (uint8_t *)buf;
    for (uint64_t i = 0; i < bytes; ++i) {
        dst[i] = ramdisk_data[offset + i];
    }
    return 0;
}

static int ramdisk_write(struct block_device *dev, uint64_t lba, uint64_t count, const void *buf)
{
    if (!dev || !buf || count == 0) {
        return -1;
    }
    if (lba >= dev->sectors || count > dev->sectors - lba) {
        return -1;
    }
    uint64_t offset = lba * dev->sector_size;
    uint64_t bytes = count * dev->sector_size;
    const uint8_t *src = (const uint8_t *)buf;
    for (uint64_t i = 0; i < bytes; ++i) {
        ramdisk_data[offset + i] = src[i];
    }
    return 0;
}

void block_init(void)
{
    ramdisk_dev.name = "ramdisk0";
    ramdisk_dev.sector_size = RAMDISK_SECTOR_SIZE;
    ramdisk_dev.sectors = RAMDISK_SECTORS;
    ramdisk_dev.read = ramdisk_read;
    ramdisk_dev.write = ramdisk_write;
    ramdisk_seed_fat16();
    default_dev = &ramdisk_dev;
    block_cache_reset();

    struct block_device *ata = ata_init();
    if (ata) {
        default_dev = ata;
        block_cache_reset();
    }
}

struct block_device *block_get_default(void)
{
    return default_dev;
}

struct block_device *block_get_ramdisk(void)
{
    return &ramdisk_dev;
}

void block_set_default(struct block_device *dev)
{
    if (dev) {
        default_dev = dev;
        block_cache_reset();
    }
}

int block_read(struct block_device *dev, uint64_t lba, uint64_t count, void *buf)
{
    if (!dev || !dev->read) {
        return -1;
    }
    if (count == 1 && buf) {
        struct block_cache_entry *cached = block_cache_lookup(dev, lba);
        if (cached) {
            uint8_t *dst = (uint8_t *)buf;
            for (uint64_t i = 0; i < RAMDISK_SECTOR_SIZE; ++i) {
                dst[i] = cached->data[i];
            }
            return 0;
        }
    }
    int rc = dev->read(dev, lba, count, buf);
    if (rc == 0 && count == 1 && buf) {
        block_cache_store(dev, lba, buf);
    }
    return rc;
}

int block_write(struct block_device *dev, uint64_t lba, uint64_t count, const void *buf)
{
    if (!dev || !dev->write) {
        return -1;
    }
    int rc = dev->write(dev, lba, count, buf);
    if (rc == 0 && count == 1 && buf) {
        block_cache_store(dev, lba, buf);
    }
    return rc;
}
