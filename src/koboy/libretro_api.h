/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_LIBRETRO_API_H
#define NICKELSPERIMENTS_LIBRETRO_API_H

#include <cstddef>
#include "libretro.h"

namespace koboy {

/*
 * The libretro ABI uses one global symbol set per core.  Ko Boy binds those
 * symbols into a table so the frontend host can be shared by multiple cores
 * without knowing whether a table came from static symbols or a shared object.
 */
struct LibretroApi {
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*init)();
    void (*deinit)();
    void (*get_system_info)(retro_system_info *);
    bool (*load_game)(const retro_game_info *);
    void (*unload_game)();
    void (*get_system_av_info)(retro_system_av_info *);
    void (*reset)();
    void (*run)();
    std::size_t (*serialize_size)();
    bool (*serialize)(void *, std::size_t);
    bool (*unserialize)(const void *, std::size_t);
    void *(*get_memory_data)(unsigned);
    std::size_t (*get_memory_size)(unsigned);
};

const LibretroApi &armgba_libretro_api();

} // namespace koboy

#endif
