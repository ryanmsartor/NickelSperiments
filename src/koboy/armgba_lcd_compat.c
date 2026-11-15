/*
 * Copyright (C) 2026 NickelSperiments contributors
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * Ko Boy presents armGBA frames through its own grayscale scaler and e-ink
 * refresh scheduler. Keep armGBA's optional LCD simulation disabled so the
 * core remains buildable with Kobo's GCC 4.9 toolchain.
 */

#include "core.h"
#include "lcd.h"

int lcdThreads;
int ghosting;
bool interleaved;
int gridHalves = 2;
int gridWay;
bool gridGaps;
bool gridFit;
unsigned gridFitWidth = 1280;
unsigned gridFitHeight = 720;
float libretroLcdShadowDistance = 1.5f;
float libretroLcdSubpixelMix;
float libretroLcdShownScale = 1.0f;
unsigned libretroLcdScreenWidth;
unsigned libretroLcdScreenHeight;
uint16_t libretroLcdLuminance[3][256];

bool lcdCastsShadow(void) { return false; }
bool lcdHasSubpixels(void) { return false; }
bool libretroLcdShadow(void) { return false; }

void libretroLcdShadowFrom(uint32_t unlit, uint32_t darkest,
                           int *start, int *scale) {
    (void)unlit;
    (void)darkest;
    *start = 0;
    *scale = 0;
}

enum LibretroLcd libretroLcd(uint32_t *gap) {
    *gap = 0;
    return LIBRETRO_LCD_COLOR;
}

bool libretroLcdKeep(struct LibretroLcdKeep *keep) {
    (void)keep;
    return false;
}

void libretroLcdFitSize(unsigned width, unsigned height,
                        unsigned *out_width, unsigned *out_height) {
    *out_width = width;
    *out_height = height;
}

void sendPicture(const void *picture, unsigned width, unsigned height,
                 size_t pitch) {
    videoRefresh(frameSkipped ? NULL : picture, width, height, pitch);
}

void showPicture(const void *picture, unsigned width, unsigned height,
                 size_t pitch) {
    sendPicture(picture, width, height, pitch);
}
