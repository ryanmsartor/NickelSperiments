/* SPDX-License-Identifier: GPL-2.0-only */

#include "libretro_library.h"

#include <iostream>
#include <string>

int main(int argc, char **argv) {
    const std::string path = argc > 1 ? argv[1]
        : "/mnt/onboard/.adds/koboy/cores/armnes_libretro.so";
    koboy::LibretroLibrary library;
    std::string error;
    if (!library.open(path, &error)) {
        std::cerr << error << '\n';
        return 1;
    }

    retro_system_info information = {nullptr, nullptr, nullptr, false, false};
    library.api().get_system_info(&information);
    std::cout << "library=" << path << '\n'
              << "name=" << (information.library_name
                                  ? information.library_name : "") << '\n'
              << "version=" << (information.library_version
                                     ? information.library_version : "") << '\n'
              << "extensions=" << (information.valid_extensions
                                        ? information.valid_extensions : "")
              << '\n'
              << "fullpath=" << (information.need_fullpath ? "yes" : "no")
              << '\n';
    return 0;
}
