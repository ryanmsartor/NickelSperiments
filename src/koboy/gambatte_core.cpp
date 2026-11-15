/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "gambatte_core.h"

#include <gambatte.h>

namespace koboy {
namespace {

const unsigned kWidth = 160U;
const unsigned kHeight = 144U;
const unsigned kSamplesPerCall = 2064U;
const unsigned kSoundBufferSamples = kSamplesPerCall + 2064U;

class GambatteInput : public gambatte::InputGetter {
public:
    GambatteInput() : buttons_(0U) {}
    void set(unsigned buttons) { buttons_ = buttons & 0xFFU; }
    virtual unsigned operator()() { return buttons_; }

private:
    unsigned buttons_;
};

} // namespace

struct GambatteCore::Impl {
    gambatte::GB gb;
    GambatteInput input;
    std::vector<gambatte::video_pixel_t> pixels;
    std::vector<gambatte::uint_least32_t> sound;

    Impl() : pixels(kWidth * kHeight), sound(kSoundBufferSamples) {
        gb.setInputGetter(&input);
    }
};

GambatteCore::GambatteCore() : impl_(new Impl) {}

GambatteCore::~GambatteCore() { delete impl_; }

const char *GambatteCore::name() const { return "Gambatte"; }

unsigned GambatteCore::video_width() const { return kWidth; }

unsigned GambatteCore::video_height() const { return kHeight; }

double GambatteCore::frame_rate() const { return 59.727500569606; }

bool GambatteCore::load_rom(const std::string &,
                            const std::vector<unsigned char> &rom,
                            std::string &error) {
    if (rom.empty() || impl_->gb.load(&rom[0], rom.size()) != 0) {
        error = "Gambatte rejected the ROM image";
        return false;
    }
    impl_->gb.setInputGetter(&impl_->input);
    return true;
}

void GambatteCore::reset() { impl_->gb.reset(); }

void GambatteCore::run_frame(unsigned buttons, std::vector<uint32_t> &video,
                             bool capture_video) {
    impl_->input.set(buttons);
    for (;;) {
        unsigned samples = kSamplesPerCall;
        if (impl_->gb.runFor(&impl_->pixels[0], kWidth, &impl_->sound[0],
                             impl_->sound.size(), samples) >= 0)
            break;
    }
    if (capture_video) {
        video.resize(impl_->pixels.size());
        for (std::size_t i = 0; i < impl_->pixels.size(); ++i)
            video[i] = static_cast<uint32_t>(impl_->pixels[i]);
    }
}

std::size_t GambatteCore::state_size() const {
    return impl_->gb.stateSize();
}

bool GambatteCore::save_state(void *data, std::size_t size) {
    if (!data || size != state_size())
        return false;
    impl_->gb.saveState(data);
    return true;
}

bool GambatteCore::load_state(const void *data, std::size_t size) {
    return data && impl_->gb.loadState(data, size);
}

void *GambatteCore::save_ram_data() { return impl_->gb.savedata_ptr(); }

std::size_t GambatteCore::save_ram_size() const {
    return impl_->gb.savedata_size();
}

} // namespace koboy
