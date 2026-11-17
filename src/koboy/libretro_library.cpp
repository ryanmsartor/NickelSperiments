/* SPDX-License-Identifier: GPL-2.0-only */

#include "libretro_library.h"

#include <cstring>
#include <dlfcn.h>

namespace koboy {

namespace {

template <typename T>
bool load_symbol(void *handle, const char *name, T *destination,
                 std::string *error) {
    dlerror();
    void *symbol = dlsym(handle, name);
    const char *failure = dlerror();
    if (failure != nullptr) {
        if (error != nullptr) {
            *error = std::string("missing libretro entry point ") + name +
                     ": " + failure;
        }
        return false;
    }
    *destination = reinterpret_cast<T>(symbol);
    return true;
}

} // namespace

LibretroLibrary::LibretroLibrary() : handle_(nullptr), api_() {}

LibretroLibrary::~LibretroLibrary() {
    close();
}

bool LibretroLibrary::open(const std::string &path, std::string *error) {
    close();
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle_ == nullptr) {
        if (error != nullptr) {
            const char *failure = dlerror();
            *error = std::string("could not load ") + path + ": " +
                     (failure != nullptr ? failure : "unknown loader error");
        }
        return false;
    }

#define LOAD(name, field) \
    if (!load_symbol(handle_, name, &api_.field, error)) { close(); return false; }
    LOAD("retro_set_environment", set_environment)
    LOAD("retro_set_video_refresh", set_video_refresh)
    LOAD("retro_set_audio_sample", set_audio_sample)
    LOAD("retro_set_audio_sample_batch", set_audio_sample_batch)
    LOAD("retro_set_input_poll", set_input_poll)
    LOAD("retro_set_input_state", set_input_state)
    LOAD("retro_init", init)
    LOAD("retro_deinit", deinit)
    LOAD("retro_get_system_info", get_system_info)
    LOAD("retro_load_game", load_game)
    LOAD("retro_unload_game", unload_game)
    LOAD("retro_get_system_av_info", get_system_av_info)
    LOAD("retro_reset", reset)
    LOAD("retro_run", run)
    LOAD("retro_serialize_size", serialize_size)
    LOAD("retro_serialize", serialize)
    LOAD("retro_unserialize", unserialize)
    LOAD("retro_get_memory_data", get_memory_data)
    LOAD("retro_get_memory_size", get_memory_size)
#undef LOAD

    return true;
}

void LibretroLibrary::close() {
    if (handle_ != nullptr) {
        dlclose(handle_);
        handle_ = nullptr;
    }
    std::memset(&api_, 0, sizeof(api_));
}

} // namespace koboy
