/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "framebuffer_backend.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <time.h>
#include <unistd.h>

namespace {

volatile sig_atomic_t interrupted = 0;

void handle_signal(int) { interrupted = 1; }

double monotonic_seconds() {
    timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        throw std::runtime_error("clock_gettime failed");
    return static_cast<double>(value.tv_sec) +
           static_cast<double>(value.tv_nsec) / 1000000000.0;
}

unsigned parse_seconds(const char *text) {
    errno = 0;
    char *end = 0;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (errno || !end || *end != '\0' || value == 0UL || value > 86400UL)
        throw std::runtime_error("seconds must be from 1 through 86400");
    return static_cast<unsigned>(value);
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 4 || std::strcmp(argv[1], "--mode") != 0 ||
        (std::strcmp(argv[2], "map") != 0 &&
         std::strcmp(argv[2], "write") != 0)) {
        std::cerr << "usage: " << argv[0]
                  << " --mode map|write SECONDS\n";
        return 2;
    }

    try {
        const bool write_pixels = std::strcmp(argv[2], "write") == 0;
        const unsigned seconds = parse_seconds(argv[3]);
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        std::signal(SIGHUP, handle_signal);

        koboy::FramebufferBackend framebuffer;
        framebuffer.open_device("/dev/fb0");
        framebuffer.configure_viewport(framebuffer.variable_info().xres,
                                       framebuffer.variable_info().yres);

        const double started = monotonic_seconds();
        const double deadline = started + seconds;
        unsigned iterations = 0U;
        while (!interrupted && monotonic_seconds() < deadline) {
            if (write_pixels) {
                const uint32_t shade = (iterations & 1U)
                    ? 0xFF555555U : 0xFFAAAAAAU;
                framebuffer.clear_screen(shade);
                ++iterations;
                usleep(90909);
            } else {
                sleep(1);
            }
        }

        std::cout << "mode=" << (write_pixels ? "write" : "map") << '\n'
                  << "elapsed_seconds=" << monotonic_seconds() - started << '\n'
                  << "write_iterations=" << iterations << '\n'
                  << "result=" << (interrupted ? "interrupted" : "pass")
                  << '\n';
        return interrupted ? 130 : 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
