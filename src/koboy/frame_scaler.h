/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_FRAME_SCALER_H
#define NICKELSPERIMENTS_FRAME_SCALER_H

#include <stdint.h>
#include <vector>

namespace koboy {

class FramebufferBackend;

enum {
    kGameBoyWidth = 160,
    kGameBoyHeight = 144
};

enum ScreenEffect {
    SCREEN_EFFECT_OFF = 0,
    SCREEN_EFFECT_GRID = 1,
    SCREEN_EFFECT_SCANLINES = 2,
    SCREEN_EFFECT_CRT = 3
};

void render_test_pattern(std::vector<uint8_t> &frame, unsigned frame_number);

void scale_monochrome(const std::vector<uint8_t> &source,
                      unsigned scale,
                      std::vector<uint32_t> &destination);

void scale_rgb32_dithered_bichrome(const std::vector<uint32_t> &source,
                                   unsigned source_width,
                                   unsigned source_height,
                                   unsigned scale,
                                   std::vector<uint32_t> &previous_source,
                                   FramebufferBackend &framebuffer,
                                   ScreenEffect screen_effect =
                                       SCREEN_EFFECT_OFF);

} // namespace koboy

#endif
