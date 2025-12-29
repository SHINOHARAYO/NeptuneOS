#pragma once

#include <stdint.h>

#define USER_BASE 0x0000000000400000ULL
#define USER_STACK_TOP 0x0000007FFFFFF000ULL
#define USER_STACK_MAX_PAGES 4

struct user_space {
    uint64_t pml4_phys;
    uint64_t entry;
    uint64_t stack_top;
    uint64_t stack_bottom;
    uint64_t stack_pages;
    uint64_t stack_phys[USER_STACK_MAX_PAGES];
};

int user_space_init(struct user_space *space);
int user_space_map_page(struct user_space *space, uint64_t virt, uint64_t phys, uint64_t flags);
int user_space_map_stack(struct user_space *space, uint64_t pages);
int user_stack_setup(struct user_space *space, const char *const *argv, const char *const *envp, uint64_t *out_sp);
void user_enter(uint64_t entry, uint64_t user_stack, uint64_t pml4_phys) __attribute__((noreturn));
void user_smoke_thread(void *arg);
void user_exit_trampoline(void) __attribute__((noreturn));
void user_exit_handler(void) __attribute__((noreturn));
