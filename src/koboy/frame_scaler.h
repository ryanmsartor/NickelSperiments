/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_FRAME_SCALER_H
#define NICKELSPERIMENTS_FRAME_SCALER_H

#include <stdint.h>
#include <vector>

namespace koboy {

enum {
    kGameBoyWidth = 160,
    kGameBoyHeight = 144
};

void render_test_pattern(std::vector<uint8_t> &frame, unsigned frame_number);

void scale_monochrome(const std::vector<uint8_t> &source,
                      unsigned scale,
                      std::vector<uint32_t> &destination);

void scale_rgb32_dithered_bichrome(const std::vector<uint32_t> &source,
                                   unsigned source_width,
                                   unsigned source_height,
                                   unsigned scale,
                                   std::vector<uint32_t> &destination,
                                   std::vector<uint32_t> &previous_source,
                                   bool grid_effect = false);

} // namespace koboy

#endif
