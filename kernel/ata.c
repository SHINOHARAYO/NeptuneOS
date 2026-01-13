#include "kernel/ata.h"
#include "kernel/block.h"
#include "kernel/io.h"
#include "kernel/log.h"

#include <stddef.h>
#include <stdint.h>

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_DF 0x20
#define ATA_SR_BSY 0x80

#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_READ 0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_FLUSH 0xE7

struct ata_device {
    uint16_t io;
    uint16_t ctrl;
    uint8_t drive;
    uint32_t sectors;
    struct block_device dev;
};

static struct ata_device ata;

static int ata_wait_not_bsy(uint16_t io)
{
    for (uint32_t i = 0; i < 100000; ++i) {
        if (!(inb(io + 7) & ATA_SR_BSY)) {
            return 0;
        }
    }
    return -1;
}

static int ata_poll(uint16_t io)
{
    uint8_t status = 0;
    for (uint32_t i = 0; i < 100000; ++i) {
        status = inb(io + 7);
        if (!(status & ATA_SR_BSY)) {
            break;
        }
    }
    if (status & (ATA_SR_ERR | ATA_SR_DF)) {
        return -1;
    }
    for (uint32_t i = 0; i < 100000; ++i) {
        status = inb(io + 7);
        if (status & ATA_SR_DRQ) {
            return 0;
        }
        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            return -1;
        }
    }
    return -1;
}

static int ata_identify(struct ata_device *dev)
{
    uint16_t io = dev->io;
    uint16_t ctrl = dev->ctrl;
    uint8_t drive = dev->drive;

    outb(ctrl, 0x02);
    outb(io + 6, (uint8_t)(0xA0 | (drive << 4)));
    outb(io + 2, 0);
    outb(io + 3, 0);
    outb(io + 4, 0);
    outb(io + 5, 0);
    outb(io + 7, ATA_CMD_IDENTIFY);

    uint8_t status = inb(io + 7);
    if (status == 0) {
        return -1;
    }
    if (ata_wait_not_bsy(io) != 0) {
        return -1;
    }
    uint8_t mid = inb(io + 4);
    uint8_t high = inb(io + 5);
    if (mid || high) {
        return -1;
    }
    if (ata_poll(io) != 0) {
        return -1;
    }

    uint16_t data[256];
    for (uint32_t i = 0; i < 256; ++i) {
        data[i] = inw(io);
    }
    uint32_t sectors = (uint32_t)data[60] | ((uint32_t)data[61] << 16);
    if (sectors == 0) {
        return -1;
    }
    dev->sectors = sectors;
    return 0;
}

static int ata_read(struct block_device *bdev, uint64_t lba, uint64_t count, void *buf)
{
    if (!bdev || !buf || count == 0) {
        return -1;
    }
    struct ata_device *dev = (struct ata_device *)bdev;
    if (lba >= dev->sectors || count > dev->sectors - lba) {
        return -1;
    }
    uint8_t *dst = (uint8_t *)buf;
    uint16_t io = dev->io;
    uint16_t ctrl = dev->ctrl;

    while (count) {
        uint8_t chunk = (count > 255) ? 255 : (uint8_t)count;
        outb(ctrl, 0x02);
        outb(io + 6, (uint8_t)(0xE0 | (dev->drive << 4) | ((lba >> 24) & 0x0F)));
        outb(io + 2, chunk);
        outb(io + 3, (uint8_t)(lba & 0xFF));
        outb(io + 4, (uint8_t)((lba >> 8) & 0xFF));
        outb(io + 5, (uint8_t)((lba >> 16) & 0xFF));
        outb(io + 7, ATA_CMD_READ);

        for (uint32_t s = 0; s < chunk; ++s) {
            if (ata_poll(io) != 0) {
                return -1;
            }
            uint16_t *out = (uint16_t *)dst;
            for (uint32_t i = 0; i < 256; ++i) {
                out[i] = inw(io);
            }
            dst += 512;
        }
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

static int ata_write(struct block_device *bdev, uint64_t lba, uint64_t count, const void *buf)
{
    if (!bdev || !buf || count == 0) {
        return -1;
    }
    struct ata_device *dev = (struct ata_device *)bdev;
    if (lba >= dev->sectors || count > dev->sectors - lba) {
        return -1;
    }
    const uint8_t *src = (const uint8_t *)buf;
    uint16_t io = dev->io;
    uint16_t ctrl = dev->ctrl;

    while (count) {
        uint8_t chunk = (count > 255) ? 255 : (uint8_t)count;
        outb(ctrl, 0x02);
        outb(io + 6, (uint8_t)(0xE0 | (dev->drive << 4) | ((lba >> 24) & 0x0F)));
        outb(io + 2, chunk);
        outb(io + 3, (uint8_t)(lba & 0xFF));
        outb(io + 4, (uint8_t)((lba >> 8) & 0xFF));
        outb(io + 5, (uint8_t)((lba >> 16) & 0xFF));
        outb(io + 7, ATA_CMD_WRITE);

        for (uint32_t s = 0; s < chunk; ++s) {
            if (ata_poll(io) != 0) {
                return -1;
            }
            const uint16_t *in = (const uint16_t *)src;
            for (uint32_t i = 0; i < 256; ++i) {
                outw(io, in[i]);
            }
            src += 512;
        }
        outb(io + 7, ATA_CMD_FLUSH);
        (void)ata_wait_not_bsy(io);
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

struct block_device *ata_init(void)
{
    ata.io = ATA_PRIMARY_IO;
    ata.ctrl = ATA_PRIMARY_CTRL;
    ata.drive = 0;
    ata.sectors = 0;
    ata.dev.name = "ata0";
    ata.dev.sector_size = 512;
    ata.dev.sectors = 0;
    ata.dev.read = ata_read;
    ata.dev.write = ata_write;

    if (ata_identify(&ata) != 0) {
        return NULL;
    }

    ata.dev.sectors = ata.sectors;
    log_info("ATA PIO disk detected");
    return &ata.dev;
}
