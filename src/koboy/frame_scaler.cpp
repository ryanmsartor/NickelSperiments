/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "frame_scaler.h"
#include "framebuffer_backend.h"

#include <cstring>
#include <stdexcept>

namespace koboy {

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

void scale_rgb32_dithered_bichrome(const std::vector<uint32_t> &source,
                                   unsigned source_width,
                                   unsigned source_height,
                                   unsigned scale,
                                   std::vector<uint32_t> &previous_source,
                                   FramebufferBackend &framebuffer,
                                   bool grid_effect) {
    struct DitherTable {
        uint32_t pixel[256][4][4];
        DitherTable() {
            static const unsigned bayer[4][4] = {
                { 0U,  8U,  2U, 10U},
                {12U,  4U, 14U,  6U},
                { 3U, 11U,  1U,  9U},
                {15U,  7U, 13U,  5U},
            };
            for (unsigned gray = 0U; gray < 256U; ++gray)
                for (unsigned y = 0U; y < 4U; ++y)
                    for (unsigned x = 0U; x < 4U; ++x) {
                        const unsigned cutoff =
                            (2U * bayer[y][x] + 1U) * 255U;
                        pixel[gray][y][x] = gray * 32U > cutoff ?
                            0xFFFFFFFFU : 0xFF000000U;
                    }
        }
    };
    static const DitherTable dither;
    if (source_width == 0U || source_height == 0U ||
        source.size() != static_cast<std::size_t>(source_width) * source_height)
        throw std::runtime_error("source frame dimensions do not match data");
    if (scale == 0U)
        throw std::runtime_error("scale must be positive");

    const unsigned width = source_width * scale;
    const unsigned height = source_height * scale;
    if (framebuffer.viewport().width != width ||
        framebuffer.viewport().height != height)
        throw std::runtime_error("scaled frame does not match viewport");
    const bool redraw_all = previous_source.size() != source.size();
    previous_source.resize(source.size());

    for (unsigned source_y = 0; source_y < source_height; ++source_y) {
        for (unsigned source_x = 0; source_x < source_width; ++source_x) {
            const std::size_t source_index =
                static_cast<std::size_t>(source_y) * source_width + source_x;
            const uint32_t rgb = source[source_index];
            if (!redraw_all && previous_source[source_index] == rgb)
                continue;
            previous_source[source_index] = rgb;
            const unsigned red = (rgb >> 16U) & 0xFFU;
            const unsigned green = (rgb >> 8U) & 0xFFU;
            const unsigned blue = rgb & 0xFFU;
            const uint32_t gray = (red * 54U + green * 183U + blue * 19U) >> 8U;
            const unsigned output_x = source_x * scale;
            for (unsigned dy = 0; dy < scale; ++dy) {
                const unsigned output_y = source_y * scale + dy;
                uint32_t *framebuffer_row =
                    framebuffer.mutable_viewport_row(output_y) + output_x;
                const uint32_t *pattern = dither.pixel[gray][output_y & 3U];
                if (!grid_effect && scale == 4U) {
                    std::memcpy(framebuffer_row, pattern,
                                4U * sizeof(*framebuffer_row));
                    continue;
                }
                for (unsigned dx = 0; dx < scale; ++dx) {
                    const bool vertical_grid = grid_effect &&
                        dx + 1U == scale && source_x + 1U < source_width;
                    const bool horizontal_grid = grid_effect &&
                        dy + 1U == scale && source_y + 1U < source_height;
                    const uint32_t pixel = vertical_grid || horizontal_grid ||
                        pattern[(output_x + dx) & 3U] == 0xFFFFFFFFU ?
                        0xFFFFFFFFU : 0xFF000000U;
                    framebuffer_row[dx] = pixel;
                }
            }
        }
    }
}

} // namespace koboy
