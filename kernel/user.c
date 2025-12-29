#include "kernel/user.h"
#include "kernel/elf.h"
#include "kernel/gdt.h"
#include "kernel/log.h"
#include "kernel/mem.h"
#include "kernel/mmu.h"
#include "kernel/sched.h"

#include <stddef.h>
#include <stdint.h>

#define USER_STACK_PAGE_SIZE 4096

#define USER_ELF_SIZE 0x200
#define USER_ELF_CODE_OFFSET 0x80
#define USER_ELF_MSG_OFFSET 0x180
#define USER_ELF_MSG_LEN 20

static const uint8_t user_elf[USER_ELF_SIZE] = {
    0x7F, 'E', 'L', 'F', 0x02, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x3E, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x38, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    [USER_ELF_CODE_OFFSET] = 0x48, 0xC7, 0xC0, 0x04, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,
    0x48, 0xBE, 0x80, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0xC2, 0x14, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xEB, 0xFE,
    [USER_ELF_MSG_OFFSET] = 'H', 'e', 'l', 'l', 'o', ' ', 'f', 'r', 'o', 'm',
    ' ', 'u', 's', 'e', 'r', ' ', 'E', 'L', 'F', '\n',
};

void user_exit_handler(void)
{
    log_info("User-mode exited to kernel");
    __asm__ volatile("sti");
    for (;;) {
        sched_yield();
    }
}

int user_space_init(struct user_space *space)
{
    if (!space) {
        return -1;
    }

    uint64_t pml4 = mmu_create_user_pml4();
    if (!pml4) {
        return -1;
    }

    space->pml4_phys = pml4;
    space->entry = USER_BASE;
    space->stack_top = USER_STACK_TOP;
    return 0;
}

int user_space_map_page(struct user_space *space, uint64_t virt, uint64_t phys, uint64_t flags)
{
    if (!space || !space->pml4_phys) {
        return -1;
    }
    return mmu_map_page_in(space->pml4_phys, virt, phys, flags | MMU_FLAG_USER);
}

int user_space_map_stack(struct user_space *space, uint64_t pages)
{
    if (!space || !space->pml4_phys || pages == 0) {
        return -1;
    }

    for (uint64_t i = 0; i < pages; ++i) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            return -1;
        }
        uint64_t virt = USER_STACK_TOP - ((i + 1) * USER_STACK_PAGE_SIZE);
        if (user_space_map_page(space, virt, phys, MMU_FLAG_WRITE | MMU_FLAG_NOEXEC) != 0) {
            return -1;
        }
    }
    space->stack_top = USER_STACK_TOP;
    return 0;
}

void user_enter(uint64_t entry, uint64_t user_stack, uint64_t pml4_phys)
{
    uint64_t rsp0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp0));
    gdt_set_kernel_stack(rsp0);

    if (pml4_phys) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
    }

    __asm__ volatile(
        "pushq %[user_ss]\n"
        "pushq %[user_rsp]\n"
        "pushfq\n"
        "popq %%rax\n"
        "orq $0x200, %%rax\n"
        "pushq %%rax\n"
        "pushq %[user_cs]\n"
        "pushq %[user_rip]\n"
        "iretq\n"
        :
        : [user_ss] "i"(GDT_USER_DATA | 0x3),
          [user_rsp] "r"(user_stack),
          [user_cs] "i"(GDT_USER_CODE | 0x3),
          [user_rip] "r"(entry)
        : "rax", "memory");

    __builtin_unreachable();
}

__attribute__((naked, noreturn)) void user_exit_trampoline(void)
{
    __asm__ volatile(
        "add $16, %rsp\n"
        "call user_exit_handler\n"
        "1: hlt\n"
        "jmp 1b\n"
    );
}

void user_smoke_thread(void *arg)
{
    (void)arg;
    struct user_space space;
    if (user_space_init(&space) != 0) {
        log_error("user_smoke: init failed");
        return;
    }

    if (elf_load_user(user_elf, sizeof(user_elf), &space) != 0) {
        log_error("user_smoke: ELF load failed");
        return;
    }

    if (user_space_map_stack(&space, 1) != 0) {
        log_error("user_smoke: map stack failed");
        return;
    }

    log_info("Entering user-mode ELF");
    user_enter(space.entry, space.stack_top, space.pml4_phys);
}
