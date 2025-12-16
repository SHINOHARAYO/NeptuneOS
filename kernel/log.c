#include "kernel/log.h"
#include "kernel/console.h"
#include "kernel/serial.h"

#include <stdint.h>

static enum log_level current_level = LOG_LEVEL_INFO;

static void write_prefix(enum log_level level)
{
    switch (level) {
    case LOG_LEVEL_DEBUG:
        console_set_color(0x2F); /* white on green */
        console_write("[DEBUG] ");
        serial_write("[DEBUG] ");
        break;
    case LOG_LEVEL_INFO:
        console_set_color(0x1F); /* white on blue */
        console_write("[INFO ] ");
        serial_write("[INFO ] ");
        break;
    case LOG_LEVEL_WARN:
        console_set_color(0x1E); /* yellow on blue */
        console_write("[WARN ] ");
        serial_write("[WARN ] ");
        break;
    case LOG_LEVEL_ERROR:
    default:
        console_set_color(0x4F); /* white on red */
        console_write("[ERROR] ");
        serial_write("[ERROR] ");
        break;
    }
}

static void log_emit(enum log_level level, const char *msg)
{
    if (level < current_level) {
        return;
    }

    write_prefix(level);
    console_write(msg);
    console_write("\n");
    serial_write(msg);
    serial_write("\n");
}

void log_init(void)
{
    serial_init();
    current_level = LOG_LEVEL_INFO;
}

void log_set_level(enum log_level level)
{
    current_level = level;
}

void log_debug(const char *msg) { log_emit(LOG_LEVEL_DEBUG, msg); }
void log_info(const char *msg) { log_emit(LOG_LEVEL_INFO, msg); }
void log_warn(const char *msg) { log_emit(LOG_LEVEL_WARN, msg); }
void log_error(const char *msg) { log_emit(LOG_LEVEL_ERROR, msg); }
