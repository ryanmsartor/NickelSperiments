/*
 * Minimal userspace ABI from MediaTek's linux/hwtcon_ioctl_cmd.h.
 * Source: Kobo mt8113 kernel hwtcon_ioctl_cmd.h.
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef NICKELSPERIMENTS_KOBO_HWTCON_H
#define NICKELSPERIMENTS_KOBO_HWTCON_H

#include <stdint.h>
#include <sys/ioctl.h>

enum {
    HWTCON_UPDATE_PARTIAL = 0,
    HWTCON_UPDATE_FULL = 1,
    HWTCON_WAVEFORM_DU = 1,
    HWTCON_WAVEFORM_GC16 = 2,
    HWTCON_WAVEFORM_A2 = 6
};

struct hwtcon_rect {
    uint32_t top;
    uint32_t left;
    uint32_t width;
    uint32_t height;
};

struct hwtcon_update_data {
    hwtcon_rect update_region;
    uint32_t waveform_mode;
    uint32_t update_mode;
    uint32_t update_marker;
    unsigned int flags;
    int dither_mode;
};

/*
 * The kernel's ioctl number includes this structure's size.  The MediaTek
 * driver implementation nevertheless reads only the uint32_t marker passed
 * by userspace, matching the behavior used by FBInk.
 */
struct hwtcon_update_marker_data {
    uint32_t update_marker;
    uint32_t collision_test;
};

#define HWTCON_SEND_UPDATE \
    _IOW('F', 0x2E, struct hwtcon_update_data)
#define HWTCON_WAIT_FOR_UPDATE_COMPLETE \
    _IOWR('F', 0x2F, struct hwtcon_update_marker_data)

#endif
