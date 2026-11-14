/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "frame_scaler.h"
#include "framebuffer_backend.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <time.h>
#include <vector>

namespace {

volatile sig_atomic_t interrupted = 0;

void handle_signal(int) {
    interrupted = 1;
}

double monotonic_seconds() {
    timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        throw std::runtime_error("clock_gettime failed");
    return static_cast<double>(value.tv_sec) +
           static_cast<double>(value.tv_nsec) / 1000000000.0;
}

void sleep_until(double deadline) {
    timespec target;
    target.tv_sec = static_cast<time_t>(deadline);
    target.tv_nsec = static_cast<long>((deadline - target.tv_sec) * 1000000000.0);
    int result;
    while ((result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                     &target, 0)) != 0) {
        if (result == EINTR && interrupted)
            return;
        if (result != EINTR)
            throw std::runtime_error(std::string("clock_nanosleep: ") +
                                     std::strerror(result));
    }
}

unsigned parse_unsigned(const char *value, const char *option) {
    errno = 0;
    char *end = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno || !end || *end != '\0' || parsed == 0UL || parsed > 100000UL)
        throw std::runtime_error(std::string("invalid value for ") + option);
    return static_cast<unsigned>(parsed);
}

struct Options {
    unsigned rate;
    unsigned scale;
    unsigned seconds;
    unsigned cleanup_seconds;

    Options() : rate(8), scale(4), seconds(60), cleanup_seconds(10) {}
};

Options parse_options(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc)
            throw std::runtime_error("every option requires a value");
        if (std::strcmp(argv[i], "--rate") == 0)
            options.rate = parse_unsigned(argv[i + 1], "--rate");
        else if (std::strcmp(argv[i], "--scale") == 0)
            options.scale = parse_unsigned(argv[i + 1], "--scale");
        else if (std::strcmp(argv[i], "--seconds") == 0)
            options.seconds = parse_unsigned(argv[i + 1], "--seconds");
        else if (std::strcmp(argv[i], "--cleanup-seconds") == 0)
            options.cleanup_seconds = parse_unsigned(argv[i + 1],
                                                      "--cleanup-seconds");
        else
            throw std::runtime_error(std::string("unknown option: ") + argv[i]);
    }
    if (options.rate > 30U)
        throw std::runtime_error("--rate must not exceed 30");
    if (options.scale < 1U || options.scale > 6U)
        throw std::runtime_error("--scale must be from 1 through 6");
    return options;
}

} // namespace

int main(int argc, char **argv) {
    try {
        const Options options = parse_options(argc, argv);
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        std::signal(SIGHUP, handle_signal);

        gambatte_kobo::FramebufferBackend framebuffer;
        framebuffer.open_device("/dev/fb0");
        framebuffer.configure_viewport(gambatte_kobo::kGameBoyWidth * options.scale,
                                       gambatte_kobo::kGameBoyHeight * options.scale);
        framebuffer.save_viewport();

        const gambatte_kobo::Viewport &viewport = framebuffer.viewport();
        std::cout << "framebuffer=" << framebuffer.variable_info().xres << 'x'
                  << framebuffer.variable_info().yres << 'x'
                  << framebuffer.variable_info().bits_per_pixel << '\n';
        std::cout << "stride_bytes=" << framebuffer.fixed_info().line_length << '\n';
        std::cout << "viewport=" << viewport.x << ',' << viewport.y << ','
                  << viewport.width << ',' << viewport.height << '\n';
        std::cout << "target_rate=" << options.rate << '\n';

        uint32_t marker = 1U;
        framebuffer.clear_viewport(0xFFFFFFFFU);
        framebuffer.refresh_gc16(marker++, true);

        std::vector<uint8_t> source;
        std::vector<uint32_t> scaled;
        const double interval = 1.0 / static_cast<double>(options.rate);
        const double started = monotonic_seconds();
        const double deadline = started + options.seconds;
        double next_frame = started;
        double next_cleanup = started + options.cleanup_seconds;
        double total_submit = 0.0;
        double maximum_submit = 0.0;
        unsigned submitted = 0U;
        unsigned dropped = 0U;
        unsigned cleanups = 0U;

        while (!interrupted && monotonic_seconds() < deadline) {
            sleep_until(next_frame);
            if (interrupted || monotonic_seconds() >= deadline)
                break;

            gambatte_kobo::render_test_pattern(source, submitted + dropped);
            gambatte_kobo::scale_monochrome(source, options.scale, scaled);
            framebuffer.draw(scaled);

            const double now = monotonic_seconds();
            double submit_time;
            if (now >= next_cleanup) {
                submit_time = framebuffer.refresh_gc16(marker++, true);
                next_cleanup += options.cleanup_seconds;
                ++cleanups;
            } else {
                submit_time = framebuffer.refresh_a2(marker++);
            }
            total_submit += submit_time;
            if (submit_time > maximum_submit)
                maximum_submit = submit_time;
            ++submitted;

            next_frame += interval;
            const double after_submit = monotonic_seconds();
            while (next_frame + interval <= after_submit) {
                next_frame += interval;
                ++dropped;
            }
        }

        const double elapsed = monotonic_seconds() - started;
        framebuffer.restore(marker++);

        std::cout << "elapsed_seconds=" << elapsed << '\n';
        std::cout << "submitted_frames=" << submitted << '\n';
        std::cout << "dropped_frames=" << dropped << '\n';
        std::cout << "gc16_cleanups=" << cleanups << '\n';
        std::cout << "average_submit_ms="
                  << (submitted ? total_submit * 1000.0 / submitted : 0.0) << '\n';
        std::cout << "maximum_submit_ms=" << maximum_submit * 1000.0 << '\n';
        std::cout << "result=" << (interrupted ? "interrupted" : "pass") << '\n';
        return interrupted ? 130 : 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
