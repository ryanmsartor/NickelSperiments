/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "libretro_core.h"
#include "core_options.h"
#include "libretro_library.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <stdexcept>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

namespace koboy {
namespace {

LibretroCore *active_core;

bool bluetooth_audio_connected() {
    FILE *query = popen(
        "dbus-send --system --print-reply "
        "--dest=com.kobo.mtk.bluedroid / "
        "org.freedesktop.DBus.ObjectManager.GetManagedObjects 2>/dev/null",
        "r");
    if (!query)
        return false;
    bool audio_connected = false;
    bool device_connected = false;
    bool device_audio = false;
    bool in_device = false;
    unsigned connected_value_lines = 0U;
    unsigned icon_value_lines = 0U;
    char line[256];
    while (std::fgets(line, sizeof(line), query)) {
        if (std::strstr(line, "object path \"/org/bluez/hci0/")) {
            if (in_device && device_connected && device_audio) {
                audio_connected = true;
                break;
            }
            in_device = std::strstr(line, "/dev_") != 0;
            device_connected = false;
            device_audio = false;
            connected_value_lines = 0U;
            icon_value_lines = 0U;
            continue;
        }
        if (!in_device)
            continue;
        if (std::strstr(line, "string \"Connected\"")) {
            connected_value_lines = 3U;
            continue;
        }
        if (connected_value_lines != 0U) {
            if (std::strstr(line, "boolean true"))
                device_connected = true;
            --connected_value_lines;
        }
        if (std::strstr(line, "string \"Icon\"")) {
            icon_value_lines = 3U;
            continue;
        }
        if (icon_value_lines != 0U) {
            if (std::strstr(line, "string \"audio-"))
                device_audio = true;
            --icon_value_lines;
        }
    }
    if (in_device && device_connected && device_audio)
        audio_connected = true;
    pclose(query);
    return audio_connected;
}

class AudioOutput {
public:
    AudioOutput()
        : pipe_fd_(-1), child_(-1), stopping_(false), paused_(false),
          volume_(10U), sample_rate_(32768U), queued_frames_(0),
          dropped_frames_(0), read_(0), used_(0),
          samples_(65536U * 2U) {}

    ~AudioOutput() { stop(); }

    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pipe_fd_ >= 0 || child_ > 0)
            return;
        int descriptors[2];
        if (pipe(descriptors) != 0)
            return;
        const pid_t child = fork();
        if (child == 0) {
            char caps[96];
            std::snprintf(caps, sizeof(caps),
                          "audio/x-raw,format=S16LE,rate=%u,channels=2,layout=interleaved",
                          sample_rate_);
            dup2(descriptors[0], STDIN_FILENO);
            close(descriptors[0]);
            close(descriptors[1]);
            const long maximum_fd = sysconf(_SC_OPEN_MAX);
            for (int fd = 3; fd < maximum_fd; ++fd)
                close(fd);
            execlp("gst-launch-1.0", "gst-launch-1.0", "-q",
                   "fdsrc", "fd=0", "!",
                   caps,
                   "!", "audioresample", "quality=0", "!",
                   "audio/x-raw,format=S16LE,rate=48000,channels=2,layout=interleaved",
                   "!", "mtkbtmwrpcaudiosink", "sync=false",
                   static_cast<char *>(0));
            _exit(127);
        }
        close(descriptors[0]);
        if (child < 0) {
            close(descriptors[1]);
            return;
        }
        pipe_fd_ = descriptors[1];
        child_ = child;
        fcntl(pipe_fd_, F_SETFL, fcntl(pipe_fd_, F_GETFL) | O_NONBLOCK);
        stopping_ = false;
        worker_ = std::thread(&AudioOutput::run, this);
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            condition_.notify_all();
        }
        if (worker_.joinable())
            worker_.join();
        if (pipe_fd_ >= 0) {
            close(pipe_fd_);
            pipe_fd_ = -1;
        }
        if (child_ > 0) {
            int status = 0;
            for (unsigned i = 0; i < 20U; ++i) {
                if (waitpid(child_, &status, WNOHANG) == child_)
                    break;
                usleep(50000);
            }
            if (waitpid(child_, &status, WNOHANG) == 0) {
                kill(child_, SIGTERM);
                waitpid(child_, &status, 0);
            }
            child_ = -1;
        }
    }

    void push(const int16_t *data, std::size_t frames) {
        if (!data || frames == 0U)
            return;
        const std::size_t count = frames * 2U;
        std::lock_guard<std::mutex> lock(mutex_);
        if (paused_ || stopping_ || pipe_fd_ < 0)
            return;
        if (count > samples_.size() - used_) {
            dropped_frames_ += frames;
            return;
        }
        static const int32_t gains[21] = {
            0, 413, 519, 655, 823, 1036, 1304, 1642, 2068, 2602, 3277,
            4125, 5194, 6538, 8231, 10361, 13045, 16422, 20675, 26028,
            32768
        };
        const int32_t gain = gains[std::min(volume_.load(), 20U)];
        std::size_t write = (read_ + used_) % samples_.size();
        for (std::size_t i = 0; i < count; ++i) {
            const int64_t product = static_cast<int64_t>(data[i]) * gain;
            samples_[write] = static_cast<int16_t>(
                (product + (product >= 0 ? 16384 : -16384)) / 32768);
            write = (write + 1U) % samples_.size();
        }
        used_ += count;
        queued_frames_ += frames;
        condition_.notify_one();
    }

    void set_paused(bool paused) {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = paused;
        if (paused) {
            read_ = 0U;
            used_ = 0U;
        }
        condition_.notify_all();
    }

    void set_volume(unsigned volume) {
        volume_ = std::min(volume, 20U);
    }

    void set_sample_rate(unsigned sample_rate) {
        if (sample_rate != 0U)
            sample_rate_ = sample_rate;
    }

    unsigned volume() const { return volume_; }

    uint64_t queued_frames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queued_frames_;
    }

    uint64_t dropped_frames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_frames_;
    }

private:
    void run() {
        std::vector<int16_t> chunk(4096U);
        while (true) {
            std::size_t count = 0U;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait_for(lock, std::chrono::milliseconds(50),
                    [this] { return stopping_ || (!paused_ && used_ != 0U); });
                if (stopping_)
                    break;
                if (paused_ || used_ == 0U)
                    continue;
                count = std::min(chunk.size(), used_);
                for (std::size_t i = 0; i < count; ++i) {
                    chunk[i] = samples_[read_];
                    read_ = (read_ + 1U) % samples_.size();
                }
                used_ -= count;
            }
            const unsigned char *bytes = reinterpret_cast<unsigned char *>(
                &chunk[0]);
            std::size_t left = count * sizeof(chunk[0]);
            while (left != 0U && !stopping_) {
                pollfd descriptor = {pipe_fd_, POLLOUT, 0};
                if (poll(&descriptor, 1, 100) <= 0)
                    continue;
                const ssize_t written = write(pipe_fd_, bytes, left);
                if (written > 0) {
                    bytes += written;
                    left -= static_cast<std::size_t>(written);
                } else if (written < 0 && errno != EAGAIN && errno != EINTR) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    stopping_ = true;
                    break;
                }
            }
        }
    }

    int pipe_fd_;
    pid_t child_;
    std::atomic<bool> stopping_;
    bool paused_;
    std::atomic<unsigned> volume_;
    unsigned sample_rate_;
    uint64_t queued_frames_;
    uint64_t dropped_frames_;
    std::size_t read_;
    std::size_t used_;
    std::vector<int16_t> samples_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
};

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

struct LibretroCore::Impl {
    enum retro_pixel_format pixel_format;
    unsigned width;
    unsigned height;
    double fps;
    unsigned buttons;
    bool loaded;
    bool audio_enabled;
    bool capture_video;
    std::string error;
    std::vector<uint32_t> frame;
    AudioOutput audio;
    retro_disk_control_callback disk;
    bool has_disk_control;
    CoreOptions options;

    explicit Impl(const std::string &core_id)
        : pixel_format(RETRO_PIXEL_FORMAT_XRGB8888), width(160U),
          height(144U), fps(16777216.0 / 280896.0), buttons(0U),
          loaded(false), audio_enabled(false), capture_video(true), disk(),
          has_disk_control(false), options(core_id) {}
};

static bool environment_callback(unsigned command, void *data) {
    if (!active_core)
        return false;
    LibretroCore::Impl &impl = *active_core->impl_;
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
            variable->value = impl.options.value(variable->key);
            return variable->value != 0;
        }
        case RETRO_ENVIRONMENT_SET_VARIABLES:
            impl.options.register_variables(
                static_cast<const retro_variable *>(data));
            return true;
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            *static_cast<unsigned *>(data) = 2U;
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
            impl.options.register_definitions(
                static_cast<const retro_core_option_definition *>(data));
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
            const retro_core_options_intl *options =
                static_cast<const retro_core_options_intl *>(data);
            impl.options.register_definitions(options ? options->us : 0);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
            const retro_core_options_v2 *options =
                static_cast<const retro_core_options_v2 *>(data);
            impl.options.register_definitions_v2(
                options ? options->definitions : 0);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
            const retro_core_options_v2_intl *options =
                static_cast<const retro_core_options_v2_intl *>(data);
            impl.options.register_definitions_v2(
                options && options->us ? options->us->definitions : 0);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY: {
            const retro_core_option_display *display =
                static_cast<const retro_core_option_display *>(data);
            if (!display || !display->key)
                return false;
            impl.options.set_visible(display->key, display->visible);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK: {
            const retro_core_options_update_display_callback *callback =
                static_cast<const retro_core_options_update_display_callback *>(
                    data);
            impl.options.set_update_display_callback(
                callback ? callback->callback : 0);
            return true;
        }
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            static_cast<retro_log_callback *>(data)->log = core_log;
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *static_cast<bool *>(data) = true;
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool *>(data) = impl.options.take_updated();
            return true;
        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
            *static_cast<int *>(data) = RETRO_AV_ENABLE_AUDIO |
                                       RETRO_AV_ENABLE_VIDEO;
            return true;
        case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
            *static_cast<bool *>(data) = false;
            return true;
        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
            if (!data)
                return false;
            impl.disk = *static_cast<retro_disk_control_callback *>(data);
            impl.has_disk_control = impl.disk.set_eject_state &&
                impl.disk.get_eject_state && impl.disk.get_image_index &&
                impl.disk.set_image_index && impl.disk.get_num_images;
            return impl.has_disk_control;
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
    LibretroCore::Impl &impl = *active_core->impl_;
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

static size_t audio_callback(const int16_t *samples, size_t frames) {
    if (active_core)
        active_core->impl_->audio.push(samples, frames);
    return frames;
}

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
        case RETRO_DEVICE_ID_JOYPAD_X: return buttons & CORE_BUTTON_X;
        case RETRO_DEVICE_ID_JOYPAD_Y: return buttons & CORE_BUTTON_Y;
        default: return 0;
    }
}

LibretroCore::LibretroCore(const std::string &library_path)
    : impl_(new Impl(library_path.find("armgba") != std::string::npos
                         ? "armgba" :
                     library_path.find("armnes") != std::string::npos
                         ? "armnes" :
                     library_path.find("superwhizkid") != std::string::npos
                         ? "superwhizkid" : "unknown")),
      api_(), library_(new LibretroLibrary), name_("libretro") {
    if (active_core) {
        delete impl_;
        delete library_;
        throw std::runtime_error("libretro host only supports one active core");
    }
    std::string error;
    if (!library_->open(library_path, &error)) {
        delete impl_;
        delete library_;
        throw std::runtime_error(error);
    }
    api_ = library_->api();
    retro_system_info information = {0, 0, 0, false, false};
    api_.get_system_info(&information);
    if (information.library_name)
        name_ = information.library_name;
    active_core = this;
    signal(SIGPIPE, SIG_IGN);
    api_.set_environment(environment_callback);
    api_.set_video_refresh(video_callback);
    api_.set_audio_sample(0);
    api_.set_audio_sample_batch(audio_callback);
    api_.set_input_poll(input_poll_callback);
    api_.set_input_state(input_state_callback);
    api_.init();
}

LibretroCore::~LibretroCore() {
    if (impl_->loaded)
        api_.unload_game();
    api_.deinit();
    active_core = 0;
    delete library_;
    delete impl_;
}

const char *LibretroCore::name() const { return name_.c_str(); }
unsigned LibretroCore::video_width() const { return impl_->width; }
unsigned LibretroCore::video_height() const { return impl_->height; }
double LibretroCore::frame_rate() const { return impl_->fps; }

bool LibretroCore::load_rom(const std::string &path,
                          const std::vector<unsigned char> &rom,
                          std::string &error) {
    if (impl_->loaded) {
        api_.unload_game();
        impl_->loaded = false;
    }
    impl_->error.clear();
    impl_->has_disk_control = false;
    std::memset(&impl_->disk, 0, sizeof(impl_->disk));
    retro_game_info game = {path.c_str(), rom.empty() ? 0 : &rom[0],
                            rom.size(), 0};
    if (rom.empty() || !api_.load_game(&game)) {
        error = impl_->error.empty() ? name_ + " rejected the ROM image"
                                     : impl_->error;
        return false;
    }
    impl_->loaded = true;
    retro_system_av_info information;
    api_.get_system_av_info(&information);
    impl_->width = information.geometry.base_width;
    impl_->height = information.geometry.base_height;
    impl_->fps = information.timing.fps;
    impl_->audio.set_sample_rate(static_cast<unsigned>(
        information.timing.sample_rate + 0.5));
    impl_->audio_enabled = bluetooth_audio_connected();
    if (name_ == "superwhizkid")
        impl_->options.set_visible("superwhizkid_audio_output",
                                   impl_->audio_enabled);
    if (impl_->audio.volume() != 0U && impl_->audio_enabled) {
        impl_->audio.start();
        std::fputs("Ko Boy: Bluetooth audio connected\n", stderr);
    } else {
        std::fputs("Ko Boy: no connected Bluetooth audio device\n", stderr);
    }
    return true;
}

void LibretroCore::reset() { api_.reset(); }

void LibretroCore::run_frame(unsigned buttons, std::vector<uint32_t> &video,
                           bool capture_video) {
    impl_->buttons = buttons;
    impl_->capture_video = capture_video;
    api_.run();
    if (capture_video)
        video = impl_->frame;
}

void LibretroCore::set_audio_paused(bool paused) {
    impl_->audio.set_paused(paused);
}

void LibretroCore::set_audio_volume(unsigned volume) {
    impl_->audio.set_volume(volume);
    if (volume == 0U) {
        impl_->audio.stop();
    } else if (impl_->loaded && bluetooth_audio_connected()) {
        impl_->audio.start();
    }
}

bool LibretroCore::audio_enabled() const {
    return impl_->audio_enabled;
}

uint64_t LibretroCore::audio_frames_queued() const {
    return impl_->audio.queued_frames();
}

uint64_t LibretroCore::audio_frames_dropped() const {
    return impl_->audio.dropped_frames();
}

unsigned LibretroCore::disk_count() const {
    return impl_->has_disk_control ? impl_->disk.get_num_images() : 0U;
}

unsigned LibretroCore::disk_index() const {
    return impl_->has_disk_control ? impl_->disk.get_image_index() : 0U;
}

bool LibretroCore::disk_ejected() const {
    return impl_->has_disk_control && impl_->disk.get_eject_state();
}

bool LibretroCore::toggle_disk() {
    const unsigned count = disk_count();
    if (!count)
        return false;
    if (!disk_ejected())
        return impl_->disk.set_eject_state(true);
    const unsigned next = (disk_index() + 1U) % count;
    return impl_->disk.set_image_index(next) &&
           impl_->disk.set_eject_state(false);
}

unsigned LibretroCore::core_option_count(bool developer) const {
    return impl_->options.curated_count(developer);
}

const char *LibretroCore::core_option_label(unsigned index,
                                          bool developer) const {
    return impl_->options.curated_label(index, developer);
}

const char *LibretroCore::core_option_value(unsigned index,
                                          bool developer) const {
    return impl_->options.curated_value_label(index, developer);
}

bool LibretroCore::cycle_core_option(unsigned index, bool developer) {
    return impl_->options.cycle_curated(index, developer);
}

std::size_t LibretroCore::state_size() const { return api_.serialize_size(); }

bool LibretroCore::save_state(void *data, std::size_t size) {
    return api_.serialize(data, size);
}

bool LibretroCore::load_state(const void *data, std::size_t size) {
    return api_.unserialize(data, size);
}

void *LibretroCore::save_ram_data() {
    return api_.get_memory_data(RETRO_MEMORY_SAVE_RAM);
}

std::size_t LibretroCore::save_ram_size() const {
    return api_.get_memory_size(RETRO_MEMORY_SAVE_RAM);
}

} // namespace koboy
