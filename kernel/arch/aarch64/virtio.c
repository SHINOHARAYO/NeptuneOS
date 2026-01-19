#include <drivers/virtio.h>
#include <kernel/block.h>
#include <kernel/mem.h>
#include <kernel/panic.h>
#include <kernel/log.h>
#include <kernel/mem.h>
#include <kernel/mmu.h>
#include <kernel/spinlock.h>
#include <stddef.h>

/* QEMU virt MMIO regions start at 0x0a000000, size 0x200 per device, up to 32 devices */
#define VIRTIO_MMIO_BASE  0x0a000000
#define VIRTIO_MMIO_SIZE  0x200
#define VIRTIO_MMIO_COUNT 32

/* Global driver state for the first found block device */
static uint64_t mmio_base_addr = 0;
static struct virtq_desc *desc_table;
static struct virtq_avail *avail_ring;
static struct virtq_used *used_ring;
static uint16_t queue_size;
static uint16_t last_used_idx = 0;
static spinlock_t virtio_lock;
    
static inline void cache_clean_range(volatile void *start, uint64_t size) {
    (void)start; (void)size;
    /* On QEMU, we assume coherence or skip for debug */
}

static inline void cache_inv_range(volatile void *start, uint64_t size) {
    (void)start; (void)size;
}

/* Helpers for MMIO access */
static inline uint32_t vio_read32(uint32_t offset) {
    return *(volatile uint32_t *)(mmio_base_addr + offset);
}

static inline void vio_write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(mmio_base_addr + offset) = value;
}

static int virtio_find_block_device(void) {
    for (int i = 0; i < VIRTIO_MMIO_COUNT; ++i) {
        uint64_t addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
        /* We need to map this? It's in the 1GB identity map range usually (0x0A... is < 1GB) 
           But if we dropped identity map, we need to ensure it's mapped.
           Our logical HHDM maps all physical RAM, but these are MMIO.
           We might need to map them if they are not in HHDM RAM range.
           However, standard QEMU virt map 0x0A000000 is usually accessible if 1:1 or HHDM depending on setup.
           Let's assume we need to access via VIRTUAL address. 
           Wait, HHDM usually only maps DRAM. MMIO is separate.
           We likely need to ioremap this region or use identity if active.
           But wait, start.s mapped 0-1GB as Device (Attr0) in Identity. 
           Are we still using TTBR0 identity for low addresses?
           Yes, TTBR0 is active.
           So accessing 0x0A000000 directly works?
           0x0A000000 is 160MB. Inside first 1GB.
           So yes, we can access using physical address directly if TTBR0 identity map 0-1GB is active.
        */
        volatile uint32_t *magic = (volatile uint32_t *)addr;
        if (*magic != VIRTIO_MMIO_MAGIC_VALUE) continue;
        
        volatile uint32_t *ver = (volatile uint32_t *)(addr + VIRTIO_REG_VERSION);
        if (*ver != 1 && *ver != 2) continue; // Only support 1 (Legacy) or 2 (Modern-transitional)
        
        volatile uint32_t *did = (volatile uint32_t *)(addr + VIRTIO_REG_DEVICEID);
        if (*did == 2) { // Block Device
            log_info_hex("VirtIO Block Device Version", *ver);
            
            /* Map MMIO to HHDM (Virtual) since Identity Map will be dropped */
            uint64_t page_phys = addr & ~0xFFFULL;
            uint64_t page_virt = phys_to_hhdm(page_phys);
            
            /* mmu_map_page(page_virt, page_phys, MMU_FLAG_WRITE | MMU_FLAG_DEVICE | MMU_FLAG_NOEXEC); */
            
            mmio_base_addr = page_virt + (addr & 0xFFFULL);
            // log_info_hex("VirtIO Mapped MMIO to", mmio_base_addr);
            return 1;
        }
    }
    return 0;
}

/* Allocator wrapper removed */

static void virtio_blk_req_read(struct block_device *dev, uint64_t lba, uint64_t count, void *buf);
static void virtio_blk_req_write(struct block_device *dev, uint64_t lba, uint64_t count, const void *buf);

/* Block API Wrappers */
static int vblk_read(struct block_device *dev, uint64_t lba, uint64_t count, void *buf) {
    (void)dev;
    virtio_blk_req_read(dev, lba, count, buf);
    return 0;
}

static int vblk_write(struct block_device *dev, uint64_t lba, uint64_t count, const void *buf) {
    (void)dev;
    virtio_blk_req_write(dev, lba, count, buf);
    return 0;
}

static struct block_device vblk_dev = {
    .name = "virtio-blk",
    .sector_size = 512,
    .sectors = 0, /* Filled later */
    .read = vblk_read,
    .write = vblk_write
};

void virtio_init(void) {
    spinlock_init(&virtio_lock);
    
    if (!virtio_find_block_device()) {
        log_warn("No VirtIO Block Device found.");
        return;
    }
    
    log_info_hex("VirtIO Block Device found at MMIO base", mmio_base_addr);
    
    /* 1. Reset */
    vio_write32(VIRTIO_REG_STATUS, 0);
    
    /* 2. Ack */
    uint32_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
    vio_write32(VIRTIO_REG_STATUS, status);
    
    /* 3. Features */
    uint32_t host_features = vio_read32(VIRTIO_REG_HOSTFEATURES);
    // log_debug_hex("Host Features", host_features);
    /* Negotiate: We accept what we know. Block Device needs defaults. */
    vio_write32(VIRTIO_REG_GUESTFEATURES, host_features); /* Accept all for now */
    
    status |= VIRTIO_STATUS_FEATURES_OK;
    vio_write32(VIRTIO_REG_STATUS, status);
    
    /* Re-read status to check if accepted */
    if (!(vio_read32(VIRTIO_REG_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
        log_error("VirtIO Feature Negotiation Failed");
        return;
    }
    
    /* 4. Queue Setup */
    vio_write32(VIRTIO_REG_QUEUESEL, 0); // Select Queue 0
    uint32_t qmax = vio_read32(VIRTIO_REG_QUEUENUMMAX);
    if (qmax == 0) {
        log_error("VirtIO Queue 0 not available");
        return;
    }
    
    queue_size = qmax; 
    /* clamp queue size to something reasonable if needed, e.g. 16 */
    if (queue_size > 16) queue_size = 16;
    
    vio_write32(VIRTIO_REG_QUEUENUM, queue_size);
    
    /* Calculate sizes */
    /* Desc: 16bytes * QSize */
    /* Avail: 2(flags) + 2(idx) + 2*QSize + 2(used_event) -> 6 + 2*QSize */
    /* Used: 2(flags) + 2(idx) + 8*QSize + 2(avail_event) -> 6 + 8*QSize */
    
    uint64_t desc_sz = 16 * queue_size;
    
    /* Alignment requirements: 4096 usually for PFN-based legacy */
    
    /* Alloc Memory */
    /* We alloc one page shared for all for simplicity if it fits */
    /* Alloc Memory */
    /* We need 2 pages contiguous for qsize=16 (needs ~4228 bytes including used ring alignment) */
    /* Use pmm_alloc_pages(2) */
    
    uint64_t phys_page = pmm_alloc_pages(2);
    void *virt_page = (void *)phys_to_hhdm(phys_page);
    
    /* Zero both pages */
    // memset(virt_page, 0, 4096 * 2);
    // Manual:
    uint8_t *p = (uint8_t *)virt_page;
    for(int i=0;i<4096*2;++i) p[i]=0;
    
    /* Layout: Desc/Avail on P1. Used on P2. */
    /* Actually we can pack them better now that we have contiguous memory, 
       but keeping P1/P2 split logic is fine if we just adjust pointers.
       Actually, standard layout:
       Desc: offset 0
       Avail: offset 256
       Used: offset 4096 (aligned)
    */
    
    desc_table = (struct virtq_desc *)virt_page;
    avail_ring = (struct virtq_avail *)(p + desc_sz);
    /* used_ring is at offset 4096 (phys_page + 4096) */
    used_ring  = (struct virtq_used *)(p + 4096);
    
    /* Flush Initial Queue Memory (Zeros) */
    cache_clean_range(desc_table, 4096); // First page
    cache_clean_range(used_ring, 4096);  // Second page
    
    // vio_write32(VIRTIO_REG_QUEUEALIGN, 4096);
    // vio_write32(VIRTIO_REG_QUEUEPFN, phys_page >> 12);
    
    // VirtIO V2 (Modern) Queue Setup
    uint64_t desc_phys = phys_page;
    uint64_t avail_phys = phys_page + desc_sz;
    uint64_t used_phys = phys_page + 4096;
    
    vio_write32(VIRTIO_REG_QUEUE_DESC_LOW, (uint32_t)desc_phys);
    vio_write32(VIRTIO_REG_QUEUE_DESC_HIGH, (uint32_t)(desc_phys >> 32));
    
    vio_write32(VIRTIO_REG_QUEUE_AVAIL_LOW, (uint32_t)avail_phys);
    vio_write32(VIRTIO_REG_QUEUE_AVAIL_HIGH, (uint32_t)(avail_phys >> 32));
    
    vio_write32(VIRTIO_REG_QUEUE_USED_LOW, (uint32_t)used_phys);
    vio_write32(VIRTIO_REG_QUEUE_USED_HIGH, (uint32_t)(used_phys >> 32));
    
    vio_write32(VIRTIO_REG_QUEUE_READY, 1);
    
    /* 5. Driver OK */
    status |= VIRTIO_STATUS_DRIVER_OK;
    vio_write32(VIRTIO_REG_STATUS, status);
    
    /* Read Capacity */
    /* Config space starts at 0x100 */
    volatile struct virtio_blk_config *cfg = (volatile struct virtio_blk_config *)(mmio_base_addr + 0x100);
    vblk_dev.sectors = cfg->capacity;
    
    log_info_hex("VirtIO Block Capacity (sectors)", vblk_dev.sectors);
    
    block_set_default(&vblk_dev);
}

/* Helper to wait for request */
/* Simplified: Spin on used ring. */
static void virtio_blk_submit(uint64_t sector, uint64_t count, void *buf, int write) {
    spinlock_acquire(&virtio_lock);
    
    /* Descriptor 0: Header (Read-only for driver) */
    /* Descriptor 1: Data (RW for driver if Read, RO if Write) */
    /* Descriptor 2: Status (Write-only for device) */
    
    /* Just use slot 0 for synchronous request */
    
    struct virtio_blk_req_header req;
    req.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req.reserved = 0;
    req.sector = sector;
    
    /* Desc 0: Header */
    /* We need header in accessible memory. Stack is fine if we wait. */
    /* But Device needs physical address. Stack is HHDM-mapped usually if kernel stack? */
    /* virt_to_phys(&req) works if stack is in HHDM. */
    /* Kernel stack is currently allocated from kalloc/pmm? Or start.s stack? */
    /* start.s stack is .bss equivalent? */
    /* It's safer to use a static buffer or heap for DMA. */
    
    /* Use allocated memory for DMA to ensure correct physical translation via HHDM */
    static struct virtio_blk_req_header *s_req_ptr = NULL;
    static uint8_t *s_status_ptr = NULL;
    
    if (!s_req_ptr) s_req_ptr = (void*)phys_to_hhdm(pmm_alloc_page()); /* Just waste a page for simplicity/safety early on */
    if (!s_status_ptr) s_status_ptr = (uint8_t*)s_req_ptr + sizeof(*s_req_ptr);
    
    *s_req_ptr = req;
    *s_status_ptr = 0;
    
    /* Setup Desc 0 -> Header */
    desc_table[0].addr = virt_to_phys(s_req_ptr);
    desc_table[0].len = sizeof(struct virtio_blk_req_header);
    desc_table[0].flags = VIRTQ_DESC_F_NEXT;
    desc_table[0].next = 1;
    
    /* Desc 1 -> Buffer */
    desc_table[1].addr = virt_to_phys(buf); /* Assume buffer is in identity/HHDM reachable RAM */
    desc_table[1].len = count * 512;
    desc_table[1].flags = VIRTQ_DESC_F_NEXT;
    if (!write) desc_table[1].flags |= VIRTQ_DESC_F_WRITE; /* Device writes to us */
    desc_table[1].next = 2;
    
    /* Desc 2 -> Status */
    desc_table[2].addr = virt_to_phys(s_status_ptr);
    desc_table[2].len = 1;
    desc_table[2].flags = VIRTQ_DESC_F_WRITE; /* Device writes status */
    desc_table[2].next = 0;
    
    /* Flush descriptors and headers */
    cache_clean_range(desc_table, sizeof(struct virtq_desc) * 3);
    cache_clean_range(s_req_ptr, sizeof(*s_req_ptr));
    if (write) {
        cache_clean_range(buf, count * 512);
    } else {
        /* If reading, we might want to invalidate early to prevent dirty cache eviction overwriting DMA? 
           But usually invalidation is done AFTER DMA.
           Clean-Invalidate (CIVAC) is safer if cache line is dirty. */
        cache_inv_range(buf, count * 512);
    }
    cache_inv_range(s_status_ptr, 1);
    
    /* Avail Ring */
    avail_ring->ring[avail_ring->idx % queue_size] = 0; /* Head index */
    /* Memory Barrier? */
    __asm__ volatile("dmb sy");
    avail_ring->idx++;
    
    /* Flush Avail Ring */
    cache_clean_range(avail_ring, sizeof(struct virtq_avail) + 2 * queue_size);
    
    __asm__ volatile("dmb sy");
    
    /* Notify */
    vio_write32(VIRTIO_REG_QUEUENOTIFY, 0); // Queue 0
    
    /* Polling Wait */
    while (last_used_idx == used_ring->idx) {
        /* Busy wait */
        // relax
        __asm__ volatile("yield");
        /* Polling RAM needs invalidation if non-coherent! */
        /* But used_ring->idx is in same cache line as other used entries possibly.
           We need to invalidate "used_ring->idx" periodically to see update. */
        cache_inv_range(&used_ring->idx, 2);
    }
    
    /* Invalidate Used Ring completely to read content */
    cache_inv_range(used_ring, sizeof(struct virtq_used) + 8 * queue_size);
    
    last_used_idx++;
    spinlock_release(&virtio_lock);
    
    /* Invalidate Status and Buffer if Read */
    cache_inv_range(s_status_ptr, 1);
    if (!write) {
        cache_inv_range(buf, count * 512);
    }
    
    if (*s_status_ptr != 0) {
        log_info_hex("VirtIO CMD Failed status", *s_status_ptr);
    }
}

static void virtio_blk_req_read(struct block_device *dev, uint64_t lba, uint64_t count, void *buf) {
    (void)dev;
    virtio_blk_submit(lba, count, buf, 0);
}

static void virtio_blk_req_write(struct block_device *dev, uint64_t lba, uint64_t count, const void *buf) {
    (void)dev;
    virtio_blk_submit(lba, count, (void *)buf, 1);
}
