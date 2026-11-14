/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_FRAME_SCALER_H
#define NICKELSPERIMENTS_FRAME_SCALER_H

#include <stdint.h>
#include <vector>

namespace gambatte_kobo {

enum {
    kGameBoyWidth = 160,
    kGameBoyHeight = 144
};

void render_test_pattern(std::vector<uint8_t> &frame, unsigned frame_number);

void scale_monochrome(const std::vector<uint8_t> &source,
                      unsigned scale,
                      std::vector<uint32_t> &destination);

void scale_rgb32_grayscale(const std::vector<uint32_t> &source,
                           unsigned scale,
                           std::vector<uint32_t> &destination);

} // namespace gambatte_kobo

#endif
