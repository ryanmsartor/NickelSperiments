/* SPDX-License-Identifier: GPL-2.0-only */

#include "libretro_api.h"

namespace koboy {

const LibretroApi &armgba_libretro_api() {
    static const LibretroApi api = {
        retro_set_environment,
        retro_set_video_refresh,
        retro_set_audio_sample,
        retro_set_audio_sample_batch,
        retro_set_input_poll,
        retro_set_input_state,
        retro_init,
        retro_deinit,
        retro_get_system_info,
        retro_load_game,
        retro_unload_game,
        retro_get_system_av_info,
        retro_reset,
        retro_run,
        retro_serialize_size,
        retro_serialize,
        retro_unserialize,
        retro_get_memory_data,
        retro_get_memory_size
    };
    return api;
}

} // namespace koboy
