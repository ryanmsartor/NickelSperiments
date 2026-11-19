/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "libretro_core.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

std::vector<unsigned char> read_file(const char *path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("cannot open ROM");
    const std::ifstream::pos_type end = input.tellg();
    if (end <= 0)
        throw std::runtime_error("ROM is empty");
    std::vector<unsigned char> data(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char *>(&data[0]), data.size());
    if (!input)
        throw std::runtime_error("cannot read complete ROM");
    return data;
}

unsigned long long checksum(const std::vector<uint32_t> &frame) {
    unsigned long long value = 1469598103934665603ULL;
    for (std::size_t i = 0; i < frame.size(); ++i) {
        value ^= frame[i];
        value *= 1099511628211ULL;
    }
    return value;
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: armgba-probe ROM\n";
            return 2;
        }
        const std::vector<unsigned char> rom = read_file(argv[1]);
        koboy::LibretroCore core(
            "/mnt/onboard/.adds/koboy/cores/armgba_libretro.so");
        std::string error;
        if (!core.load_rom(argv[1], rom, error))
            throw std::runtime_error(error);
        std::vector<uint32_t> frame;
        for (unsigned i = 0; i < 120U; ++i)
            core.run_frame(0U, frame, i == 119U);
        std::vector<unsigned char> state(core.state_size());
        const bool state_saved = !state.empty() &&
            core.save_state(&state[0], state.size());
        const bool state_loaded = state_saved &&
            core.load_state(&state[0], state.size());
        std::cout << "core=" << core.name() << '\n';
        std::cout << "video=" << core.video_width() << 'x'
                  << core.video_height() << '\n';
        std::cout << "frame_rate=" << core.frame_rate() << '\n';
        std::cout << "frame_checksum=" << checksum(frame) << '\n';
        std::cout << "state_bytes=" << state.size() << '\n';
        std::cout << "state_round_trip="
                  << (state_loaded ? "pass" : "fail") << '\n';
        std::cout << "save_ram_bytes=" << core.save_ram_size() << '\n';
        return state_loaded && !frame.empty() ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
