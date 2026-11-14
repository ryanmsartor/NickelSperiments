/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "framebuffer_backend.h"
#include "kobo_hwtcon.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

namespace gambatte_kobo {
namespace {

double monotonic_seconds() {
    timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        throw std::runtime_error("clock_gettime failed");
    return static_cast<double>(value.tv_sec) +
           static_cast<double>(value.tv_nsec) / 1000000000.0;
}

std::runtime_error system_error(const char *operation) {
    return std::runtime_error(std::string(operation) + ": " +
                              std::strerror(errno));
}

} // namespace

FramebufferBackend::FramebufferBackend()
    : fd_(-1), mapping_(0), mapping_size_(0), restored_(false) {
    std::memset(&variable_, 0, sizeof(variable_));
    std::memset(&fixed_, 0, sizeof(fixed_));
    std::memset(&viewport_, 0, sizeof(viewport_));
}

FramebufferBackend::~FramebufferBackend() {
    if (!restored_ && (!saved_.empty() || !saved_screen_.empty())) {
        try {
            restore(0xFFFFFFFEU);
        } catch (...) {
        }
    }
    close_device();
}

void FramebufferBackend::open_device(const char *path) {
    if (fd_ >= 0)
        throw std::runtime_error("framebuffer is already open");

    fd_ = ::open(path, O_RDWR | O_CLOEXEC);
    if (fd_ < 0)
        throw system_error("open framebuffer");
    if (ioctl(fd_, FBIOGET_FSCREENINFO, &fixed_) < 0)
        throw system_error("FBIOGET_FSCREENINFO");
    if (ioctl(fd_, FBIOGET_VSCREENINFO, &variable_) < 0)
        throw system_error("FBIOGET_VSCREENINFO");

    if (std::strncmp(fixed_.id, "hwtcon", 6) != 0)
        throw std::runtime_error("unsupported framebuffer: expected hwtcon");
    if (variable_.bits_per_pixel != 32U)
        throw std::runtime_error("unsupported framebuffer: expected 32 bpp");
    if (fixed_.line_length < variable_.xres * 4U)
        throw std::runtime_error("invalid framebuffer stride");

    mapping_size_ = fixed_.smem_len;
    mapping_ = static_cast<uint8_t *>(
        mmap(0, mapping_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
    if (mapping_ == MAP_FAILED) {
        mapping_ = 0;
        throw system_error("mmap framebuffer");
    }
}

void FramebufferBackend::configure_viewport(unsigned content_width,
                                            unsigned content_height) {
    if (!mapping_)
        throw std::runtime_error("framebuffer is not open");
    if (content_width > variable_.xres || content_height > variable_.yres)
        throw std::runtime_error("content does not fit framebuffer");

    viewport_.width = content_width;
    viewport_.height = content_height;
    viewport_.x = ((variable_.xres - content_width) / 2U) & ~7U;
    viewport_.y = 96U;
    if (viewport_.y + content_height > variable_.yres)
        viewport_.y = ((variable_.yres - content_height) / 2U) & ~7U;
}

uint8_t *FramebufferBackend::row_address(uint32_t y) {
    const size_t offset = static_cast<size_t>(variable_.yoffset + y) *
                              fixed_.line_length +
                          static_cast<size_t>(variable_.xoffset + viewport_.x) * 4U;
    const size_t bytes = static_cast<size_t>(viewport_.width) * 4U;
    if (offset + bytes > mapping_size_)
        throw std::runtime_error("framebuffer row is outside mapped memory");
    return mapping_ + offset;
}

uint8_t *FramebufferBackend::screen_row_address(uint32_t y) {
    const size_t offset = static_cast<size_t>(variable_.yoffset + y) *
                              fixed_.line_length +
                          static_cast<size_t>(variable_.xoffset) * 4U;
    const size_t bytes = static_cast<size_t>(variable_.xres) * 4U;
    if (offset + bytes > mapping_size_)
        throw std::runtime_error("framebuffer row is outside mapped memory");
    return mapping_ + offset;
}

void FramebufferBackend::save_viewport() {
    const size_t row_bytes = static_cast<size_t>(viewport_.width) * 4U;
    saved_.resize(row_bytes * viewport_.height);
    for (uint32_t y = 0; y < viewport_.height; ++y)
        std::memcpy(&saved_[static_cast<size_t>(y) * row_bytes],
                    row_address(viewport_.y + y), row_bytes);
    restored_ = false;
}

void FramebufferBackend::save_screen() {
    const size_t row_bytes = static_cast<size_t>(variable_.xres) * 4U;
    saved_screen_.resize(row_bytes * variable_.yres);
    for (uint32_t y = 0; y < variable_.yres; ++y)
        std::memcpy(&saved_screen_[static_cast<size_t>(y) * row_bytes],
                    screen_row_address(y), row_bytes);
    restored_ = false;
}

void FramebufferBackend::clear_viewport(uint32_t pixel) {
    for (uint32_t y = 0; y < viewport_.height; ++y) {
        uint32_t *row = reinterpret_cast<uint32_t *>(row_address(viewport_.y + y));
        for (uint32_t x = 0; x < viewport_.width; ++x)
            row[x] = pixel;
    }
}

void FramebufferBackend::clear_screen(uint32_t pixel) {
    for (uint32_t y = 0; y < variable_.yres; ++y) {
        uint32_t *row = reinterpret_cast<uint32_t *>(screen_row_address(y));
        for (uint32_t x = 0; x < variable_.xres; ++x)
            row[x] = pixel;
    }
}

void FramebufferBackend::draw(const std::vector<uint32_t> &pixels) {
    if (pixels.size() != static_cast<size_t>(viewport_.width) * viewport_.height)
        throw std::runtime_error("scaled frame does not match viewport");
    const size_t row_bytes = static_cast<size_t>(viewport_.width) * 4U;
    for (uint32_t y = 0; y < viewport_.height; ++y)
        std::memcpy(row_address(viewport_.y + y),
                    &pixels[static_cast<size_t>(y) * viewport_.width], row_bytes);
}

void FramebufferBackend::draw_screen(const std::vector<uint32_t> &pixels) {
    if (pixels.size() != static_cast<size_t>(variable_.xres) * variable_.yres)
        throw std::runtime_error("screen image does not match framebuffer");
    const size_t row_bytes = static_cast<size_t>(variable_.xres) * 4U;
    for (uint32_t y = 0; y < variable_.yres; ++y)
        std::memcpy(screen_row_address(y),
                    &pixels[static_cast<size_t>(y) * variable_.xres], row_bytes);
}

double FramebufferBackend::submit(uint32_t waveform,
                                  uint32_t update_mode,
                                  uint32_t marker,
                                  bool wait_for_completion,
                                  const Viewport &region) {
    hwtcon_update_data update;
    std::memset(&update, 0, sizeof(update));
    update.update_region.top = region.y;
    update.update_region.left = region.x;
    update.update_region.width = region.width;
    update.update_region.height = region.height;
    update.waveform_mode = waveform;
    update.update_mode = update_mode;
    update.update_marker = marker;

    const double started = monotonic_seconds();
    if (ioctl(fd_, HWTCON_SEND_UPDATE, &update) < 0)
        throw system_error("HWTCON_SEND_UPDATE");
    if (wait_for_completion) {
        uint32_t wait_marker = marker;
        if (ioctl(fd_, HWTCON_WAIT_FOR_UPDATE_COMPLETE, &wait_marker) < 0)
            throw system_error("HWTCON_WAIT_FOR_UPDATE_COMPLETE");
    }
    return monotonic_seconds() - started;
}

double FramebufferBackend::refresh_a2(uint32_t marker) {
    return submit(HWTCON_WAVEFORM_A2, HWTCON_UPDATE_PARTIAL, marker, false,
                  viewport_);
}

double FramebufferBackend::refresh_gc16(uint32_t marker,
                                        bool wait_for_completion) {
    return submit(HWTCON_WAVEFORM_GC16, HWTCON_UPDATE_FULL, marker,
                  wait_for_completion, viewport_);
}

double FramebufferBackend::refresh_screen_gc16(uint32_t marker,
                                                bool wait_for_completion) {
    Viewport screen = { 0U, 0U, variable_.xres, variable_.yres };
    return submit(HWTCON_WAVEFORM_GC16, HWTCON_UPDATE_FULL, marker,
                  wait_for_completion, screen);
}

void FramebufferBackend::restore(uint32_t marker) {
    if (restored_)
        return;
    if (!saved_screen_.empty()) {
        const size_t row_bytes = static_cast<size_t>(variable_.xres) * 4U;
        for (uint32_t y = 0; y < variable_.yres; ++y)
            std::memcpy(screen_row_address(y),
                        &saved_screen_[static_cast<size_t>(y) * row_bytes],
                        row_bytes);
        refresh_screen_gc16(marker, true);
    } else if (!saved_.empty()) {
        const size_t row_bytes = static_cast<size_t>(viewport_.width) * 4U;
        for (uint32_t y = 0; y < viewport_.height; ++y)
            std::memcpy(row_address(viewport_.y + y),
                        &saved_[static_cast<size_t>(y) * row_bytes], row_bytes);
        refresh_gc16(marker, true);
    } else {
        return;
    }
    restored_ = true;
}

void FramebufferBackend::close_device() {
    if (mapping_) {
        munmap(mapping_, mapping_size_);
        mapping_ = 0;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace gambatte_kobo
