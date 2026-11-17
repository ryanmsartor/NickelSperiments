/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_TOUCH_INPUT_H
#define NICKELSPERIMENTS_TOUCH_INPUT_H

#include "emulator_core.h"
#include <linux/input.h>
#include <deque>

namespace koboy {

enum UiAction {
    UI_NONE,
    UI_OPEN_MENU,
    UI_RESUME_GAME,
    UI_SAVE_STATE,
    UI_LOAD_STATE,
    UI_RESET_GAME,
    UI_SELECT_GAME,
    UI_DISPLAY_SETTINGS,
    UI_VOLUME_DOWN,
    UI_VOLUME_UP,
    UI_TOGGLE_DISK,
    UI_EXIT_TO_NICKEL,
    UI_SETTING_FULL_REFRESH,
    UI_SETTING_SCREEN_EFFECT,
    UI_SETTING_TARGET_REFRESH,
    UI_SETTING_FPS_COUNTER,
    UI_LIST_PREVIOUS,
    UI_LIST_NEXT,
    UI_LIST_CANCEL,
    UI_GAME_0,
    UI_GAME_1,
    UI_GAME_2,
    UI_GAME_3,
    UI_GAME_4,
    UI_GAME_5,
    UI_GAME_6
};

enum UiScreen {
    UI_SCREEN_GAME,
    UI_SCREEN_MENU,
    UI_SCREEN_MENU_DEVELOPER,
    UI_SCREEN_DISPLAY_SETTINGS,
    UI_SCREEN_DISPLAY_SETTINGS_DEVELOPER,
    UI_SCREEN_GAME_LIST
};

class TouchInput {
public:
    TouchInput(unsigned screen_width, unsigned screen_height);
    ~TouchInput();

    void open_device();
    void advance_frame();
    void suppress_input_until_release();
    void wait_for_release();
    void set_shoulder_buttons_enabled(bool enabled) {
        shoulder_buttons_enabled_ = enabled;
    }
    void set_disk_controls_enabled(bool enabled) {
        disk_controls_enabled_ = enabled;
    }
    void set_audio_controls_enabled(bool enabled) {
        audio_controls_enabled_ = enabled;
    }
    UiAction take_ui_action(UiScreen screen);
    unsigned buttons() const { return buttons_; }

    int x_min() const { return x_info_.minimum; }
    int x_max() const { return x_info_.maximum; }
    int y_min() const { return y_info_.minimum; }
    int y_max() const { return y_info_.maximum; }

private:
    struct Contact {
        bool active;
        int x;
        int y;
        int tracking_id;
        unsigned primary_face_button;
    };

    TouchInput(const TouchInput &);
    TouchInput &operator=(const TouchInput &);

    void poll();
    void handle(const input_event &event);
    void update_buttons();
    unsigned hit_test(int raw_x, int raw_y) const;
    UiAction ui_hit_test(int raw_x, int raw_y, UiScreen screen) const;
    void screen_position(int raw_x, int raw_y, int &x, int &y) const;
    int normalize(int value, const input_absinfo &axis, unsigned extent) const;

    int fd_;
    unsigned screen_width_;
    unsigned screen_height_;
    input_absinfo x_info_;
    input_absinfo y_info_;
    Contact contacts_[16];
    unsigned current_slot_;
    bool slot_protocol_;
    unsigned physical_buttons_;
    unsigned buttons_;
    bool ui_touch_active_;
    bool suppress_gameplay_until_release_;
    bool shoulder_buttons_enabled_;
    bool disk_controls_enabled_;
    bool audio_controls_enabled_;
    std::deque<unsigned> pending_states_;
};

} // namespace koboy

#endif
