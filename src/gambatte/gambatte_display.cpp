/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "frame_scaler.h"
#include "framebuffer_backend.h"
#include "kobo_session.h"
#include "touch_input.h"

#include <gambatte.h>

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <vector>

namespace {

const unsigned kSamplesPerCall = 2064U;
const unsigned kSoundBufferSamples = kSamplesPerCall + 2064U;
const double kGameBoyFrameRate = 59.727500569606;
const char *const kKoBoyRoot = "/mnt/onboard/KoBoy";
const char *const kKoBoyRomDirectory = "/mnt/onboard/KoBoy/Roms";
const char *const kKoBoyStateDirectory = "/mnt/onboard/KoBoy/States";
volatile sig_atomic_t interrupted = 0;

void handle_signal(int) { interrupted = 1; }

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
    if (errno || !end || *end != '\0' || parsed == 0UL || parsed > UINT_MAX)
        throw std::runtime_error(std::string("invalid value for ") + option);
    return static_cast<unsigned>(parsed);
}

std::vector<unsigned char> read_file(const char *path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error(std::string("cannot open ROM: ") + path);
    const std::ifstream::pos_type end = input.tellg();
    if (end <= 0 || end > static_cast<std::ifstream::pos_type>(UINT_MAX))
        throw std::runtime_error("ROM has an invalid or unsupported size");
    std::vector<unsigned char> data(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char *>(&data[0]), data.size());
    if (!input)
        throw std::runtime_error("failed to read complete ROM");
    return data;
}

bool ensure_directory(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return true;
    std::cerr << "warning: cannot create " << path << ": "
              << std::strerror(errno) << '\n';
    return false;
}

std::string state_path(const char *rom_path, unsigned slot) {
    std::string name(rom_path);
    const std::string::size_type slash = name.rfind('/');
    if (slash != std::string::npos)
        name.erase(0, slash + 1U);
    for (std::string::size_type index = 0; index < name.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(name[index]);
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '-' || character == '_'))
            name[index] = '_';
    }
    return std::string(kKoBoyStateDirectory) + "/" + name + ".state" +
           static_cast<char>('0' + slot);
}

bool load_state(gambatte::GB &gb, const std::string &path) {
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input)
        return false;
    const std::ifstream::pos_type end = input.tellg();
    if (end <= 0)
        return false;
    std::vector<unsigned char> state(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char *>(&state[0]), state.size());
    if (!input || !gb.loadState(&state[0], state.size())) {
        std::cerr << "warning: rejected save state " << path << '\n';
        return false;
    }
    return true;
}

bool save_state(gambatte::GB &gb, const std::string &path) {
    if (!ensure_directory(kKoBoyRoot) ||
        !ensure_directory(kKoBoyStateDirectory))
        return false;
    const std::size_t size = gb.stateSize();
    if (!size)
        return false;
    std::vector<unsigned char> state(size);
    gb.saveState(&state[0]);
    const std::string temporary = path + ".tmp";
    std::ofstream output(temporary.c_str(),
                         std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(&state[0]), state.size());
    output.close();
    if (!output) {
        std::cerr << "warning: cannot write save state " << temporary << '\n';
        return false;
    }
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        std::cerr << "warning: cannot install save state " << path << ": "
                  << std::strerror(errno) << '\n';
        return false;
    }
    return true;
}

void run_frame(gambatte::GB &gb,
               std::vector<gambatte::video_pixel_t> &video,
               std::vector<gambatte::uint_least32_t> &sound) {
    for (;;) {
        unsigned samples = kSamplesPerCall;
        if (gb.runFor(&video[0], gambatte_kobo::kGameBoyWidth, &sound[0],
                      sound.size(), samples) >= 0)
            return;
    }
}

struct Options {
    const char *rom;
    unsigned rate;
    unsigned scale;
    unsigned seconds;
    unsigned cleanup_seconds;
    Options() : rom(0), rate(8), scale(5), seconds(30), cleanup_seconds(10) {}
};

Options parse_options(int argc, char **argv) {
    if (argc < 2)
        throw std::runtime_error("usage: kobo-gambatte-display ROM [--rate N] [--scale N] [--seconds N] [--cleanup-seconds N]");
    Options options;
    options.rom = argv[1];
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc)
            throw std::runtime_error("every option requires a value");
        if (std::strcmp(argv[i], "--rate") == 0)
            options.rate = parse_unsigned(argv[i + 1], "--rate");
        else if (std::strcmp(argv[i], "--scale") == 0)
            options.scale = parse_unsigned(argv[i + 1], "--scale");
        else if (std::strcmp(argv[i], "--seconds") == 0)
            options.seconds = parse_unsigned(argv[i + 1], "--seconds");
        else if (std::strcmp(argv[i], "--cleanup-seconds") == 0)
            options.cleanup_seconds = parse_unsigned(argv[i + 1], "--cleanup-seconds");
        else
            throw std::runtime_error(std::string("unknown option: ") + argv[i]);
    }
    if (options.rate > 30U)
        throw std::runtime_error("--rate must not exceed 30");
    if (options.scale > 6U)
        throw std::runtime_error("--scale must not exceed 6");
    return options;
}

void fill_rectangle(std::vector<uint32_t> &screen, unsigned width,
                    unsigned height, int left, int top, int right, int bottom,
                    uint32_t color) {
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > static_cast<int>(width)) right = width;
    if (bottom > static_cast<int>(height)) bottom = height;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            screen[static_cast<size_t>(y) * width + x] = color;
}

void fill_circle(std::vector<uint32_t> &screen, unsigned width,
                 unsigned height, int center_x, int center_y, int radius,
                 uint32_t color) {
    for (int y = -radius; y <= radius; ++y) {
        const int target_y = center_y + y;
        if (target_y < 0 || target_y >= static_cast<int>(height))
            continue;
        for (int x = -radius; x <= radius; ++x) {
            const int target_x = center_x + x;
            if (target_x >= 0 && target_x < static_cast<int>(width) &&
                x * x + y * y <= radius * radius)
                screen[static_cast<size_t>(target_y) * width + target_x] = color;
        }
    }
}

const unsigned char *glyph(char character) {
    static const unsigned char a[7] = {14, 17, 17, 31, 17, 17, 17};
    static const unsigned char b[7] = {30, 17, 17, 30, 17, 17, 30};
    static const unsigned char e[7] = {31, 16, 16, 30, 16, 16, 31};
    static const unsigned char l[7] = {16, 16, 16, 16, 16, 16, 31};
    static const unsigned char r[7] = {30, 17, 17, 30, 20, 18, 17};
    static const unsigned char s[7] = {15, 16, 16, 14, 1, 1, 30};
    static const unsigned char t[7] = {31, 4, 4, 4, 4, 4, 4};
    static const unsigned char c[7] = {15, 16, 16, 16, 16, 16, 15};
    switch (character) {
        case 'A': return a;
        case 'B': return b;
        case 'C': return c;
        case 'E': return e;
        case 'L': return l;
        case 'R': return r;
        case 'S': return s;
        case 'T': return t;
        default: return 0;
    }
}

void draw_label(std::vector<uint32_t> &screen, unsigned width,
                unsigned height, int center_x, int center_y,
                const char *label, int scale, uint32_t color) {
    const int length = std::strlen(label);
    const int total_width = (length * 6 - 1) * scale;
    int left = center_x - total_width / 2;
    for (int index = 0; index < length; ++index) {
        const unsigned char *rows = glyph(label[index]);
        if (!rows)
            continue;
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 5; ++x)
                if (rows[y] & (1U << (4 - x)))
                    fill_rectangle(screen, width, height,
                                   left + (index * 6 + x) * scale,
                                   center_y - 7 * scale / 2 + y * scale,
                                   left + (index * 6 + x + 1) * scale,
                                   center_y - 7 * scale / 2 + (y + 1) * scale,
                                   color);
    }
}

void draw_controls(std::vector<uint32_t> &screen, unsigned width,
                   unsigned height) {
    const uint32_t dark = 0xFF202020U;
    const uint32_t light = 0xFFFFFFFFU;
    const int dpad_x = width * 21 / 100;
    const int dpad_y = height * 82 / 100;
    const int dpad_extent = width * 16 / 100;
    const int dpad_half_width = width * 6 / 100;
    fill_rectangle(screen, width, height, dpad_x - dpad_half_width,
                   dpad_y - dpad_extent, dpad_x + dpad_half_width,
                   dpad_y + dpad_extent, dark);
    fill_rectangle(screen, width, height, dpad_x - dpad_extent,
                   dpad_y - dpad_half_width, dpad_x + dpad_extent,
                   dpad_y + dpad_half_width, dark);

    const int radius = width * 9 / 100;
    const int a_x = width * 85 / 100;
    const int a_y = height * 76 / 100;
    const int b_x = width * 70 / 100;
    const int b_y = height * 84 / 100;
    fill_circle(screen, width, height, a_x, a_y, radius, dark);
    fill_circle(screen, width, height, b_x, b_y, radius, dark);
    draw_label(screen, width, height, a_x, a_y, "A", 10, light);
    draw_label(screen, width, height, b_x, b_y, "B", 10, light);

    const int menu_y = height * 94 / 100;
    const int menu_half_width = width * 8 / 100;
    const int menu_half_height = height * 3 / 100;
    const int select_x = width * 43 / 100;
    const int start_x = width * 60 / 100;
    fill_rectangle(screen, width, height, select_x - menu_half_width,
                   menu_y - menu_half_height, select_x + menu_half_width,
                   menu_y + menu_half_height, dark);
    fill_rectangle(screen, width, height, start_x - menu_half_width,
                   menu_y - menu_half_height, start_x + menu_half_width,
                   menu_y + menu_half_height, dark);
    draw_label(screen, width, height, select_x, menu_y, "SELECT", 3, light);
    draw_label(screen, width, height, start_x, menu_y, "START", 3, light);
}

} // namespace

int main(int argc, char **argv) {
    try {
        const Options options = parse_options(argc, argv);
        const bool supervised = std::getenv("GAMBATTE_SUPERVISED") != 0;
        const std::vector<unsigned char> rom = read_file(options.rom);
        gambatte::GB gb;
        if (gb.load(&rom[0], rom.size()) != 0)
            throw std::runtime_error("Gambatte rejected the ROM image");
        const std::string automatic_state = state_path(options.rom, 0U);
        const bool automatic_state_loaded = load_state(gb, automatic_state);

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        std::signal(SIGHUP, handle_signal);

        gambatte_kobo::NickelSession nickel;
        gambatte_kobo::TouchInput input(1072U, 1448U);
        input.open_device();
        if (!supervised)
            nickel.suspend();
        gb.setInputGetter(&input);

        gambatte_kobo::FramebufferBackend framebuffer;
        framebuffer.open_device("/dev/fb0");
        framebuffer.configure_viewport(gambatte_kobo::kGameBoyWidth * options.scale,
                                       gambatte_kobo::kGameBoyHeight * options.scale);
        if (!supervised)
            framebuffer.save_screen();

        std::vector<gambatte::video_pixel_t> video(
            gambatte_kobo::kGameBoyWidth * gambatte_kobo::kGameBoyHeight);
        std::vector<gambatte::uint_least32_t> sound(kSoundBufferSamples);
        std::vector<uint32_t> rgb(video.size());
        std::vector<uint32_t> scaled;
        uint32_t marker = 1U;
        const unsigned screen_width = framebuffer.variable_info().xres;
        const unsigned screen_height = framebuffer.variable_info().yres;
        std::vector<uint32_t> screen(
            static_cast<size_t>(screen_width) * screen_height, 0xFFFFFFFFU);
        draw_controls(screen, screen_width, screen_height);
        framebuffer.draw_screen(screen);
        framebuffer.refresh_screen_gc16(marker++, true);

        std::cout << "scale=" << options.scale << '\n';
        std::cout << "touch_x_range=" << input.x_min() << ',' << input.x_max() << '\n';
        std::cout << "touch_y_range=" << input.y_min() << ',' << input.y_max() << '\n';

        const double display_interval = 1.0 / options.rate;
        const double started = monotonic_seconds();
        const double deadline = started + options.seconds;
        double next_display = started;
        double next_cleanup = started + options.cleanup_seconds;
        double emulation_accumulator = 0.0;
        unsigned emulated_frames = 0U;
        unsigned displayed_frames = 0U;
        unsigned cleanups = 0U;
        unsigned last_buttons = 0U;
        unsigned input_transitions = 0U;
        unsigned input_nonzero_polls = 0U;

        while (!interrupted && monotonic_seconds() < deadline) {
            emulation_accumulator += kGameBoyFrameRate / options.rate;
            unsigned frames_this_update = static_cast<unsigned>(emulation_accumulator);
            emulation_accumulator -= frames_this_update;
            while (frames_this_update-- != 0U) {
                input.advance_frame();
                const unsigned current_buttons = input();
                if (current_buttons != last_buttons) {
                    ++input_transitions;
                    last_buttons = current_buttons;
                }
                if (current_buttons != 0U)
                    ++input_nonzero_polls;
                run_frame(gb, video, sound);
                ++emulated_frames;
            }
            for (std::size_t i = 0; i < video.size(); ++i)
                rgb[i] = static_cast<uint32_t>(video[i]);
            gambatte_kobo::scale_rgb32_grayscale(rgb, options.scale, scaled);
            framebuffer.draw(scaled);

            const double now = monotonic_seconds();
            if (now >= next_cleanup) {
                framebuffer.refresh_gc16(marker++, true);
                next_cleanup += options.cleanup_seconds;
                ++cleanups;
            } else {
                framebuffer.refresh_a2(marker++);
            }
            ++displayed_frames;
            next_display += display_interval;
            sleep_until(next_display);
        }

        const double elapsed = monotonic_seconds() - started;
        const bool automatic_state_saved = save_state(gb, automatic_state);
        if (!supervised)
            framebuffer.restore(marker++);
        std::cout << "name=Ko Boy\n";
        std::cout << "rom_directory=" << kKoBoyRomDirectory << '\n';
        std::cout << "state0_loaded="
                  << (automatic_state_loaded ? "yes" : "no") << '\n';
        std::cout << "state0_saved="
                  << (automatic_state_saved ? "yes" : "no") << '\n';
        std::cout << "rom_bytes=" << rom.size() << '\n';
        std::cout << "elapsed_seconds=" << elapsed << '\n';
        std::cout << "emulated_frames=" << emulated_frames << '\n';
        std::cout << "displayed_frames=" << displayed_frames << '\n';
        std::cout << "gc16_cleanups=" << cleanups << '\n';
        std::cout << "input_transitions=" << input_transitions << '\n';
        std::cout << "input_nonzero_polls=" << input_nonzero_polls << '\n';
        std::cout << "result=" << (interrupted ? "interrupted" : "pass") << '\n';
        return interrupted ? 130 : 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
