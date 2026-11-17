/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_LIBRETRO_LIBRARY_H
#define NICKELSPERIMENTS_LIBRETRO_LIBRARY_H

#include <string>

#include "libretro_api.h"

namespace koboy {

class LibretroLibrary {
public:
    LibretroLibrary();
    ~LibretroLibrary();

    LibretroLibrary(const LibretroLibrary &) = delete;
    LibretroLibrary &operator=(const LibretroLibrary &) = delete;

    bool open(const std::string &path, std::string *error);
    void close();

    bool is_open() const { return handle_ != nullptr; }
    const LibretroApi &api() const { return api_; }

private:
    void *handle_;
    LibretroApi api_;
};

} // namespace koboy

#endif
