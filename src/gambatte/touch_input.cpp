/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "touch_input.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

namespace gambatte_kobo {
namespace {

bool inside_circle(int x, int y, int center_x, int center_y, int radius) {
    const int dx = x - center_x;
    const int dy = y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}

} // namespace

TouchInput::TouchInput(unsigned screen_width, unsigned screen_height)
    : fd_(-1), screen_width_(screen_width), screen_height_(screen_height),
      current_slot_(0U), physical_buttons_(0U), buttons_(0U) {
    std::memset(&x_info_, 0, sizeof(x_info_));
    std::memset(&y_info_, 0, sizeof(y_info_));
    std::memset(contacts_, 0, sizeof(contacts_));
}

TouchInput::~TouchInput() {
    if (fd_ >= 0) {
        ioctl(fd_, EVIOCGRAB, 0);
        close(fd_);
    }
}

void TouchInput::open_device() {
    for (unsigned number = 0; number < 16U && fd_ < 0; ++number) {
        char path[64];
        std::snprintf(path, sizeof(path), "/dev/input/event%u", number);
        int candidate = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (candidate < 0)
            continue;
        char name[128] = { 0 };
        if (ioctl(candidate, EVIOCGNAME(sizeof(name)), name) >= 0 &&
            std::strcmp(name, "cyttsp5_mt") == 0) {
            fd_ = candidate;
        } else {
            close(candidate);
        }
    }
    if (fd_ < 0)
        throw std::runtime_error("cyttsp5_mt input device was not found");
    if (ioctl(fd_, EVIOCGABS(ABS_MT_POSITION_X), &x_info_) < 0 ||
        ioctl(fd_, EVIOCGABS(ABS_MT_POSITION_Y), &y_info_) < 0)
        throw std::runtime_error("cannot query touchscreen axes");
    if (ioctl(fd_, EVIOCGRAB, 1) < 0)
        throw std::runtime_error(std::string("cannot grab touchscreen: ") +
                                 std::strerror(errno));
}

int TouchInput::normalize(int value, const input_absinfo &axis,
                          unsigned extent) const {
    if (axis.maximum <= axis.minimum || extent == 0U)
        return 0;
    if (value < axis.minimum)
        value = axis.minimum;
    if (value > axis.maximum)
        value = axis.maximum;
    return static_cast<int>((static_cast<long long>(value - axis.minimum) *
                             (extent - 1U)) /
                            (axis.maximum - axis.minimum));
}

unsigned TouchInput::hit_test(int raw_x, int raw_y) const {
    int x;
    int y;
    const int raw_x_extent = x_info_.maximum - x_info_.minimum + 1;
    const int raw_y_extent = y_info_.maximum - y_info_.minimum + 1;
    if (raw_x_extent == static_cast<int>(screen_height_) &&
        raw_y_extent == static_cast<int>(screen_width_)) {
        /* Clara BW portrait: the panel is axis-swapped with X mirrored. */
        x = static_cast<int>(screen_width_) - 1 -
            normalize(raw_y, y_info_, screen_width_);
        y = normalize(raw_x, x_info_, screen_height_);
    } else {
        x = normalize(raw_x, x_info_, screen_width_);
        y = normalize(raw_y, y_info_, screen_height_);
    }

    const int dpad_x = static_cast<int>(screen_width_ * 21U / 100U);
    const int dpad_y = static_cast<int>(screen_height_ * 82U / 100U);
    const int dx = x - dpad_x;
    const int dy = y - dpad_y;
    const int dpad_extent = static_cast<int>(screen_width_ * 16U / 100U);
    const int dpad_half_width = static_cast<int>(screen_width_ * 6U / 100U);
    if (dx >= -dpad_extent && dx <= dpad_extent &&
        dy >= -dpad_extent && dy <= dpad_extent &&
        (std::abs(dx) <= dpad_half_width ||
         std::abs(dy) <= dpad_half_width)) {
        if (std::abs(dx) > std::abs(dy))
            return dx < 0 ? LEFT : RIGHT;
        return dy < 0 ? UP : DOWN;
    }

    const int radius = static_cast<int>(screen_width_ * 9U / 100U);
    if (inside_circle(x, y, static_cast<int>(screen_width_ * 85U / 100U),
                      static_cast<int>(screen_height_ * 76U / 100U), radius))
        return A;
    if (inside_circle(x, y, static_cast<int>(screen_width_ * 70U / 100U),
                      static_cast<int>(screen_height_ * 84U / 100U), radius))
        return B;

    const int menu_y = static_cast<int>(screen_height_ * 94U / 100U);
    const int menu_half_width = static_cast<int>(screen_width_ * 8U / 100U);
    const int menu_half_height = static_cast<int>(screen_height_ * 3U / 100U);
    if (std::abs(y - menu_y) <= menu_half_height) {
        if (std::abs(x - static_cast<int>(screen_width_ * 43U / 100U)) <=
            menu_half_width)
            return SELECT;
        if (std::abs(x - static_cast<int>(screen_width_ * 60U / 100U)) <=
            menu_half_width)
            return START;
    }
    return 0U;
}

void TouchInput::handle(const input_event &event) {
    if (event.type == EV_KEY &&
        (event.code == BTN_TOUCH || event.code == BTN_TOOL_FINGER) &&
        event.value == 0) {
        /*
         * cyttsp5 emits a global finger-up key report in addition to its
         * slot data.  Treat it as authoritative so a missing/stale slot
         * release cannot leave a Game Boy button held indefinitely.
         */
        for (unsigned slot = 0; slot < 16U; ++slot)
            contacts_[slot].active = false;
    } else if (event.type == EV_ABS) {
        if (event.code == ABS_MT_SLOT) {
            if (event.value >= 0 && event.value < 16)
                current_slot_ = static_cast<unsigned>(event.value);
        } else if (event.code == ABS_MT_TRACKING_ID) {
            contacts_[current_slot_].active = event.value >= 0;
        } else if (event.code == ABS_MT_POSITION_X) {
            contacts_[current_slot_].x = event.value;
        } else if (event.code == ABS_MT_POSITION_Y) {
            contacts_[current_slot_].y = event.value;
        }
    } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
        update_buttons();
    }
}

void TouchInput::update_buttons() {
    unsigned updated = 0U;
    for (unsigned slot = 0; slot < 16U; ++slot) {
        if (contacts_[slot].active)
            updated |= hit_test(contacts_[slot].x, contacts_[slot].y);
    }
    if ((updated & (LEFT | RIGHT)) == (LEFT | RIGHT))
        updated &= ~(LEFT | RIGHT);
    if ((updated & (UP | DOWN)) == (UP | DOWN))
        updated &= ~(UP | DOWN);

    const unsigned last_queued = pending_states_.empty()
        ? physical_buttons_ : pending_states_.back();
    physical_buttons_ = updated;
    if (updated != last_queued)
        pending_states_.push_back(updated);
}

void TouchInput::poll() {
    for (;;) {
        input_event event;
        const ssize_t bytes = read(fd_, &event, sizeof(event));
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            if (errno == EINTR)
                continue;
            throw std::runtime_error(std::string("touchscreen read failed: ") +
                                     std::strerror(errno));
        }
        if (bytes == 0)
            return;
        if (bytes != static_cast<ssize_t>(sizeof(event)))
            throw std::runtime_error("short touchscreen event read");
        handle(event);
    }
}

void TouchInput::advance_frame() {
    poll();
    if (!pending_states_.empty()) {
        buttons_ = pending_states_.front();
        pending_states_.pop_front();
    } else {
        buttons_ = physical_buttons_;
    }
}

} // namespace gambatte_kobo
