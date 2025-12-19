#pragma once

#include <stdint.h>

enum log_level {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
};

struct log_colors {
    uint8_t debug_color;
    uint8_t info_color;
    uint8_t warn_color;
    uint8_t error_color;
    uint8_t default_color;
};

void log_init(void);
void log_set_level(enum log_level level);
void log_set_colors(const struct log_colors *colors);
void log_debug(const char *msg);
void log_info(const char *msg);
void log_warn(const char *msg);
void log_error(const char *msg);

/* hex logging helpers */
void log_debug_hex(const char *label, uint64_t value);
void log_info_hex(const char *label, uint64_t value);
