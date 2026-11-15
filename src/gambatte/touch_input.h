/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_TOUCH_INPUT_H
#define NICKELSPERIMENTS_TOUCH_INPUT_H

#include <gambatte.h>
#include <linux/input.h>
#include <deque>

namespace gambatte_kobo {

enum UiAction {
    UI_NONE,
    UI_OPEN_MENU,
    UI_RESUME_GAME,
    UI_SELECT_GAME,
    UI_REFRESH_SETTINGS,
    UI_EXIT_TO_NICKEL,
    UI_SETTING_FAST_TYPE,
    UI_SETTING_SLOW_TYPE,
    UI_SETTING_SLOWEST_TYPE,
    UI_SETTING_FAST_FREQUENCY,
    UI_SETTING_SLOW_FREQUENCY,
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
    UI_SCREEN_REFRESH_SETTINGS,
    UI_SCREEN_GAME_LIST
};

class TouchInput : public gambatte::InputGetter {
public:
    TouchInput(unsigned screen_width, unsigned screen_height);
    ~TouchInput();

    void open_device();
    void advance_frame();
    void suppress_ui_input_until_release();
    UiAction take_ui_action(UiScreen screen);
    unsigned operator()() { return buttons_; }

    int x_min() const { return x_info_.minimum; }
    int x_max() const { return x_info_.maximum; }
    int y_min() const { return y_info_.minimum; }
    int y_max() const { return y_info_.maximum; }

private:
    struct Contact {
        bool active;
        int x;
        int y;
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
    unsigned physical_buttons_;
    unsigned buttons_;
    bool ui_touch_active_;
    std::deque<unsigned> pending_states_;
};

} // namespace gambatte_kobo

#endif
