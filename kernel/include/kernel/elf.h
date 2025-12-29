#pragma once

#include <stdint.h>

struct user_space;

int elf_load_user(const void *image, uint64_t size, struct user_space *space);
