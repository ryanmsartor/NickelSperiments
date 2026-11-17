/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_ARMGBA_CORE_H
#define NICKELSPERIMENTS_ARMGBA_CORE_H

#include "emulator_core.h"
#include "libretro_api.h"

#include <string>

namespace koboy {

struct LibretroApi;
class LibretroLibrary;

class ArmGbaCore : public EmulatorCore {
public:
    explicit ArmGbaCore(const std::string &library_path);
    virtual ~ArmGbaCore();

    virtual const char *name() const;
    virtual unsigned video_width() const;
    virtual unsigned video_height() const;
    virtual double frame_rate() const;
    virtual bool load_rom(const std::string &path,
                          const std::vector<unsigned char> &rom,
                          std::string &error);
    virtual void reset();
    virtual void run_frame(unsigned buttons, std::vector<uint32_t> &video,
                           bool capture_video);
    virtual void set_audio_paused(bool paused);
    virtual void set_audio_volume(unsigned volume);
    virtual bool audio_enabled() const;
    virtual uint64_t audio_frames_queued() const;
    virtual uint64_t audio_frames_dropped() const;
    virtual unsigned disk_count() const;
    virtual unsigned disk_index() const;
    virtual bool disk_ejected() const;
    virtual bool toggle_disk();
    virtual unsigned core_option_count(bool developer) const;
    virtual const char *core_option_label(unsigned index,
                                          bool developer) const;
    virtual const char *core_option_value(unsigned index,
                                          bool developer) const;
    virtual bool cycle_core_option(unsigned index, bool developer);
    virtual std::size_t state_size() const;
    virtual bool save_state(void *data, std::size_t size);
    virtual bool load_state(const void *data, std::size_t size);
    virtual void *save_ram_data();
    virtual std::size_t save_ram_size() const;

    /* Used only by the C callback bridge in armgba_core.cpp. */
    struct Impl;
    Impl *impl_;

private:
    ArmGbaCore(const ArmGbaCore &);
    ArmGbaCore &operator=(const ArmGbaCore &);

    LibretroApi api_;
    LibretroLibrary *library_;
    std::string name_;
};

} // namespace koboy

#endif
