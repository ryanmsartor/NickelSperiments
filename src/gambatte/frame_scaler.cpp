/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "frame_scaler.h"

#include <stdexcept>

namespace gambatte_kobo {

void render_test_pattern(std::vector<uint8_t> &frame, unsigned frame_number) {
    frame.resize(kGameBoyWidth * kGameBoyHeight);

    const unsigned bar_x = frame_number % kGameBoyWidth;
    const unsigned box_x = (frame_number * 3U) % (kGameBoyWidth - 24U);
    const unsigned box_y = 48U + ((frame_number / 2U) % 48U);

    for (unsigned y = 0; y < kGameBoyHeight; ++y) {
        for (unsigned x = 0; x < kGameBoyWidth; ++x) {
            bool black = ((x / 8U) + (y / 8U)) % 2U != 0U;
            if (x == bar_x || x == (bar_x + 1U) % kGameBoyWidth)
                black = true;
            if (x >= box_x && x < box_x + 24U &&
                y >= box_y && y < box_y + 16U)
                black = !black;
            if (x < 4U || y < 4U || x >= kGameBoyWidth - 4U ||
                y >= kGameBoyHeight - 4U)
                black = true;
            frame[y * kGameBoyWidth + x] = black ? 0U : 255U;
        }
    }
}

void scale_monochrome(const std::vector<uint8_t> &source,
                      unsigned scale,
                      std::vector<uint32_t> &destination) {
    if (source.size() != kGameBoyWidth * kGameBoyHeight)
        throw std::runtime_error("source frame must be 160x144");
    if (scale == 0U)
        throw std::runtime_error("scale must be positive");

    const unsigned width = kGameBoyWidth * scale;
    const unsigned height = kGameBoyHeight * scale;
    destination.resize(width * height);

    for (unsigned source_y = 0; source_y < kGameBoyHeight; ++source_y) {
        for (unsigned source_x = 0; source_x < kGameBoyWidth; ++source_x) {
            const uint8_t gray = source[source_y * kGameBoyWidth + source_x];
            const uint32_t pixel = 0xFF000000U |
                                   (static_cast<uint32_t>(gray) * 0x00010101U);
            for (unsigned dy = 0; dy < scale; ++dy) {
                uint32_t *row = &destination[(source_y * scale + dy) * width +
                                             source_x * scale];
                for (unsigned dx = 0; dx < scale; ++dx)
                    row[dx] = pixel;
            }
        }
    }
}

} // namespace gambatte_kobo
