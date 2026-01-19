#pragma once

#include <stdint.h>

/* VirtIO MMIO Constants */
#define VIRTIO_MMIO_MAGIC_VALUE     0x74726976
#define VIRTIO_MMIO_VERSION         0x100
#define VIRTIO_MMIO_DEVICE_ID       0x200
#define VIRTIO_MMIO_VENDOR_ID       0x2f4d5551

/* Registers */
#define VIRTIO_REG_MAGICVALUE       0x000
#define VIRTIO_REG_VERSION          0x004
#define VIRTIO_REG_DEVICEID         0x008
#define VIRTIO_REG_VENDORID         0x00c
#define VIRTIO_REG_HOSTFEATURES     0x010
#define VIRTIO_REG_HOSTFEATURESSEL  0x014
#define VIRTIO_REG_GUESTFEATURES    0x020
#define VIRTIO_REG_GUESTFEATURESSEL 0x024
#define VIRTIO_REG_GUESTPAGESIZE    0x028
#define VIRTIO_REG_QUEUESEL         0x030
#define VIRTIO_REG_QUEUENUMMAX      0x034
#define VIRTIO_REG_QUEUENUM         0x038
#define VIRTIO_REG_QUEUEALIGN       0x03c
#define VIRTIO_REG_QUEUEPFN         0x040
#define VIRTIO_REG_QUEUE_READY      0x044
#define VIRTIO_REG_QUEUENOTIFY      0x050
#define VIRTIO_REG_INTERRUPTSTATUS  0x060
#define VIRTIO_REG_INTERRUPTACK     0x064
#define VIRTIO_REG_STATUS           0x070
#define VIRTIO_REG_QUEUE_DESC_LOW   0x080
#define VIRTIO_REG_QUEUE_DESC_HIGH  0x084
#define VIRTIO_REG_QUEUE_AVAIL_LOW  0x090
#define VIRTIO_REG_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_REG_QUEUE_USED_LOW   0x0a0
#define VIRTIO_REG_QUEUE_USED_HIGH  0x0a4

/* Status Bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_FAILED        128
#define VIRTIO_STATUS_FEATURES_OK   8
#define VIRTIO_STATUS_DRIVER_OK     4
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64

/* Device Features */
#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11

/* Virtqueues */
#define VIRTQ_DESC_F_NEXT           1
#define VIRTQ_DESC_F_WRITE          2
#define VIRTQ_DESC_F_INDIRECT       4

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
} __attribute__((packed));

/* Block Request Header */
#define VIRTIO_BLK_T_IN           0
#define VIRTIO_BLK_T_OUT          1
#define VIRTIO_BLK_T_FLUSH        4

struct virtio_blk_req_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

struct virtio_blk_config {
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;
    struct virtio_blk_geometry {
        uint16_t cylinders;
        uint8_t heads;
        uint8_t sectors;
    } geometry;
    uint32_t blk_size;
    /* ... ignored rest */
} __attribute__((packed));

void virtio_init(void);
