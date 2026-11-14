/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_TOUCH_INPUT_H
#define NICKELSPERIMENTS_TOUCH_INPUT_H

#include <gambatte.h>
#include <linux/input.h>
#include <deque>

namespace gambatte_kobo {

class TouchInput : public gambatte::InputGetter {
public:
    TouchInput(unsigned screen_width, unsigned screen_height);
    ~TouchInput();

    void open_device();
    void advance_frame();
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
    std::deque<unsigned> pending_states_;
};

} // namespace gambatte_kobo

#endif
