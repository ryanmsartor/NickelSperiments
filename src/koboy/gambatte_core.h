/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_GAMBATTE_CORE_H
#define NICKELSPERIMENTS_GAMBATTE_CORE_H

#include "emulator_core.h"

namespace koboy {

class GambatteCore : public EmulatorCore {
public:
    GambatteCore();
    virtual ~GambatteCore();

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
    virtual std::size_t state_size() const;
    virtual bool save_state(void *data, std::size_t size);
    virtual bool load_state(const void *data, std::size_t size);
    virtual void *save_ram_data();
    virtual std::size_t save_ram_size() const;

private:
    GambatteCore(const GambatteCore &);
    GambatteCore &operator=(const GambatteCore &);

    struct Impl;
    Impl *impl_;
};

} // namespace koboy

#endif
