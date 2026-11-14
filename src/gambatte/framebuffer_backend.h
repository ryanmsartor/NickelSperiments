/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_FRAMEBUFFER_BACKEND_H
#define NICKELSPERIMENTS_FRAMEBUFFER_BACKEND_H

#include <linux/fb.h>
#include <cstddef>
#include <stdint.h>
#include <vector>

namespace gambatte_kobo {

struct Viewport {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

class FramebufferBackend {
public:
    FramebufferBackend();
    ~FramebufferBackend();

    void open_device(const char *path);
    void configure_viewport(unsigned content_width, unsigned content_height);
    void save_viewport();
    void save_screen();
    void clear_viewport(uint32_t pixel);
    void clear_screen(uint32_t pixel);
    void draw(const std::vector<uint32_t> &pixels);
    void draw_screen(const std::vector<uint32_t> &pixels);
    double refresh_a2(uint32_t marker);
    double refresh_gc16(uint32_t marker, bool wait_for_completion);
    double refresh_screen_gc16(uint32_t marker, bool wait_for_completion);
    void restore(uint32_t marker);

    const Viewport &viewport() const { return viewport_; }
    const fb_var_screeninfo &variable_info() const { return variable_; }
    const fb_fix_screeninfo &fixed_info() const { return fixed_; }

private:
    FramebufferBackend(const FramebufferBackend &);
    FramebufferBackend &operator=(const FramebufferBackend &);

    double submit(uint32_t waveform,
                  uint32_t update_mode,
                  uint32_t marker,
                  bool wait_for_completion,
                  const Viewport &region);
    uint8_t *row_address(uint32_t y);
    uint8_t *screen_row_address(uint32_t y);
    void close_device();

    int fd_;
    uint8_t *mapping_;
    size_t mapping_size_;
    fb_var_screeninfo variable_;
    fb_fix_screeninfo fixed_;
    Viewport viewport_;
    std::vector<uint8_t> saved_;
    std::vector<uint8_t> saved_screen_;
    bool restored_;
};

} // namespace gambatte_kobo

#endif
