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

namespace koboy {
namespace {

bool inside_circle(int x, int y, int center_x, int center_y, int radius) {
    const int dx = x - center_x;
    const int dy = y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}

} // namespace

TouchInput::TouchInput(unsigned screen_width, unsigned screen_height)
    : fd_(-1), screen_width_(screen_width), screen_height_(screen_height),
      current_slot_(0U), physical_buttons_(0U), buttons_(0U),
      ui_touch_active_(false), suppress_gameplay_until_release_(false),
      shoulder_buttons_enabled_(false) {
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

void TouchInput::screen_position(int raw_x, int raw_y, int &x, int &y) const {
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
}

unsigned TouchInput::hit_test(int raw_x, int raw_y) const {
    int x;
    int y;
    screen_position(raw_x, raw_y, x, y);

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
            return dx < 0 ? CORE_BUTTON_LEFT : CORE_BUTTON_RIGHT;
        return dy < 0 ? CORE_BUTTON_UP : CORE_BUTTON_DOWN;
    }

    const int radius = static_cast<int>(screen_width_ * 9U / 100U);
    if (inside_circle(x, y, static_cast<int>(screen_width_ * 85U / 100U),
                      static_cast<int>(screen_height_ * 76U / 100U), radius))
        return CORE_BUTTON_A;
    if (inside_circle(x, y, static_cast<int>(screen_width_ * 70U / 100U),
                      static_cast<int>(screen_height_ * 84U / 100U), radius))
        return CORE_BUTTON_B;
    if (shoulder_buttons_enabled_) {
        const int shoulder_radius =
            static_cast<int>(screen_width_ * 5U / 100U);
        const int shoulder_y =
            static_cast<int>(screen_height_ * 64U / 100U);
        if (inside_circle(x, y,
                          static_cast<int>(screen_width_ * 70U / 100U),
                          shoulder_y, shoulder_radius))
            return CORE_BUTTON_L;
        if (inside_circle(x, y,
                          static_cast<int>(screen_width_ * 85U / 100U),
                          shoulder_y, shoulder_radius))
            return CORE_BUTTON_R;
    }

    const int menu_y = static_cast<int>(screen_height_ * 94U / 100U);
    const int menu_half_width = static_cast<int>(screen_width_ * 8U / 100U);
    const int menu_half_height = static_cast<int>(screen_height_ * 3U / 100U);
    if (std::abs(y - menu_y) <= menu_half_height) {
        if (std::abs(x - static_cast<int>(screen_width_ * 43U / 100U)) <=
            menu_half_width)
            return CORE_BUTTON_SELECT;
        if (std::abs(x - static_cast<int>(screen_width_ * 60U / 100U)) <=
            menu_half_width)
            return CORE_BUTTON_START;
    }
    return 0U;
}

UiAction TouchInput::ui_hit_test(int raw_x, int raw_y,
                                 UiScreen screen) const {
    int x;
    int y;
    screen_position(raw_x, raw_y, x, y);
    if (screen == UI_SCREEN_GAME) {
        const int center_x = static_cast<int>(screen_width_ * 52U / 100U);
        const int center_y = static_cast<int>(screen_height_ * 75U / 100U);
        const int half_width = static_cast<int>(screen_width_ * 8U / 100U);
        const int half_height = static_cast<int>(screen_height_ * 2U / 100U);
        return std::abs(x - center_x) <= half_width &&
               std::abs(y - center_y) <= half_height
            ? UI_OPEN_MENU : UI_NONE;
    }

    if (screen == UI_SCREEN_MENU || screen == UI_SCREEN_MENU_DEVELOPER) {
        const int left = static_cast<int>(screen_width_ * 18U / 100U);
        const int right = static_cast<int>(screen_width_ * 82U / 100U);
        const int half_height = static_cast<int>(screen_height_ * 3U / 100U);
        if (x < left || x > right)
            return UI_NONE;
        const unsigned resume_y = 27U;
        const unsigned state_y = 37U;
        const unsigned settings_y = 47U;
        const unsigned volume_y = 57U;
        const unsigned reset_y = 67U;
        const unsigned select_y = 77U;
        const unsigned exit_y = 87U;
        if (std::abs(y - static_cast<int>(screen_height_ * resume_y / 100U)) <=
            half_height)
            return UI_RESUME_GAME;
        if (std::abs(y - static_cast<int>(screen_height_ * state_y / 100U)) <=
            half_height)
            return x < static_cast<int>(screen_width_ / 2U)
                ? UI_LOAD_STATE : UI_SAVE_STATE;
        if (std::abs(y - static_cast<int>(screen_height_ * reset_y / 100U)) <=
            half_height)
            return UI_RESET_GAME;
        if (std::abs(y - static_cast<int>(screen_height_ * select_y / 100U)) <=
            half_height)
            return UI_SELECT_GAME;
        if (std::abs(y - static_cast<int>(screen_height_ * settings_y / 100U)) <=
            half_height)
            return UI_DISPLAY_SETTINGS;
        if (std::abs(y - static_cast<int>(screen_height_ * volume_y / 100U)) <=
            half_height)
            return x < static_cast<int>(screen_width_ / 2U)
                ? UI_VOLUME_DOWN : UI_VOLUME_UP;
        if (std::abs(y - static_cast<int>(screen_height_ * exit_y / 100U)) <=
            half_height)
            return UI_EXIT_TO_NICKEL;
        return UI_NONE;
    }

    if (screen == UI_SCREEN_DISPLAY_SETTINGS ||
        screen == UI_SCREEN_DISPLAY_SETTINGS_DEVELOPER) {
        const int left = static_cast<int>(screen_width_ * 8U / 100U);
        const int right = static_cast<int>(screen_width_ * 92U / 100U);
        const bool developer =
            screen == UI_SCREEN_DISPLAY_SETTINGS_DEVELOPER;
        const int half_height = static_cast<int>(screen_height_ *
            (developer ? 3U : 4U) / 100U);
        if (x < left || x > right)
            return UI_NONE;
        const unsigned developer_centers[] = {27U, 43U, 59U, 75U};
        const UiAction developer_actions[] = {
            UI_SETTING_FULL_REFRESH, UI_SETTING_GRID_EFFECT, UI_SETTING_BICHROME,
            UI_SETTING_TARGET_REFRESH
        };
        const unsigned user_centers[] = {36U, 60U};
        const UiAction user_actions[] = {
            UI_SETTING_FULL_REFRESH, UI_SETTING_GRID_EFFECT
        };
        const unsigned *centers = developer ? developer_centers : user_centers;
        const UiAction *actions = developer ? developer_actions : user_actions;
        const unsigned count = developer ? 4U : 2U;
        for (unsigned i = 0; i < count; ++i)
            if (std::abs(y - static_cast<int>(screen_height_ * centers[i] /
                                              100U)) <= half_height)
                return actions[i];
        if (y >= static_cast<int>(screen_height_ * 86U / 100U))
            return UI_LIST_CANCEL;
        return UI_NONE;
    }

    const int list_left = static_cast<int>(screen_width_ * 6U / 100U);
    const int list_right = static_cast<int>(screen_width_ * 94U / 100U);
    const int row_top = static_cast<int>(screen_height_ * 16U / 100U);
    const int row_height = static_cast<int>(screen_height_ * 9U / 100U);
    if (x >= list_left && x <= list_right && y >= row_top &&
        y < row_top + 7 * row_height) {
        return static_cast<UiAction>(UI_GAME_0 + (y - row_top) / row_height);
    }
    const int footer_y = static_cast<int>(screen_height_ * 91U / 100U);
    const int footer_half_height = static_cast<int>(screen_height_ * 5U / 100U);
    if (std::abs(y - footer_y) <= footer_half_height) {
        if (x < static_cast<int>(screen_width_ / 3U))
            return UI_LIST_PREVIOUS;
        if (x < static_cast<int>(screen_width_ * 2U / 3U))
            return UI_LIST_CANCEL;
        return UI_LIST_NEXT;
    }
    return UI_NONE;
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
    bool touching = false;
    for (unsigned slot = 0; slot < 16U; ++slot)
        touching = touching || contacts_[slot].active;
    if (suppress_gameplay_until_release_) {
        physical_buttons_ = 0U;
        buttons_ = 0U;
        pending_states_.clear();
        if (!touching)
            suppress_gameplay_until_release_ = false;
        return;
    }

    unsigned updated = 0U;
    for (unsigned slot = 0; slot < 16U; ++slot) {
        if (contacts_[slot].active)
            updated |= hit_test(contacts_[slot].x, contacts_[slot].y);
    }
    if ((updated & (CORE_BUTTON_LEFT | CORE_BUTTON_RIGHT)) ==
        (CORE_BUTTON_LEFT | CORE_BUTTON_RIGHT))
        updated &= ~(CORE_BUTTON_LEFT | CORE_BUTTON_RIGHT);
    if ((updated & (CORE_BUTTON_UP | CORE_BUTTON_DOWN)) ==
        (CORE_BUTTON_UP | CORE_BUTTON_DOWN))
        updated &= ~(CORE_BUTTON_UP | CORE_BUTTON_DOWN);

    const unsigned last_queued = pending_states_.empty()
        ? physical_buttons_ : pending_states_.back();
    physical_buttons_ = updated;
    if (updated != last_queued)
        pending_states_.push_back(updated);
}

void TouchInput::poll() {
    if (fd_ < 0)
        return;
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

void TouchInput::suppress_input_until_release() {
    poll();
    physical_buttons_ = 0U;
    buttons_ = 0U;
    pending_states_.clear();
    bool touching = false;
    for (unsigned slot = 0; slot < 16U; ++slot)
        touching = touching || contacts_[slot].active;
    suppress_gameplay_until_release_ = touching;
    ui_touch_active_ = touching;
}

void TouchInput::wait_for_release() {
    suppress_input_until_release();
    while (ui_touch_active_) {
        usleep(10000);
        poll();
        ui_touch_active_ = false;
        for (unsigned slot = 0; slot < 16U; ++slot)
            if (contacts_[slot].active) {
                ui_touch_active_ = true;
                break;
            }
    }
    suppress_input_until_release();
}

UiAction TouchInput::take_ui_action(UiScreen screen) {
    poll();
    bool touching = false;
    UiAction action = UI_NONE;
    for (unsigned slot = 0; slot < 16U; ++slot) {
        if (!contacts_[slot].active)
            continue;
        touching = true;
        if (action == UI_NONE)
            action = ui_hit_test(contacts_[slot].x, contacts_[slot].y, screen);
    }
    if (ui_touch_active_ || !touching)
        action = UI_NONE;
    ui_touch_active_ = touching;
    return action;
}

} // namespace koboy
