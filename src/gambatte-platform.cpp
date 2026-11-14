/*
 * Minimal standalone hooks required by the libretro-maintained Gambatte core.
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <libretro.h>

#include <cstdarg>
#include <cstdio>

extern "C" {

static retro_log_printf_t log_callback;

void gambatte_log_set_cb(retro_log_printf_t callback) {
    log_callback = callback;
}

void gambatte_log(enum retro_log_level level, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    if (log_callback) {
        char message[512];
        std::vsnprintf(message, sizeof(message), format, arguments);
        log_callback(level, "%s", message);
    } else {
        std::vfprintf(level == RETRO_LOG_ERROR ? stderr : stdout,
                      format, arguments);
    }
    va_end(arguments);
}

} // extern "C"

void cartridge_set_rumble(unsigned) {}
