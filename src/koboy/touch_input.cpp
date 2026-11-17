/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "touch_input.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

namespace koboy {
namespace {

bool inside_circle(int x, int y, int center_x, int center_y, int radius) {
    const int dx = x - center_x;
    const int dy = y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}

double monotonic_seconds() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec) +
           static_cast<double>(now.tv_nsec) / 1000000000.0;
}

bool bit_set(const unsigned long *bits, unsigned bit) {
    const unsigned bits_per_word = sizeof(unsigned long) * 8U;
    return (bits[bit / bits_per_word] &
            (1UL << (bit % bits_per_word))) != 0UL;
}

int axis_direction(int value, const input_absinfo &axis) {
    if (axis.maximum <= axis.minimum)
        return 0;
    const int center = axis.minimum + (axis.maximum - axis.minimum) / 2;
    const int deadzone = std::max(axis.flat,
        (axis.maximum - axis.minimum) / 4);
    if (value < center - deadzone)
        return -1;
    if (value > center + deadzone)
        return 1;
    return 0;
}

} // namespace

TouchInput::TouchInput(unsigned screen_width, unsigned screen_height)
    : fd_(-1), controller_fd_(-1), screen_width_(screen_width),
      screen_height_(screen_height),
      current_slot_(0U), slot_protocol_(false), physical_buttons_(0U),
      controller_key_buttons_(0U), controller_physical_buttons_(0U),
      controller_x_(0), controller_y_(0), controller_hat_x_(0),
      controller_hat_y_(0), next_controller_scan_(0.0), buttons_(0U),
      ui_touch_active_(false), suppress_gameplay_until_release_(false),
      shoulder_buttons_enabled_(false), disk_controls_enabled_(false),
      audio_controls_enabled_(false), core_settings_enabled_(false),
      diagnostics_(false), suppress_controller_until_release_(false) {
    std::memset(&x_info_, 0, sizeof(x_info_));
    std::memset(&y_info_, 0, sizeof(y_info_));
    std::memset(&controller_x_info_, 0, sizeof(controller_x_info_));
    std::memset(&controller_y_info_, 0, sizeof(controller_y_info_));
    std::memset(contacts_, 0, sizeof(contacts_));
}

TouchInput::~TouchInput() {
    if (fd_ >= 0) {
        ioctl(fd_, EVIOCGRAB, 0);
        close(fd_);
    }
    if (controller_fd_ >= 0)
        close(controller_fd_);
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
    input_absinfo slot_info;
    std::memset(&slot_info, 0, sizeof(slot_info));
    slot_protocol_ = ioctl(fd_, EVIOCGABS(ABS_MT_SLOT), &slot_info) == 0;
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
        dy >= -dpad_extent && dy <= dpad_extent) {
        const int diagonal_threshold = dpad_half_width / 2;
        unsigned directions = 0U;
        if (dx < -diagonal_threshold)
            directions |= CORE_BUTTON_LEFT;
        else if (dx > diagonal_threshold)
            directions |= CORE_BUTTON_RIGHT;
        if (dy < -diagonal_threshold)
            directions |= CORE_BUTTON_UP;
        else if (dy > diagonal_threshold)
            directions |= CORE_BUTTON_DOWN;
        const unsigned horizontal = CORE_BUTTON_LEFT | CORE_BUTTON_RIGHT;
        const unsigned vertical = CORE_BUTTON_UP | CORE_BUTTON_DOWN;
        if ((directions & horizontal) && (directions & vertical)) {
            const int absolute_x = std::abs(dx);
            const int absolute_y = std::abs(dy);
            /*
             * Keep single-touch diagonals around the corner bisectors, but
             * let a clearly dominant axis claim the surrounding area.  This
             * makes grid movement less prone to accidental diagonal steps.
             */
            if (absolute_x * 5 < absolute_y * 3)
                directions &= ~horizontal;
            else if (absolute_y * 5 < absolute_x * 3)
                directions &= ~vertical;
        }
        return directions;
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
        const unsigned extras = (disk_controls_enabled_ ? 1U : 0U) +
                                (audio_controls_enabled_ ? 1U : 0U) +
                                (core_settings_enabled_ ? 1U : 0U);
        const unsigned count = 6U + extras;
        unsigned row = 0U;
#define MENU_Y() (24U + row++ * 63U / (count - 1U))
        const unsigned resume_y = MENU_Y();
        const unsigned state_y = MENU_Y();
        const unsigned display_y = MENU_Y();
        const unsigned core_y = core_settings_enabled_ ? MENU_Y() : 0U;
        const unsigned volume_y = audio_controls_enabled_ ? MENU_Y() : 0U;
        const unsigned disk_y = disk_controls_enabled_ ? MENU_Y() : 0U;
        const unsigned reset_y = MENU_Y();
        const unsigned select_y = MENU_Y();
        const unsigned exit_y = MENU_Y();
#undef MENU_Y
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
        if (std::abs(y - static_cast<int>(screen_height_ * display_y / 100U)) <=
            half_height)
            return UI_DISPLAY_SETTINGS;
        if (core_settings_enabled_ &&
            std::abs(y - static_cast<int>(screen_height_ * core_y / 100U)) <=
                half_height)
            return UI_GAME_SETTINGS;
        if (audio_controls_enabled_ &&
            std::abs(y - static_cast<int>(screen_height_ * volume_y / 100U)) <=
            half_height)
            return x < static_cast<int>(screen_width_ / 2U)
                ? UI_VOLUME_DOWN : UI_VOLUME_UP;
        if (disk_controls_enabled_ &&
            std::abs(y - static_cast<int>(screen_height_ * disk_y / 100U)) <=
                half_height)
            return UI_TOGGLE_DISK;
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
        const unsigned developer_centers[] = {25U, 42U, 59U, 76U};
        const UiAction developer_actions[] = {
            UI_SETTING_FULL_REFRESH, UI_SETTING_SCREEN_EFFECT,
            UI_SETTING_TARGET_REFRESH, UI_SETTING_FPS_COUNTER
        };
        const unsigned user_centers[] = {36U, 60U};
        const UiAction user_actions[] = {
            UI_SETTING_FULL_REFRESH, UI_SETTING_SCREEN_EFFECT
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

    if (screen == UI_SCREEN_CORE_SETTINGS) {
        const int left = static_cast<int>(screen_width_ * 8U / 100U);
        const int right = static_cast<int>(screen_width_ * 92U / 100U);
        const int row_top = static_cast<int>(screen_height_ * 18U / 100U);
        const int row_height = static_cast<int>(screen_height_ * 15U / 100U);
        if (x >= left && x <= right && y >= row_top &&
            y < row_top + 4 * row_height)
            return static_cast<UiAction>(UI_GAME_0 +
                                         (y - row_top) / row_height);
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
        for (unsigned slot = 0; slot < 16U; ++slot) {
            contacts_[slot].active = false;
            contacts_[slot].primary_face_button = 0U;
        }
    } else if (event.type == EV_ABS) {
        if (event.code == ABS_MT_SLOT) {
            if (event.value >= 0 && event.value < 16)
                current_slot_ = static_cast<unsigned>(event.value);
        } else if (event.code == ABS_MT_TRACKING_ID) {
            Contact &contact = contacts_[current_slot_];
            if (event.value < 0) {
                contact.active = false;
                contact.primary_face_button = 0U;
            } else {
                if (!contact.active || contact.tracking_id != event.value)
                    contact.primary_face_button = 0U;
                contact.active = true;
                contact.tracking_id = event.value;
            }
        } else if (event.code == ABS_MT_POSITION_X) {
            contacts_[current_slot_].x = event.value;
        } else if (event.code == ABS_MT_POSITION_Y) {
            contacts_[current_slot_].y = event.value;
        }
    } else if (event.type == EV_SYN && event.code == SYN_MT_REPORT) {
        /*
         * Protocol-A devices describe each contact in sequence and separate
         * them with SYN_MT_REPORT.  Some cyttsp5 kernels misleadingly expose
         * ABS_MT_SLOT as a capability while never emitting slot events, so
         * the observed report format must override the capability probe.
         */
        slot_protocol_ = false;
        if (current_slot_ < 16U)
            ++current_slot_;
    } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
        if (!slot_protocol_) {
            for (unsigned slot = current_slot_; slot < 16U; ++slot) {
                contacts_[slot].active = false;
                contacts_[slot].primary_face_button = 0U;
            }
        }
        update_buttons();
        if (!slot_protocol_)
            current_slot_ = 0U;
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
    const unsigned face_buttons = CORE_BUTTON_A | CORE_BUTTON_B;
    for (unsigned slot = 0; slot < 16U; ++slot) {
        Contact &contact = contacts_[slot];
        if (contact.active) {
            const unsigned hit = hit_test(contact.x, contact.y);
            const unsigned current_face_buttons = hit & face_buttons;
            if (contact.primary_face_button == 0U &&
                current_face_buttons != 0U)
                contact.primary_face_button = current_face_buttons;
            updated |= (hit & ~face_buttons) |
                       current_face_buttons |
                       contact.primary_face_button;
        }
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

void TouchInput::maybe_open_controller() {
    if (controller_fd_ >= 0)
        return;
    const double now = monotonic_seconds();
    if (now < next_controller_scan_)
        return;
    next_controller_scan_ = now + 1.0;

    const unsigned bits_per_word = sizeof(unsigned long) * 8U;
    for (unsigned number = 0; number < 32U; ++number) {
        char path[64];
        std::snprintf(path, sizeof(path), "/dev/input/event%u", number);
        int candidate = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (candidate < 0)
            continue;
        char name[128] = { 0 };
        ioctl(candidate, EVIOCGNAME(sizeof(name)), name);
        if (std::strcmp(name, "cyttsp5_mt") == 0) {
            close(candidate);
            continue;
        }
        unsigned long event_bits[(EV_MAX + bits_per_word) / bits_per_word];
        unsigned long key_bits[(KEY_MAX + bits_per_word) / bits_per_word];
        unsigned long abs_bits[(ABS_MAX + bits_per_word) / bits_per_word];
        std::memset(event_bits, 0, sizeof(event_bits));
        std::memset(key_bits, 0, sizeof(key_bits));
        std::memset(abs_bits, 0, sizeof(abs_bits));
        ioctl(candidate, EVIOCGBIT(0, sizeof(event_bits)), event_bits);
        if (!bit_set(event_bits, EV_KEY) || !bit_set(event_bits, EV_ABS)) {
            close(candidate);
            continue;
        }
        ioctl(candidate, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
        ioctl(candidate, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);
        const bool face_buttons = bit_set(key_bits, BTN_A) &&
                                  bit_set(key_bits, BTN_B);
        const bool directions =
            (bit_set(abs_bits, ABS_X) && bit_set(abs_bits, ABS_Y)) ||
            (bit_set(abs_bits, ABS_HAT0X) &&
             bit_set(abs_bits, ABS_HAT0Y));
        if (!face_buttons || !directions) {
            close(candidate);
            continue;
        }

        controller_fd_ = candidate;
        controller_path_ = path;
        controller_key_buttons_ = 0U;
        controller_physical_buttons_ = 0U;
        controller_hat_x_ = 0;
        controller_hat_y_ = 0;
        std::memset(&controller_x_info_, 0, sizeof(controller_x_info_));
        std::memset(&controller_y_info_, 0, sizeof(controller_y_info_));
        if (ioctl(controller_fd_, EVIOCGABS(ABS_X),
                  &controller_x_info_) == 0)
            controller_x_ = controller_x_info_.value;
        if (ioctl(controller_fd_, EVIOCGABS(ABS_Y),
                  &controller_y_info_) == 0)
            controller_y_ = controller_y_info_.value;
        input_id identity;
        std::memset(&identity, 0, sizeof(identity));
        ioctl(controller_fd_, EVIOCGID, &identity);
        if (diagnostics_)
            std::cerr << "controller_connected path=" << controller_path_
                      << " name=" << name << " vendor=" << std::hex
                      << identity.vendor << " product=" << identity.product
                      << std::dec << '\n';
        update_controller_buttons();
        return;
    }
}

void TouchInput::close_controller(const char *reason) {
    if (controller_fd_ < 0)
        return;
    if (diagnostics_)
        std::cerr << "controller_disconnected path=" << controller_path_
                  << " reason=" << reason << '\n';
    close(controller_fd_);
    controller_fd_ = -1;
    controller_path_.clear();
    controller_key_buttons_ = 0U;
    controller_x_ = 0;
    controller_y_ = 0;
    controller_hat_x_ = 0;
    controller_hat_y_ = 0;
    suppress_controller_until_release_ = false;
    if (controller_physical_buttons_ != 0U)
        controller_pending_states_.push_back(0U);
    controller_physical_buttons_ = 0U;
    next_controller_scan_ = monotonic_seconds() + 1.0;
}

void TouchInput::handle_controller(const input_event &event) {
    if (event.type == EV_KEY) {
        unsigned mapped = 0U;
        switch (event.code) {
            /* Switch mode reports the Nintendo-labelled face layout. */
            case BTN_A: mapped = CORE_BUTTON_B; break;
            case BTN_B: mapped = CORE_BUTTON_A; break;
            case BTN_C: mapped = CORE_BUTTON_X; break; /* physical Y */
            case BTN_X: mapped = CORE_BUTTON_Y; break; /* physical X */
            case BTN_Y: mapped = CORE_BUTTON_L; break; /* physical L1 */
            case BTN_Z: mapped = CORE_BUTTON_R; break; /* physical R1 */
            case BTN_TL2: mapped = CORE_BUTTON_SELECT; break; /* minus */
            case BTN_TR2: mapped = CORE_BUTTON_START; break; /* plus */
            default:
                if (diagnostics_ && event.value != 2)
                    std::cerr << "controller_key code=" << event.code
                              << " value=" << event.value << '\n';
                return;
        }
        if (event.value)
            controller_key_buttons_ |= mapped;
        else
            controller_key_buttons_ &= ~mapped;
    } else if (event.type == EV_ABS) {
        if (event.code == ABS_X)
            controller_x_ = event.value;
        else if (event.code == ABS_Y)
            controller_y_ = event.value;
        else if (event.code == ABS_HAT0X)
            controller_hat_x_ = event.value;
        else if (event.code == ABS_HAT0Y)
            controller_hat_y_ = event.value;
    } else if (event.type == EV_MSC && diagnostics_) {
        std::cerr << "controller_scan code=" << event.code
                  << " value=" << std::hex << event.value << std::dec << '\n';
    } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
        update_controller_buttons();
    }
}

void TouchInput::update_controller_buttons() {
    unsigned updated = controller_key_buttons_;
    int horizontal = controller_hat_x_;
    int vertical = controller_hat_y_;
    if (!horizontal)
        horizontal = axis_direction(controller_x_, controller_x_info_);
    if (!vertical)
        vertical = axis_direction(controller_y_, controller_y_info_);
    if (horizontal < 0)
        updated |= CORE_BUTTON_LEFT;
    else if (horizontal > 0)
        updated |= CORE_BUTTON_RIGHT;
    if (vertical < 0)
        updated |= CORE_BUTTON_UP;
    else if (vertical > 0)
        updated |= CORE_BUTTON_DOWN;

    if (suppress_controller_until_release_) {
        controller_physical_buttons_ = 0U;
        controller_pending_states_.clear();
        if (updated == 0U)
            suppress_controller_until_release_ = false;
        return;
    }
    const unsigned last_queued = controller_pending_states_.empty()
        ? controller_physical_buttons_ : controller_pending_states_.back();
    controller_physical_buttons_ = updated;
    if (updated != last_queued)
        controller_pending_states_.push_back(updated);
}

void TouchInput::poll_controller() {
    maybe_open_controller();
    if (controller_fd_ < 0)
        return;
    for (;;) {
        input_event event;
        const ssize_t bytes = read(controller_fd_, &event, sizeof(event));
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            if (errno == EINTR)
                continue;
            close_controller(std::strerror(errno));
            return;
        }
        if (bytes == 0) {
            close_controller("end of file");
            return;
        }
        if (bytes != static_cast<ssize_t>(sizeof(event))) {
            close_controller("short event read");
            return;
        }
        handle_controller(event);
    }
}

void TouchInput::poll() {
    if (fd_ >= 0) for (;;) {
        input_event event;
        const ssize_t bytes = read(fd_, &event, sizeof(event));
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            throw std::runtime_error(std::string("touchscreen read failed: ") +
                                     std::strerror(errno));
        }
        if (bytes == 0)
            break;
        if (bytes != static_cast<ssize_t>(sizeof(event)))
            throw std::runtime_error("short touchscreen event read");
        handle(event);
    }
    poll_controller();
}

void TouchInput::advance_frame() {
    poll();
    unsigned touch_buttons = physical_buttons_;
    if (!pending_states_.empty()) {
        touch_buttons = pending_states_.front();
        pending_states_.pop_front();
    }
    unsigned controller_buttons = controller_physical_buttons_;
    if (!controller_pending_states_.empty()) {
        controller_buttons = controller_pending_states_.front();
        controller_pending_states_.pop_front();
    }
    buttons_ = touch_buttons | controller_buttons;
    if ((buttons_ & (CORE_BUTTON_LEFT | CORE_BUTTON_RIGHT)) ==
        (CORE_BUTTON_LEFT | CORE_BUTTON_RIGHT))
        buttons_ &= ~(CORE_BUTTON_LEFT | CORE_BUTTON_RIGHT);
    if ((buttons_ & (CORE_BUTTON_UP | CORE_BUTTON_DOWN)) ==
        (CORE_BUTTON_UP | CORE_BUTTON_DOWN))
        buttons_ &= ~(CORE_BUTTON_UP | CORE_BUTTON_DOWN);
}

void TouchInput::suppress_input_until_release() {
    poll();
    physical_buttons_ = 0U;
    controller_physical_buttons_ = 0U;
    buttons_ = 0U;
    pending_states_.clear();
    controller_pending_states_.clear();
    bool touching = false;
    for (unsigned slot = 0; slot < 16U; ++slot)
        touching = touching || contacts_[slot].active;
    suppress_gameplay_until_release_ = touching;
    suppress_controller_until_release_ = controller_key_buttons_ != 0U ||
        axis_direction(controller_x_, controller_x_info_) != 0 ||
        axis_direction(controller_y_, controller_y_info_) != 0 ||
        controller_hat_x_ != 0 || controller_hat_y_ != 0;
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
