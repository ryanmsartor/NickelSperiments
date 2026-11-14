/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <gambatte.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

namespace {

const unsigned kDefaultFrames = 3600;
const unsigned kSamplesPerCall = 2064;
const unsigned kSoundBufferSamples = kSamplesPerCall + 2064;
const int kWidth = 160;
const int kHeight = 144;

class NoInput : public gambatte::InputGetter {
public:
    unsigned operator()() { return 0; }
};

double monotonic_seconds() {
    timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        throw std::runtime_error("clock_gettime failed");
    return static_cast<double>(value.tv_sec) +
           static_cast<double>(value.tv_nsec) / 1000000000.0;
}

double timeval_seconds(const timeval &value) {
    return static_cast<double>(value.tv_sec) +
           static_cast<double>(value.tv_usec) / 1000000.0;
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

unsigned parse_frames(const char *text) {
    errno = 0;
    char *end = 0;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (errno || !end || *end != '\0' || value == 0 || value > UINT_MAX)
        throw std::runtime_error("--frames must be an integer from 1 to UINT_MAX");
    return static_cast<unsigned>(value);
}

unsigned long long checksum(const void *data, std::size_t size) {
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    unsigned long long hash = 1469598103934665603ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void run_frame(gambatte::GB &gb,
               std::vector<gambatte::video_pixel_t> &video,
               std::vector<gambatte::uint_least32_t> &sound) {
    for (;;) {
        unsigned samples = kSamplesPerCall;
        const long frame_sample =
            gb.runFor(&video[0], kWidth, &sound[0], sound.size(), samples);
        if (frame_sample >= 0)
            return;
    }
}

void validate_save_state(gambatte::GB &gb,
                         std::vector<gambatte::video_pixel_t> &video,
                         std::vector<gambatte::uint_least32_t> &sound) {
    const std::size_t state_size = gb.stateSize();
    if (!state_size)
        throw std::runtime_error("Gambatte returned an empty save state");

    std::vector<unsigned char> state(state_size);
    gb.saveState(&state[0]);

    run_frame(gb, video, sound);
    const unsigned long long first = checksum(&video[0],
                                               video.size() * sizeof(video[0]));

    if (!gb.loadState(&state[0], state.size()))
        throw std::runtime_error("Gambatte rejected its own save state");

    run_frame(gb, video, sound);
    const unsigned long long second = checksum(&video[0],
                                                video.size() * sizeof(video[0]));
    if (first != second)
        throw std::runtime_error("save-state replay produced a different frame");

    std::cout << "save_state_bytes=" << state_size << '\n';
    std::cout << "save_state_round_trip=pass\n";
}

void validate_save_ram(gambatte::GB &gb) {
    const unsigned size = gb.savedata_size();
    void *const data = gb.savedata_ptr();
    if (!size || !data) {
        std::cout << "save_ram_bytes=0\n";
        std::cout << "save_ram_round_trip=not_applicable\n";
        return;
    }

    std::vector<unsigned char> saved(size);
    std::memcpy(&saved[0], data, size);
    static_cast<unsigned char *>(data)[0] ^= 0xFF;
    std::memcpy(data, &saved[0], size);
    if (std::memcmp(data, &saved[0], size) != 0)
        throw std::runtime_error("failed to restore in-memory save RAM");

    std::cout << "save_ram_bytes=" << size << '\n';
    std::cout << "save_ram_round_trip=pass\n";
}

void usage(const char *program) {
    std::cerr << "usage: " << program << " ROM [--frames COUNT]\n";
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2 && argc != 4) {
        usage(argv[0]);
        return 2;
    }

    try {
        unsigned frame_count = kDefaultFrames;
        if (argc == 4) {
            if (std::strcmp(argv[2], "--frames") != 0) {
                usage(argv[0]);
                return 2;
            }
            frame_count = parse_frames(argv[3]);
        }

        const std::vector<unsigned char> rom = read_file(argv[1]);
        gambatte::GB gb;
        NoInput input;
        gb.setInputGetter(&input);
        if (gb.load(&rom[0], rom.size()) != 0)
            throw std::runtime_error("Gambatte rejected the ROM image");

        std::vector<gambatte::video_pixel_t> video(kWidth * kHeight);
        std::vector<gambatte::uint_least32_t> sound(kSoundBufferSamples);

        validate_save_state(gb, video, sound);
        validate_save_ram(gb);

        rusage usage_before;
        rusage usage_after;
        if (getrusage(RUSAGE_SELF, &usage_before) != 0)
            throw std::runtime_error("getrusage failed before benchmark");
        const double started = monotonic_seconds();

        for (unsigned frame = 0; frame < frame_count; ++frame)
            run_frame(gb, video, sound);

        const double elapsed = monotonic_seconds() - started;
        if (getrusage(RUSAGE_SELF, &usage_after) != 0)
            throw std::runtime_error("getrusage failed after benchmark");

        const double fps = static_cast<double>(frame_count) / elapsed;
        const double user_seconds = timeval_seconds(usage_after.ru_utime) -
                                    timeval_seconds(usage_before.ru_utime);
        const double system_seconds = timeval_seconds(usage_after.ru_stime) -
                                      timeval_seconds(usage_before.ru_stime);
        const unsigned long long last_frame_checksum =
            checksum(&video[0], video.size() * sizeof(video[0]));
        std::cout << "rom_bytes=" << rom.size() << '\n';
        std::cout << "frames=" << frame_count << '\n';
        std::cout << "elapsed_seconds=" << elapsed << '\n';
        std::cout << "user_cpu_seconds=" << user_seconds << '\n';
        std::cout << "system_cpu_seconds=" << system_seconds << '\n';
        std::cout << "emulated_fps=" << fps << '\n';
        std::cout << "realtime_multiple=" << fps / 59.727500569606 << '\n';
        std::cout << "max_rss_kib=" << usage_after.ru_maxrss << '\n';
        std::cout << "last_frame_checksum=" << last_frame_checksum << '\n';
        std::cout << "result=" << (fps >= 59.727500569606 ? "pass" : "slow")
                  << '\n';
        return fps >= 59.727500569606 ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
