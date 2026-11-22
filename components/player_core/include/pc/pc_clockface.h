// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Clock faces (spec 2026-11-10-clock-design.md section 5): names, hand angles, the sun dial, ring fractions and
 * the small areas a moving hand covers. Angles are deci-degrees clockwise from 12 o'clock. */
int pc_cf_count(void);
const char *pc_cf_name(int i);           /* cfg value ("info", "classic", ...); out of range -> "info" */
int pc_cf_parse(const char *name);       /* unknown / NULL -> 0 */
int pc_cf_next(int i);                   /* wraps */
/* the HUD face's colour schemes: "green", "red", "cyan", "tri" (purple frames, orange digits, green accents); cfg hudcol */
enum { PC_HC_GREEN, PC_HC_RED, PC_HC_CYAN, PC_HC_TRI };
int pc_hc_count(void);
const char *pc_hc_name(int i);           /* out of range -> "green" */
int pc_hc_parse(const char *name);       /* unknown / NULL -> 0 */
/* the faces of the timing features (pomodoro, stopwatch, countdown): "info", "rings", "hud"; classic, minimal, digital, sun and moon are the clock's only */
enum { PC_TF_INFO, PC_TF_RINGS, PC_TF_HUD };
int pc_tf_count(void);
const char *pc_tf_name(int i);           /* out of range -> "info" */
int pc_tf_parse(const char *name);       /* unknown / NULL -> 0 */

typedef struct { int hour, minute, second; } pc_cf_hands_t;
/* hour hand follows minutes and seconds, minute hand follows seconds, second hand follows ms (smooth sweep) */
void pc_cf_hands(int h, int m, int s, int ms, pc_cf_hands_t *out);
/* point at radius r along angle a (deci-degrees) from (cx, cy); r may be negative (a counterweight tail) */
void pc_cf_point(int cx, int cy, int a, int r, int *x, int *y);

typedef struct { int x1, y1, x2, y2; } pc_cf_box_t;
/* A hand from radius r0 to r1 at angle a, half width hw, cut into nseg boxes that together cover it
 * (redraw only these, not one box round the whole diagonal). Returns the boxes written (<= max). */
int pc_cf_hand_boxes(int cx, int cy, int a, int r0, int r1, int hw, int nseg, pc_cf_box_t *out, int max);

/* The sun dial: day on the top half (sunrise at 9 o'clock, sunset at 3), night on the bottom half.
 * Minutes of the local day; polar: 1 = sun never sets (whole day on top), -1 = never rises, 0 = normal. */
int pc_cf_sun_deg(int now_min, int rise_min, int set_min, int polar);
bool pc_cf_is_day(int now_min, int rise_min, int set_min, int polar);

typedef struct { int hour, minute, second; } pc_cf_rings_t;   /* permille: of 12 h, of 60 min, of 60 s */
void pc_cf_rings(int h, int m, int s, int ms, pc_cf_rings_t *out);
#ifdef __cplusplus
}
#endif
