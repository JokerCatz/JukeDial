// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_splash.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pc/pc_paths.h"

const char *const pc_splash_modes[] = { "player", "usb", "inst", "cast", "charge", NULL };

bool pc_splash_path(const char *mode, int i, char *out, size_t cap)
{
    if (!mode || !*mode || i < 0 || i > 1) return false;
    for (const char *p = mode; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
    int n = i == 0 ? snprintf(out, cap, PC_SPLASH_DIR "/%s.jpg", mode) : snprintf(out, cap, "%s", PC_SPLASH_JPG);
    return n > 0 && (size_t)n < cap;
}

static uint8_t low_of(uint8_t user)
{
    uint8_t l = (uint8_t)(user / PC_SPLASH_LOW_DIV);
    return l == 0 && user > 0 ? 1 : l;
}

bool pc_splash_seg(int i, uint8_t user_pct, pc_splash_seg_t *s)
{
    if (user_pct > 100) user_pct = 100;
    switch (i) {
    case 0: *s = (pc_splash_seg_t){ user_pct, PC_SPLASH_UP1_MS }; return true;
    case 1: *s = (pc_splash_seg_t){ low_of(user_pct), PC_SPLASH_DOWN_MS }; return true;
    case 2: *s = (pc_splash_seg_t){ user_pct, PC_SPLASH_UP2_MS }; return true;
    }
    return false;
}

int pc_splash_seg_at(uint32_t t)
{
    if (t < PC_SPLASH_UP1_MS) return 0;
    if (t < PC_SPLASH_UP1_MS + PC_SPLASH_DOWN_MS) return 1;
    if (t < PC_SPLASH_TOTAL_MS) return 2;
    return PC_SPLASH_NSEG;
}

uint8_t pc_splash_level(uint32_t t, uint8_t user)
{
    if (user > 100) user = 100;
    int k = pc_splash_seg_at(t);
    if (k >= PC_SPLASH_NSEG) return user;
    uint32_t t0 = k == 0 ? 0 : k == 1 ? PC_SPLASH_UP1_MS : PC_SPLASH_UP1_MS + PC_SPLASH_DOWN_MS;
    pc_splash_seg_t s;
    pc_splash_seg(k, user, &s);
    int from = k == 0 ? 0 : k == 1 ? user : low_of(user);
    /* straight lines in the PERCEIVED domain (the eye and the PWM duty are not linear: a linear percent ramp looks like a jump at the dark end): sqrt(percent) interpolated, squared.
     * The segment ends (user, user / 3, 0) are unchanged. */
    const float a = sqrtf((float)from), b = sqrtf((float)s.to_pct), x = a + (b - a) * (float)(t - t0) / (float)s.ms;
    int v = (int)(x * x + 0.5f);
    if (v < 1 && t > 0 && user > 0) v = 1;       /* after the first light never 0 (an AMOLED's 0 % is display-off and a 120 ms wake) */
    if (v > user) v = user;
    return (uint8_t)v;
}
