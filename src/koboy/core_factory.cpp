/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "core_factory.h"
#include "libretro_core.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace koboy {

namespace {

std::string extension_of(const std::string &path) {
    const std::string::size_type slash = path.find_last_of('/');
    const std::string::size_type dot = path.find_last_of('.');
    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash))
        return std::string();
    std::string extension = path.substr(dot + 1U);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension;
}

bool extension_in(const std::string &extension, const char *const *values,
                  std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (extension == values[index])
            return true;
    }
    return false;
}

} // namespace

bool is_gamate_rom(const std::vector<unsigned char> &rom) {
    static const unsigned char signature[] =
        "COPYRIGHT BIT CORPORATION";
    const std::size_t offset = 5U;
    const std::size_t length = sizeof(signature) - 1U;
    return rom.size() >= offset + length &&
        std::equal(signature, signature + length, rom.begin() + offset);
}

std::unique_ptr<EmulatorCore> create_core_for_rom(
        const std::string &path, const std::vector<unsigned char> &rom) {
    const std::string extension = extension_of(path);
    if (extension == "gam" ||
        (extension == "bin" && is_gamate_rom(rom))) {
        return std::unique_ptr<EmulatorCore>(new LibretroCore(
            "/mnt/onboard/.adds/koboy/cores/superwhizkid_libretro.so"));
    }
    static const char *const armnes_extensions[] = {
        "nes", "fds", "unf", "unif", "nsf", "sv", "ws"
    };
    if (extension_in(extension, armnes_extensions,
                     sizeof(armnes_extensions) / sizeof(armnes_extensions[0]))) {
        return std::unique_ptr<EmulatorCore>(new LibretroCore(
            "/mnt/onboard/.adds/koboy/cores/armnes_libretro.so"));
    }
    static const char *const armgba_extensions[] = {
        "gb", "gbc", "gb2", "gba", "gba2", "bin", "duck",
        "md0", "md1", "md2", "md20s", "md25s"
    };
    if (!extension_in(extension, armgba_extensions,
                      sizeof(armgba_extensions) / sizeof(armgba_extensions[0])))
        throw std::runtime_error("no emulator core supports this ROM extension");
    return std::unique_ptr<EmulatorCore>(new LibretroCore(
        "/mnt/onboard/.adds/koboy/cores/armgba_libretro.so"));
}

} // namespace koboy
