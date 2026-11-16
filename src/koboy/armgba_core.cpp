/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "armgba_core.h"

#include "libretro.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace koboy {
namespace {

ArmGbaCore *active_core;

uint32_t rgb565_to_xrgb8888(uint16_t pixel) {
    const uint32_t red = (pixel >> 11U) & 0x1FU;
    const uint32_t green = (pixel >> 5U) & 0x3FU;
    const uint32_t blue = pixel & 0x1FU;
    return 0xFF000000U | ((red << 3U) | (red >> 2U)) << 16U |
           ((green << 2U) | (green >> 4U)) << 8U |
           ((blue << 3U) | (blue >> 2U));
}

void core_log(enum retro_log_level, const char *format, ...) {
    char message[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    std::fputs(message, stderr);
}

} // namespace

struct ArmGbaCore::Impl {
    enum retro_pixel_format pixel_format;
    unsigned width;
    unsigned height;
    double fps;
    unsigned buttons;
    bool loaded;
    bool capture_video;
    std::string error;
    std::vector<uint32_t> frame;

    Impl()
        : pixel_format(RETRO_PIXEL_FORMAT_XRGB8888), width(160U),
          height(144U), fps(16777216.0 / 280896.0), buttons(0U),
          loaded(false), capture_video(true) {}
};

static bool environment_callback(unsigned command, void *data) {
    if (!active_core)
        return false;
    ArmGbaCore::Impl &impl = *active_core->impl_;
    static const char *const system_directory = "/mnt/onboard/KoBoy/BIOS";
    static const char *const save_directory = "/mnt/onboard/KoBoy/Saves";
    switch (command) {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            impl.pixel_format = *static_cast<enum retro_pixel_format *>(data);
            return impl.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888 ||
                   impl.pixel_format == RETRO_PIXEL_FORMAT_RGB565;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            *static_cast<const char **>(data) = system_directory;
            return true;
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *static_cast<const char **>(data) = save_directory;
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            retro_variable *variable = static_cast<retro_variable *>(data);
            if (!variable || !variable->key)
                return false;
            if (std::strcmp(variable->key, "armgba_skip_bios") == 0)
                variable->value = "disabled";
            else if (std::strcmp(variable->key, "armgba_bios") == 0)
                variable->value = "nintendo";
            else if (std::strcmp(variable->key, "armgba_gb_boot_rom") == 0)
                variable->value = "enabled";
            else
                return false;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            static_cast<retro_log_callback *>(data)->log = core_log;
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *static_cast<bool *>(data) = true;
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool *>(data) = false;
            return true;
        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
            *static_cast<int *>(data) = RETRO_AV_ENABLE_AUDIO |
                                       RETRO_AV_ENABLE_VIDEO;
            return true;
        case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
            *static_cast<bool *>(data) = false;
            return true;
        case RETRO_ENVIRONMENT_SET_GEOMETRY: {
            const retro_game_geometry *geometry =
                static_cast<const retro_game_geometry *>(data);
            impl.width = geometry->base_width;
            impl.height = geometry->base_height;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
            const retro_system_av_info *information =
                static_cast<const retro_system_av_info *>(data);
            impl.width = information->geometry.base_width;
            impl.height = information->geometry.base_height;
            impl.fps = information->timing.fps;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_MESSAGE:
            impl.error = static_cast<const retro_message *>(data)->msg;
            return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
            impl.error = static_cast<const retro_message_ext *>(data)->msg;
            return true;
        default:
            return false;
    }
}

static void video_callback(const void *data, unsigned width,
                           unsigned height, size_t pitch) {
    if (!active_core || !data)
        return;
    ArmGbaCore::Impl &impl = *active_core->impl_;
    impl.width = width;
    impl.height = height;
    if (!impl.capture_video)
        return;
    impl.frame.resize(static_cast<std::size_t>(width) * height);
    for (unsigned y = 0; y < height; ++y) {
        if (impl.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
            const uint32_t *row = reinterpret_cast<const uint32_t *>(
                static_cast<const unsigned char *>(data) + y * pitch);
            for (unsigned x = 0; x < width; ++x)
                impl.frame[y * width + x] = 0xFF000000U | row[x];
        } else {
            const uint16_t *row = reinterpret_cast<const uint16_t *>(
                static_cast<const unsigned char *>(data) + y * pitch);
            for (unsigned x = 0; x < width; ++x)
                impl.frame[y * width + x] = rgb565_to_xrgb8888(row[x]);
        }
    }
}

static size_t audio_callback(const int16_t *, size_t frames) { return frames; }

static void input_poll_callback() {}

static int16_t input_state_callback(unsigned port, unsigned device,
                                    unsigned, unsigned id) {
    if (!active_core || port != 0U || device != RETRO_DEVICE_JOYPAD)
        return 0;
    const unsigned buttons = active_core->impl_->buttons;
    switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_A: return buttons & CORE_BUTTON_A;
        case RETRO_DEVICE_ID_JOYPAD_B: return buttons & CORE_BUTTON_B;
        case RETRO_DEVICE_ID_JOYPAD_SELECT: return buttons & CORE_BUTTON_SELECT;
        case RETRO_DEVICE_ID_JOYPAD_START: return buttons & CORE_BUTTON_START;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT: return buttons & CORE_BUTTON_RIGHT;
        case RETRO_DEVICE_ID_JOYPAD_LEFT: return buttons & CORE_BUTTON_LEFT;
        case RETRO_DEVICE_ID_JOYPAD_UP: return buttons & CORE_BUTTON_UP;
        case RETRO_DEVICE_ID_JOYPAD_DOWN: return buttons & CORE_BUTTON_DOWN;
        case RETRO_DEVICE_ID_JOYPAD_L: return buttons & CORE_BUTTON_L;
        case RETRO_DEVICE_ID_JOYPAD_R: return buttons & CORE_BUTTON_R;
        default: return 0;
    }
}

ArmGbaCore::ArmGbaCore() : impl_(new Impl) {
    if (active_core) {
        delete impl_;
        throw std::runtime_error("armGBA only supports one active core");
    }
    active_core = this;
    retro_set_environment(environment_callback);
    retro_set_video_refresh(video_callback);
    retro_set_audio_sample(0);
    retro_set_audio_sample_batch(audio_callback);
    retro_set_input_poll(input_poll_callback);
    retro_set_input_state(input_state_callback);
    retro_init();
}

ArmGbaCore::~ArmGbaCore() {
    if (impl_->loaded)
        retro_unload_game();
    retro_deinit();
    active_core = 0;
    delete impl_;
}

const char *ArmGbaCore::name() const { return "armGBA"; }
unsigned ArmGbaCore::video_width() const { return impl_->width; }
unsigned ArmGbaCore::video_height() const { return impl_->height; }
double ArmGbaCore::frame_rate() const { return impl_->fps; }

bool ArmGbaCore::load_rom(const std::string &path,
                          const std::vector<unsigned char> &rom,
                          std::string &error) {
    if (impl_->loaded) {
        retro_unload_game();
        impl_->loaded = false;
    }
    impl_->error.clear();
    retro_game_info game = {path.c_str(), rom.empty() ? 0 : &rom[0],
                            rom.size(), 0};
    if (rom.empty() || !retro_load_game(&game)) {
        error = impl_->error.empty() ? "armGBA rejected the ROM image"
                                     : impl_->error;
        return false;
    }
    impl_->loaded = true;
    retro_system_av_info information;
    retro_get_system_av_info(&information);
    impl_->width = information.geometry.base_width;
    impl_->height = information.geometry.base_height;
    impl_->fps = information.timing.fps;
    return true;
}

void ArmGbaCore::reset() { retro_reset(); }

void ArmGbaCore::run_frame(unsigned buttons, std::vector<uint32_t> &video,
                           bool capture_video) {
    impl_->buttons = buttons;
    impl_->capture_video = capture_video;
    retro_run();
    if (capture_video)
        video = impl_->frame;
}

std::size_t ArmGbaCore::state_size() const { return retro_serialize_size(); }

bool ArmGbaCore::save_state(void *data, std::size_t size) {
    return retro_serialize(data, size);
}

bool ArmGbaCore::load_state(const void *data, std::size_t size) {
    return retro_unserialize(data, size);
}

void *ArmGbaCore::save_ram_data() {
    return retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
}

std::size_t ArmGbaCore::save_ram_size() const {
    return retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
}

} // namespace koboy
