#include <kernel/fdt.h>
#include <kernel/log.h>
#include <kernel/console.h>
#include <stddef.h>

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int strprefix(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++; prefix++;
    }
    return 1;
}

static size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

uint64_t align_up(uint64_t val, uint64_t align) {
    return (val + align - 1) & ~(align - 1);
}

int fdt_get_memory(uint64_t fdt_addr, uint64_t *start, uint64_t *size) {
    struct fdt_header *hdr = (struct fdt_header *)fdt_addr;
    
    if (be32_to_cpu(hdr->magic) != FDT_MAGIC) {
        /* log_debug("Invalid FDT magic"); */
        return 0;
    }

    uint8_t *struct_ptr = (uint8_t *)fdt_addr + be32_to_cpu(hdr->off_dt_struct);
    uint8_t *strings_ptr = (uint8_t *)fdt_addr + be32_to_cpu(hdr->off_dt_strings);

    uint32_t token;
    int depth = 0;
    int in_memory_node = 0;

    /* TODO: Parse #address-cells and #size-cells from root node. 
       Defaulting to 2 and 2 (64-bit) for AArch64 QEMU. */
    int address_cells = 2;
    int size_cells = 2;

    while (1) {
        token = be32_to_cpu(*(uint32_t *)struct_ptr);
        struct_ptr += 4;

        if (token == FDT_END) {
            break;
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_BEGIN_NODE) {
            char *name = (char *)struct_ptr;
            struct_ptr += align_up(strlen(name) + 1, 4);
            
            if (depth == 1 && strprefix(name, "memory")) {
                in_memory_node = 1;
            } else {
                in_memory_node = 0;
            }
            depth++;
        } else if (token == FDT_END_NODE) {
            depth--;
            in_memory_node = 0;
        } else if (token == FDT_PROP) {
            uint32_t len = be32_to_cpu(*(uint32_t *)struct_ptr);
            struct_ptr += 4;
            uint32_t nameoff = be32_to_cpu(*(uint32_t *)struct_ptr);
            struct_ptr += 4;
            
            uint8_t *val = struct_ptr;
            struct_ptr += align_up(len, 4);

            const char *prop_name = (const char *)(strings_ptr + nameoff);

            if (in_memory_node && streq(prop_name, "reg")) {
                /* Found memory reg property */
                if (len < (uint32_t)((address_cells + size_cells) * 4)) {
                    continue; 
                }
                
                uint64_t addr = 0;
                uint64_t sz = 0;

                /* Read Address */
                if (address_cells == 2) {
                    addr = be64_to_cpu(*(uint64_t *)val);
                    val += 8;
                } else {
                    addr = be32_to_cpu(*(uint32_t *)val);
                    val += 4;
                }

                /* Read Size */
                if (size_cells == 2) {
                    sz = be64_to_cpu(*(uint64_t *)val);
                    val += 8;
                } else {
                    sz = be32_to_cpu(*(uint32_t *)val);
                    val += 4;
                }

                *start = addr;
                *size = sz;
                return 1;
            }
        }
    }

    return 0;
}
