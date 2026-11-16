/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_EMULATOR_CORE_H
#define NICKELSPERIMENTS_EMULATOR_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace koboy {

enum CoreButton {
    CORE_BUTTON_A = 0x01,
    CORE_BUTTON_B = 0x02,
    CORE_BUTTON_SELECT = 0x04,
    CORE_BUTTON_START = 0x08,
    CORE_BUTTON_RIGHT = 0x10,
    CORE_BUTTON_LEFT = 0x20,
    CORE_BUTTON_UP = 0x40,
    CORE_BUTTON_DOWN = 0x80,
    CORE_BUTTON_L = 0x100,
    CORE_BUTTON_R = 0x200
};

class EmulatorCore {
public:
    virtual ~EmulatorCore() {}

    virtual const char *name() const = 0;
    virtual unsigned video_width() const = 0;
    virtual unsigned video_height() const = 0;
    virtual double frame_rate() const = 0;

    virtual bool load_rom(const std::string &path,
                          const std::vector<unsigned char> &rom,
                          std::string &error) = 0;
    virtual void reset() = 0;
    virtual void run_frame(unsigned buttons,
                           std::vector<uint32_t> &video,
                           bool capture_video) = 0;
    virtual void set_audio_paused(bool) {}
    virtual void set_audio_volume(unsigned) {}
    virtual uint64_t audio_frames_queued() const { return 0; }
    virtual uint64_t audio_frames_dropped() const { return 0; }

    virtual std::size_t state_size() const = 0;
    virtual bool save_state(void *data, std::size_t size) = 0;
    virtual bool load_state(const void *data, std::size_t size) = 0;

    virtual void *save_ram_data() = 0;
    virtual std::size_t save_ram_size() const = 0;
};

} // namespace koboy

#endif
