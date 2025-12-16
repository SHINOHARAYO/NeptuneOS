#pragma once

enum log_level {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
};

void log_init(void);
void log_set_level(enum log_level level);
void log_debug(const char *msg);
void log_info(const char *msg);
void log_warn(const char *msg);
void log_error(const char *msg);
