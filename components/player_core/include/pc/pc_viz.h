// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Round-screen visuals (spec 5.7): dark UI themes, spectrum colour ramps, the "breathing ring". Colours are 0xRRGGBB. */
typedef struct { const char *name, *label; uint32_t bg, panel, panel2, fg, dim, a1, a2, a3, on; } pc_theme_t;
int pc_theme_count(void);
const pc_theme_t *pc_theme_at(int i);                 /* NULL out of range */
const pc_theme_t *pc_theme_find(const char *name);    /* unknown or NULL -> the default (neon) */

typedef enum { PC_RAMP_THEME, PC_RAMP_FIRE, PC_RAMP_ICE, PC_RAMP_WINAMP, PC_RAMP_RAINBOW, PC_RAMP_DUO, PC_RAMP_MONO, PC_RAMP_COUNT } pc_ramp_t;
const char *pc_ramp_name(pc_ramp_t r);                /* "theme", "fire", ... (cfg value) */
pc_ramp_t pc_ramp_parse(const char *name);           /* unknown -> PC_RAMP_THEME */
uint32_t pc_ramp_color(pc_ramp_t r, const pc_theme_t *t, float f);   /* f = level 0..1, clamped (NaN -> 0); a light theme (pc_theme_is_light) gets the paper ramps */
uint32_t pc_ramp_color_ex(pc_ramp_t r, const pc_theme_t *t, float f, bool light);   /* light forced: false = the dark ramps (LEDs emit light on a black backing, whatever the theme) */
bool pc_theme_is_light(const pc_theme_t *t);         /* background luminance (YIQ) >= 128: the one rule of ui_theme_is_light() */

/* Whether the spectrum draws this tick (15 fps timer), user 2026-11-09: pause while a cover thumbnail is being
 * decoded and for PC_VIZ_SWITCH_PAUSE_MS after a track change; on battery draw 2 ticks of 3 (10 fps). */
#define PC_VIZ_SWITCH_PAUSE_MS 1500
bool pc_viz_should_draw(bool thumb_busy, int64_t now_ms, int64_t changed_ms, bool battery, uint32_t tick);
#define PC_VIZ_FALL_PER_S 1.5f
#define PC_VIZ_PEAK_HOLD_S 0.35f
#define PC_VIZ_PEAK_FALL_PER_S 0.6f
/* in: n live values 0..1 (NaN and negatives count as 0, above 1 as 1). dt seconds, clamped to 0..0.25. level/peak/hold: n each, caller zeroes them.
 * As rgxx: level = max(in, level - 1.5*dt), not below 0; level >= peak -> peak = level, hold = 0.35;
 * otherwise hold -= dt, and from the step where hold goes below 0 peak -= 0.6*dt, not below 0. */
void pc_viz_follow(const float *in, int n, float dt, float *level, float *peak, float *hold);
#ifdef __cplusplus
}
#endif
