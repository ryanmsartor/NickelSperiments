/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NICKELSPERIMENTS_KOBO_SESSION_H
#define NICKELSPERIMENTS_KOBO_SESSION_H

#include <sys/types.h>

namespace gambatte_kobo {

class NickelSession {
public:
    NickelSession();
    ~NickelSession();

    void suspend();
    void resume();

private:
    NickelSession(const NickelSession &);
    NickelSession &operator=(const NickelSession &);
    pid_t nickel_pid_;
    bool suspended_;
};

} // namespace gambatte_kobo

#endif
