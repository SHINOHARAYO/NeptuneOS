#include "kernel/elf.h"
#include "kernel/log.h"
#include "kernel/mem.h"
#include "kernel/mmu.h"
#include "kernel/user.h"
#include <arch/processor.h>

#include <stdint.h>

#include <stdint.h>
#include <stddef.h>

#define EI_NIDENT 16
#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6

#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define PT_LOAD 1

#define PF_X 0x1
#define PF_W 0x2

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

static uint64_t align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1);
}

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1) & ~(align - 1);
}

static int elf_check_header(const Elf64_Ehdr *eh, uint64_t size)
{
    if (!eh || size < sizeof(*eh)) {
        return -1;
    }
    if (eh->e_ident[EI_MAG0] != ELFMAG0 ||
        eh->e_ident[EI_MAG1] != ELFMAG1 ||
        eh->e_ident[EI_MAG2] != ELFMAG2 ||
        eh->e_ident[EI_MAG3] != ELFMAG3) {
        return -1;
    }
    if (eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_ident[EI_DATA] != ELFDATA2LSB ||
        eh->e_ident[EI_VERSION] != 1) {
        return -1;
    }
    if (eh->e_phentsize != sizeof(Elf64_Phdr) || eh->e_phnum == 0) {
        return -1;
    }
    if (eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr) > size) {
        return -1;
    }
    return 0;
}

int elf_load_user(const void *image, uint64_t size, struct user_space *space)
{
    if (!image || !space || size == 0) {
        return -1;
    }

    const uint8_t *bytes = (const uint8_t *)image;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)bytes;
    if (elf_check_header(eh, size) != 0) {
        log_error("ELF header invalid");
        return -1;
    }

    const Elf64_Phdr *phdrs = (const Elf64_Phdr *)(bytes + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; ++i) {
        const Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (ph->p_memsz == 0 || ph->p_filesz > ph->p_memsz) {
            return -1;
        }
        if (ph->p_offset + ph->p_filesz > size) {
            return -1;
        }
        if (ph->p_vaddr + ph->p_memsz < ph->p_vaddr) {
            return -1;
        }

        uint64_t seg_start = align_down(ph->p_vaddr, 4096);
        uint64_t seg_end = align_up(ph->p_vaddr + ph->p_memsz, 4096);
        uint64_t load_min = USER_BASE;
        if (load_min >= 0x1000) {
            load_min -= 0x1000;
        }
        if (seg_start < load_min || seg_end > USER_STACK_TOP) {
            return -1;
        }

        uint64_t flags = 0;
        if (ph->p_flags & PF_W) {
            flags |= MMU_FLAG_WRITE;
        }
        if (!(ph->p_flags & PF_X)) {
            flags |= MMU_FLAG_NOEXEC;
        }

        for (uint64_t v = seg_start; v < seg_end; v += 4096) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                return -1;
            }
            if (user_space_map_page(space, v, phys, flags) != 0) {
                return -1;
            }

            uint8_t *dst = (uint8_t *)phys_to_hhdm(phys);
            for (size_t j = 0; j < 4096; ++j) {
                dst[j] = 0;
            }

            uint64_t page_start = v;
            uint64_t page_end = v + 4096;
            uint64_t data_start = ph->p_vaddr;
            uint64_t data_end = ph->p_vaddr + ph->p_filesz;
            uint64_t copy_start = page_start > data_start ? page_start : data_start;
            uint64_t copy_end = page_end < data_end ? page_end : data_end;
            if (copy_start < copy_end) {
                uint64_t src_off = ph->p_offset + (copy_start - ph->p_vaddr);
                uint64_t dst_off = copy_start - page_start;
                uint64_t len = copy_end - copy_start;
                for (uint64_t k = 0; k < len; ++k) {
                    dst[dst_off + k] = bytes[src_off + k];
                }
                /* Sync I/D cache for this block if it's executable? Sync always to be safe. */
                arch_icode_sync(dst + dst_off, len);
            }
        }
    }

    space->entry = eh->e_entry;
    return 0;
}
