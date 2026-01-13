#include "kernel/fat.h"
#include "kernel/block.h"

#include <stdint.h>
#include <stddef.h>

#define FAT16_MIN_CLUSTERS 4085
#define FAT16_EOC 0xFFF8

struct fat_state {
    struct block_device *dev;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint16_t fat_size;
    uint32_t total_sectors;
    uint32_t root_dir_sectors;
    uint32_t fat_start;
    uint32_t root_start;
    uint32_t data_start;
    uint32_t cluster_count;
    int ready;
};

static struct fat_state fat;

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_sector(uint32_t lba, uint8_t *buf)
{
    return block_read(fat.dev, lba, 1, buf);
}

static int is_end_cluster(uint16_t value)
{
    return value >= FAT16_EOC;
}

static uint16_t fat_next_cluster(uint16_t cluster)
{
    uint32_t offset = (uint32_t)cluster * 2u;
    uint32_t sector = fat.fat_start + (offset / fat.bytes_per_sector);
    uint32_t off = offset % fat.bytes_per_sector;
    uint8_t buf[512];
    if (read_sector(sector, buf) != 0) {
        return 0;
    }
    return read_u16(&buf[off]);
}

static int fat_set_cluster(uint16_t cluster, uint16_t value)
{
    uint32_t offset = (uint32_t)cluster * 2u;
    uint32_t sector = fat.fat_start + (offset / fat.bytes_per_sector);
    uint32_t off = offset % fat.bytes_per_sector;
    uint8_t buf[512];
    if (read_sector(sector, buf) != 0) {
        return -1;
    }
    buf[off] = (uint8_t)(value & 0xFF);
    buf[off + 1] = (uint8_t)(value >> 8);
    return block_write(fat.dev, sector, 1, buf);
}

static uint16_t fat_alloc_cluster(void)
{
    uint32_t total = fat.cluster_count + 2;
    for (uint32_t cluster = 2; cluster < total; ++cluster) {
        uint16_t val = fat_next_cluster((uint16_t)cluster);
        if (val == 0x0000) {
            if (fat_set_cluster((uint16_t)cluster, FAT16_EOC) != 0) {
                return 0;
            }
            return (uint16_t)cluster;
        }
    }
    return 0;
}

static int upper_char(int c)
{
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }
    return c;
}

static int build_short_name(const char *path, char out[11])
{
    for (int i = 0; i < 11; ++i) {
        out[i] = ' ';
    }
    if (!path || path[0] == '\0') {
        return -1;
    }
    int name_len = 0;
    int ext_len = 0;
    int in_ext = 0;
    while (*path) {
        char c = *path;
        if (c == '/') {
            break;
        }
        if (c == '.') {
            if (in_ext) {
                return -1;
            }
            in_ext = 1;
            ++path;
            continue;
        }
        c = (char)upper_char((int)c);
        if (!in_ext) {
            if (name_len >= 8) {
                return -1;
            }
            out[name_len++] = c;
        } else {
            if (ext_len >= 3) {
                return -1;
            }
            out[8 + ext_len++] = c;
        }
        ++path;
    }
    return name_len > 0 ? 0 : -1;
}

static uint32_t cluster_to_sector(uint16_t cluster)
{
    return fat.data_start + ((uint32_t)(cluster - 2) * fat.sectors_per_cluster);
}

struct dir_loc {
    uint32_t sector;
    uint16_t offset;
};

static int dir_read_sector(uint32_t lba, uint8_t *buf)
{
    return read_sector(lba, buf);
}

static int dir_write_sector(uint32_t lba, const uint8_t *buf)
{
    return block_write(fat.dev, lba, 1, buf);
}

static int dir_find_entry(uint16_t dir_cluster, const char name[11], int want_dir,
                          struct fat_file *out, struct dir_loc *loc, int allow_free)
{
    uint8_t sector[512];
    uint32_t entries_per_sector = fat.bytes_per_sector / 32;
    uint32_t total_entries = (dir_cluster == 0) ? fat.root_entries : 0xFFFFFFFFu;
    uint32_t seen = 0;
    uint16_t cluster = dir_cluster;
    int saw_free = 0;
    struct dir_loc free_loc = {0};
    while (1) {
        uint32_t sector_start = (dir_cluster == 0) ? fat.root_start :
            cluster_to_sector(cluster);
        uint32_t sector_count = (dir_cluster == 0) ? fat.root_dir_sectors : fat.sectors_per_cluster;
        for (uint32_t s = 0; s < sector_count; ++s) {
            if (dir_read_sector(sector_start + s, sector) != 0) {
                return -1;
            }
            for (uint32_t e = 0; e < entries_per_sector; ++e) {
                if (dir_cluster == 0 && seen >= total_entries) {
                    if (allow_free && saw_free) {
                        if (loc) {
                            *loc = free_loc;
                        }
                        return 1;
                    }
                    return -1;
                }
                uint32_t idx = e * 32;
                uint8_t first = sector[idx];
                if (dir_cluster == 0) {
                    ++seen;
                }
                if (first == 0x00) {
                    if (allow_free && !saw_free) {
                        free_loc.sector = sector_start + s;
                        free_loc.offset = (uint16_t)idx;
                        saw_free = 1;
                    }
                    if (allow_free && saw_free) {
                        if (loc) {
                            *loc = free_loc;
                        }
                        return 1;
                    }
                    return -1;
                }
                if (first == 0xE5) {
                    if (allow_free && !saw_free) {
                        free_loc.sector = sector_start + s;
                        free_loc.offset = (uint16_t)idx;
                        saw_free = 1;
                    }
                    continue;
                }
                if (sector[idx + 11] == 0x0F) {
                    continue;
                }
                int is_dir = (sector[idx + 11] & 0x10) != 0;
                if (want_dir == 1 && !is_dir) {
                    continue;
                }
                if (want_dir == 0 && is_dir) {
                    continue;
                }
                int match = 1;
                for (int i = 0; i < 11; ++i) {
                    if ((char)sector[idx + i] != name[i]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    if (out) {
                        out->start_cluster = read_u16(&sector[idx + 26]);
                        out->size = read_u32(&sector[idx + 28]);
                        out->dir_sector = sector_start + s;
                        out->dir_offset = (uint16_t)idx;
                        out->is_dir = is_dir ? 1 : 0;
                        out->attr = sector[idx + 11];
                    }
                    if (loc) {
                        loc->sector = sector_start + s;
                        loc->offset = (uint16_t)idx;
                    }
                    return 0;
                }
            }
        }
        if (dir_cluster == 0) {
            break;
        }
        uint16_t next = fat_next_cluster(cluster);
        if (next == 0 || is_end_cluster(next)) {
            break;
        }
        cluster = next;
        seen = 0;
    }
    if (allow_free && saw_free) {
        if (loc) {
            *loc = free_loc;
        }
        return 1;
    }
    return -1;
}

static int dir_write_entry(const struct dir_loc *loc, const uint8_t *entry)
{
    if (!loc || !entry) {
        return -1;
    }
    uint8_t sector[512];
    if (dir_read_sector(loc->sector, sector) != 0) {
        return -1;
    }
    for (uint32_t i = 0; i < 32; ++i) {
        sector[loc->offset + i] = entry[i];
    }
    return dir_write_sector(loc->sector, sector);
}

static int fat_traverse_path(const char *path, int create, int want_dir_last, struct fat_file *out)
{
    if (!path || !out) {
        return -1;
    }
    uint16_t dir_cluster = 0;
    const char *p = path;
    if (*p == '/') {
        ++p;
    }
    char name[11];
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') {
            ++p;
        }
        char comp[16];
        uint64_t len = (uint64_t)(p - start);
        if (len == 0 || len >= sizeof(comp)) {
            return -1;
        }
        for (uint64_t i = 0; i < len; ++i) {
            comp[i] = start[i];
        }
        comp[len] = '\0';
        int last = (*p == '\0');
        if (build_short_name(comp, name) != 0) {
            return -1;
        }
        struct fat_file found = {0};
        struct dir_loc loc = {0};
        int want_dir = last ? want_dir_last : 1;
        int allow_free = last && create && want_dir_last == 0;
        int res = dir_find_entry(dir_cluster, name, want_dir, &found, &loc, allow_free);
        if (res == 0) {
            if (!last) {
                dir_cluster = (uint16_t)found.start_cluster;
            } else {
                *out = found;
                return 0;
            }
        } else if (res == 1 && allow_free) {
            uint8_t entry[32];
            for (int i = 0; i < 32; ++i) {
                entry[i] = 0;
            }
            for (int i = 0; i < 11; ++i) {
                entry[i] = (uint8_t)name[i];
            }
            entry[11] = 0x20;
            if (dir_write_entry(&loc, entry) != 0) {
                return -1;
            }
            out->start_cluster = 0;
            out->size = 0;
            out->dir_sector = loc.sector;
            out->dir_offset = loc.offset;
            out->is_dir = 0;
            out->attr = 0x20;
            return 0;
        } else {
            return -1;
        }
        if (*p == '/') {
            ++p;
        }
    }
    return -1;
}

int fat_init(struct block_device *dev)
{
    fat.ready = 0;
    if (!dev) {
        return -1;
    }
    fat.dev = dev;
    uint8_t buf[512];
    if (block_read(dev, 0, 1, buf) != 0) {
        return -1;
    }
    fat.bytes_per_sector = read_u16(&buf[11]);
    fat.sectors_per_cluster = buf[13];
    fat.reserved_sectors = read_u16(&buf[14]);
    fat.fat_count = buf[16];
    fat.root_entries = read_u16(&buf[17]);
    uint16_t total16 = read_u16(&buf[19]);
    fat.total_sectors = total16 ? total16 : read_u32(&buf[32]);
    fat.fat_size = read_u16(&buf[22]);
    if (fat.bytes_per_sector != 512 || fat.sectors_per_cluster == 0 || fat.fat_size == 0) {
        return -1;
    }
    fat.root_dir_sectors = ((uint32_t)fat.root_entries * 32 + (fat.bytes_per_sector - 1)) / fat.bytes_per_sector;
    fat.fat_start = fat.reserved_sectors;
    fat.root_start = fat.fat_start + (uint32_t)fat.fat_count * fat.fat_size;
    fat.data_start = fat.root_start + fat.root_dir_sectors;
    uint32_t data_sectors = fat.total_sectors - fat.data_start;
    fat.cluster_count = data_sectors / fat.sectors_per_cluster;
    if (fat.cluster_count < FAT16_MIN_CLUSTERS) {
        return -1;
    }
    fat.ready = 1;
    return 0;
}

int fat_open(const char *path, struct fat_file *out)
{
    if (!fat.ready || !path || !out) {
        return -1;
    }
    return fat_traverse_path(path, 0, 0, out);
}

int fat_open_dir(const char *path, struct fat_file *out)
{
    if (!fat.ready || !path || !out) {
        return -1;
    }
    return fat_traverse_path(path, 0, 1, out);
}

int fat_create(const char *path, struct fat_file *out)
{
    if (!fat.ready || !path || !out) {
        return -1;
    }
    return fat_traverse_path(path, 1, 0, out);
}

int64_t fat_read(struct fat_file *file, uint64_t *offset, void *buf, uint64_t len)
{
    if (!fat.ready || !file || !offset || !buf) {
        return -1;
    }
    if (file->start_cluster == 0 || *offset >= file->size || len == 0) {
        return 0;
    }
    uint64_t remaining = file->size - *offset;
    if (len > remaining) {
        len = remaining;
    }
    uint32_t cluster = (uint16_t)file->start_cluster;
    uint64_t cluster_size = (uint64_t)fat.sectors_per_cluster * fat.bytes_per_sector;
    uint64_t skip = *offset;
    while (skip >= cluster_size && cluster >= 2) {
        uint16_t next = fat_next_cluster((uint16_t)cluster);
        if (next == 0 || is_end_cluster(next)) {
            return 0;
        }
        cluster = next;
        skip -= cluster_size;
    }
    uint8_t sector_buf[512];
    uint64_t copied = 0;
    while (len > 0 && cluster >= 2) {
        uint32_t base_sector = cluster_to_sector((uint16_t)cluster);
        uint64_t cluster_off = skip;
        while (cluster_off < cluster_size && len > 0) {
            uint32_t sector = base_sector + (uint32_t)(cluster_off / fat.bytes_per_sector);
            uint32_t off = (uint32_t)(cluster_off % fat.bytes_per_sector);
            if (read_sector(sector, sector_buf) != 0) {
                return -1;
            }
            uint64_t chunk = fat.bytes_per_sector - off;
            if (chunk > len) {
                chunk = len;
            }
            uint8_t *dst = (uint8_t *)buf;
            for (uint64_t i = 0; i < chunk; ++i) {
                dst[copied + i] = sector_buf[off + i];
            }
            copied += chunk;
            len -= chunk;
            cluster_off += chunk;
            if (len == 0) {
                break;
            }
        }
        skip = 0;
        if (len == 0) {
            break;
        }
        uint16_t next = fat_next_cluster((uint16_t)cluster);
        if (next == 0 || is_end_cluster(next)) {
            break;
        }
        cluster = next;
    }
    *offset += copied;
    return (int64_t)copied;
}

int64_t fat_write(struct fat_file *file, uint64_t *offset, const void *buf, uint64_t len)
{
    if (!fat.ready || !file || !offset || !buf) {
        return -1;
    }
    if (file->is_dir) {
        return -1;
    }
    if (file->attr & 0x01) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    uint64_t end = *offset + len;
    if (end < *offset) {
        return -1;
    }
    if (file->start_cluster == 0) {
        uint16_t first = fat_alloc_cluster();
        if (!first) {
            return -1;
        }
        file->start_cluster = first;
        uint8_t sector[512];
        if (dir_read_sector(file->dir_sector, sector) != 0) {
            return -1;
        }
        sector[file->dir_offset + 26] = (uint8_t)(first & 0xFF);
        sector[file->dir_offset + 27] = (uint8_t)(first >> 8);
        if (dir_write_sector(file->dir_sector, sector) != 0) {
            return -1;
        }
    }
    uint16_t cluster = (uint16_t)file->start_cluster;
    uint64_t cluster_size = (uint64_t)fat.sectors_per_cluster * fat.bytes_per_sector;
    uint64_t skip = *offset;
    while (skip >= cluster_size) {
        uint16_t next = fat_next_cluster(cluster);
        if (next == 0 || is_end_cluster(next)) {
            uint16_t new_cluster = fat_alloc_cluster();
            if (!new_cluster) {
                return -1;
            }
            if (fat_set_cluster(cluster, new_cluster) != 0) {
                return -1;
            }
            cluster = new_cluster;
        } else {
            cluster = next;
        }
        skip -= cluster_size;
    }
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t sector_buf[512];
    uint64_t written = 0;
    while (len > 0) {
        uint32_t base_sector = cluster_to_sector(cluster);
        uint64_t cluster_off = skip;
        while (cluster_off < cluster_size && len > 0) {
            uint32_t sector = base_sector + (uint32_t)(cluster_off / fat.bytes_per_sector);
            uint32_t off = (uint32_t)(cluster_off % fat.bytes_per_sector);
            if (dir_read_sector(sector, sector_buf) != 0) {
                return -1;
            }
            uint64_t chunk = fat.bytes_per_sector - off;
            if (chunk > len) {
                chunk = len;
            }
            for (uint64_t i = 0; i < chunk; ++i) {
                sector_buf[off + i] = src[written + i];
            }
            if (dir_write_sector(sector, sector_buf) != 0) {
                return -1;
            }
            written += chunk;
            len -= chunk;
            cluster_off += chunk;
        }
        skip = 0;
        if (len == 0) {
            break;
        }
        uint16_t next = fat_next_cluster(cluster);
        if (next == 0 || is_end_cluster(next)) {
            uint16_t new_cluster = fat_alloc_cluster();
            if (!new_cluster) {
                return (int64_t)written;
            }
            if (fat_set_cluster(cluster, new_cluster) != 0) {
                return (int64_t)written;
            }
            cluster = new_cluster;
        } else {
            cluster = next;
        }
    }
    if (end > file->size) {
        file->size = (uint32_t)end;
        uint8_t sector[512];
        if (dir_read_sector(file->dir_sector, sector) == 0) {
            uint32_t off = file->dir_offset + 28;
            sector[off + 0] = (uint8_t)(file->size & 0xFF);
            sector[off + 1] = (uint8_t)((file->size >> 8) & 0xFF);
            sector[off + 2] = (uint8_t)((file->size >> 16) & 0xFF);
            sector[off + 3] = (uint8_t)((file->size >> 24) & 0xFF);
            (void)dir_write_sector(file->dir_sector, sector);
        }
    }
    *offset += written;
    return (int64_t)written;
}

static void dir_entry_init(uint8_t *entry, const char name[11], uint8_t attr, uint16_t cluster, uint32_t size)
{
    for (int i = 0; i < 32; ++i) {
        entry[i] = 0;
    }
    for (int i = 0; i < 11; ++i) {
        entry[i] = (uint8_t)name[i];
    }
    entry[11] = attr;
    entry[26] = (uint8_t)(cluster & 0xFF);
    entry[27] = (uint8_t)(cluster >> 8);
    entry[28] = (uint8_t)(size & 0xFF);
    entry[29] = (uint8_t)((size >> 8) & 0xFF);
    entry[30] = (uint8_t)((size >> 16) & 0xFF);
    entry[31] = (uint8_t)((size >> 24) & 0xFF);
}

static int zero_cluster(uint16_t cluster)
{
    uint8_t sector[512];
    for (uint32_t i = 0; i < fat.bytes_per_sector; ++i) {
        sector[i] = 0;
    }
    uint32_t start = cluster_to_sector(cluster);
    for (uint32_t s = 0; s < fat.sectors_per_cluster; ++s) {
        if (dir_write_sector(start + s, sector) != 0) {
            return -1;
        }
    }
    return 0;
}

static int init_dir_cluster(uint16_t cluster, uint16_t parent_cluster)
{
    if (zero_cluster(cluster) != 0) {
        return -1;
    }
    uint8_t sector[512];
    if (dir_read_sector(cluster_to_sector(cluster), sector) != 0) {
        return -1;
    }
    char dot[11];
    char dotdot[11];
    for (int i = 0; i < 11; ++i) {
        dot[i] = ' ';
        dotdot[i] = ' ';
    }
    dot[0] = '.';
    dotdot[0] = '.';
    dotdot[1] = '.';
    dir_entry_init(&sector[0], dot, 0x10, cluster, 0);
    dir_entry_init(&sector[32], dotdot, 0x10, parent_cluster, 0);
    return dir_write_sector(cluster_to_sector(cluster), sector);
}

static int fat_find_parent(const char *path, uint16_t *out_dir_cluster, char name[11])
{
    if (!path || !out_dir_cluster || !name) {
        return -1;
    }
    const char *p = path;
    while (*p == '/') {
        ++p;
    }
    if (*p == '\0') {
        return -1;
    }
    uint16_t dir_cluster = 0;
    while (*p) {
        while (*p == '/') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        const char *start = p;
        while (*p && *p != '/') {
            ++p;
        }
        uint64_t len = (uint64_t)(p - start);
        if (len == 0 || len >= 16) {
            return -1;
        }
        char comp[16];
        for (uint64_t i = 0; i < len; ++i) {
            comp[i] = start[i];
        }
        comp[len] = '\0';
        const char *next = p;
        while (*next == '/') {
            ++next;
        }
        int last = (*next == '\0');
        char tmp_name[11];
        if (build_short_name(comp, tmp_name) != 0) {
            return -1;
        }
        if (last) {
            for (int i = 0; i < 11; ++i) {
                name[i] = tmp_name[i];
            }
            *out_dir_cluster = dir_cluster;
            return 0;
        }
        struct fat_file found = {0};
        if (dir_find_entry(dir_cluster, tmp_name, 1, &found, NULL, 0) != 0) {
            return -1;
        }
        dir_cluster = (uint16_t)found.start_cluster;
        p = next;
    }
    return -1;
}

int fat_mkdir(const char *path)
{
    if (!fat.ready || !path) {
        return -1;
    }
    uint16_t dir_cluster = 0;
    char name[11];
    if (fat_find_parent(path, &dir_cluster, name) != 0) {
        return -1;
    }
    struct fat_file found = {0};
    struct dir_loc loc = {0};
    int res = dir_find_entry(dir_cluster, name, -1, &found, &loc, 1);
    if (res == 0) {
        return found.is_dir ? 0 : -1;
    }
    if (res != 1) {
        return -1;
    }
    uint16_t cluster = fat_alloc_cluster();
    if (!cluster) {
        return -1;
    }
    uint8_t entry[32];
    dir_entry_init(entry, name, 0x10, cluster, 0);
    if (dir_write_entry(&loc, entry) != 0) {
        return -1;
    }
    if (init_dir_cluster(cluster, dir_cluster) != 0) {
        return -1;
    }
    return 0;
}

static uint64_t append_str(char *buf, uint64_t len, uint64_t written, const char *s)
{
    for (uint64_t i = 0; s[i] != '\0'; ++i) {
        if (written + 1 >= len) {
            return written;
        }
        buf[written++] = s[i];
    }
    return written;
}

static void format_name(const uint8_t *ent, char *out, uint64_t out_len)
{
    uint64_t idx = 0;
    for (int i = 0; i < 8 && idx + 1 < out_len; ++i) {
        if (ent[i] == ' ') {
            break;
        }
        out[idx++] = (char)ent[i];
    }
    int has_ext = 0;
    for (int i = 8; i < 11; ++i) {
        if (ent[i] != ' ') {
            has_ext = 1;
            break;
        }
    }
    if (has_ext && idx + 1 < out_len) {
        out[idx++] = '.';
        for (int i = 8; i < 11 && idx + 1 < out_len; ++i) {
            if (ent[i] == ' ') {
                break;
            }
            out[idx++] = (char)ent[i];
        }
    }
    out[idx] = '\0';
}

static uint64_t list_dir(uint16_t dir_cluster, const char *prefix, char *buf, uint64_t len)
{
    if (!buf || len == 0 || !prefix) {
        return 0;
    }
    uint8_t sector[512];
    uint32_t entries_per_sector = fat.bytes_per_sector / 32;
    uint32_t total_entries = (dir_cluster == 0) ? (uint32_t)fat.root_entries : 0xFFFFFFFFu;
    uint32_t seen = 0;
    uint64_t written = 0;
    char name[16];
    uint16_t cluster = dir_cluster;
    while (1) {
        uint32_t sector_start = (dir_cluster == 0) ? fat.root_start : cluster_to_sector(cluster);
        uint32_t sector_count = (dir_cluster == 0) ? fat.root_dir_sectors : fat.sectors_per_cluster;
        for (uint32_t s = 0; s < sector_count; ++s) {
            if (read_sector(sector_start + s, sector) != 0) {
                return written;
            }
            for (uint32_t e = 0; e < entries_per_sector; ++e) {
                if (dir_cluster == 0 && seen >= total_entries) {
                    return written;
                }
                const uint8_t *ent = &sector[e * 32];
                if (dir_cluster == 0) {
                    ++seen;
                }
                if (ent[0] == 0x00) {
                    return written;
                }
                if (ent[0] == 0xE5 || ent[11] == 0x0F || (ent[11] & 0x08)) {
                    continue;
                }
                if (ent[0] == '.' && (ent[1] == ' ' || ent[1] == '.')) {
                    continue;
                }
                if (ent[11] & 0x10) {
                    format_name(ent, name, sizeof(name));
                    written = append_str(buf, len, written, prefix);
                    written = append_str(buf, len, written, name);
                    if (written + 2 >= len) {
                        return written;
                    }
                    buf[written++] = '/';
                    buf[written++] = '\n';
                    continue;
                }
                format_name(ent, name, sizeof(name));
                written = append_str(buf, len, written, prefix);
                written = append_str(buf, len, written, name);
                if (written + 1 >= len) {
                    return written;
                }
                buf[written++] = '\n';
            }
        }
        if (dir_cluster == 0) {
            break;
        }
        uint16_t next = fat_next_cluster(cluster);
        if (next == 0 || is_end_cluster(next)) {
            break;
        }
        cluster = next;
    }
    return written;
}

static int normalize_path(const char *path, char *out, uint64_t out_len)
{
    if (!out || out_len == 0) {
        return -1;
    }
    if (!path) {
        out[0] = '\0';
        return 0;
    }
    const char *p = path;
    while (*p == '/') {
        ++p;
    }
    uint64_t len = 0;
    while (*p && len + 1 < out_len) {
        out[len++] = *p++;
    }
    out[len] = '\0';
    while (len > 0 && out[len - 1] == '/') {
        out[--len] = '\0';
    }
    if (*p) {
        return -1;
    }
    return 0;
}

uint64_t fat_list_dir(const char *path, char *buf, uint64_t len)
{
    if (!fat.ready || !buf || len == 0) {
        return 0;
    }
    char trimmed[64];
    if (normalize_path(path, trimmed, sizeof(trimmed)) != 0) {
        return 0;
    }
    uint16_t dir_cluster = 0;
    if (trimmed[0]) {
        struct fat_file dir = {0};
        if (fat_open_dir(trimmed, &dir) != 0 || !dir.is_dir) {
            return 0;
        }
        dir_cluster = (uint16_t)dir.start_cluster;
    }
    char prefix[96];
    uint64_t pos = 0;
    const char base[] = "/disk/";
    for (uint64_t i = 0; base[i] != '\0' && pos + 1 < sizeof(prefix); ++i) {
        prefix[pos++] = base[i];
    }
    if (trimmed[0]) {
        for (uint64_t i = 0; trimmed[i] != '\0' && pos + 1 < sizeof(prefix); ++i) {
            prefix[pos++] = trimmed[i];
        }
        if (pos + 1 < sizeof(prefix)) {
            prefix[pos++] = '/';
        }
    }
    prefix[pos] = '\0';
    return list_dir(dir_cluster, prefix, buf, len);
}

uint64_t fat_list(char *buf, uint64_t len)
{
    return fat_list_dir(NULL, buf, len);
}
