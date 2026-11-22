// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_clockface.h"
#include <math.h>
#include <string.h>

static const char *const NAMES[] = { "info", "classic", "minimal", "digital", "sun", "rings", "moon", "hud" };
#define N_FACES ((int)(sizeof NAMES / sizeof NAMES[0]))

int pc_cf_count(void) { return N_FACES; }
const char *pc_cf_name(int i) { return i >= 0 && i < N_FACES ? NAMES[i] : NAMES[0]; }
int pc_cf_parse(const char *name)
{
    for (int i = 0; name && i < N_FACES; i++) if (!strcmp(name, NAMES[i])) return i;
    return 0;
}
int pc_cf_next(int i) { return i + 1 < N_FACES && i >= 0 ? i + 1 : 0; }

/* the HUD face's colour schemes (an axis on the face): green, red, cyan and a three-colour one (purple frames, orange digits, green accents) */
static const char *const HC_NAMES[] = { "green", "red", "cyan", "tri" };
#define N_HC ((int)(sizeof HC_NAMES / sizeof HC_NAMES[0]))
int pc_hc_count(void) { return N_HC; }
const char *pc_hc_name(int i) { return i >= 0 && i < N_HC ? HC_NAMES[i] : HC_NAMES[0]; }
int pc_hc_parse(const char *name)
{
    for (int i = 0; name && i < N_HC; i++) if (!strcmp(name, HC_NAMES[i])) return i;
    return 0;
}

/* the faces of the timing features (pomodoro, stopwatch, countdown): the ones that make sense without a clock's hands, sun or moon */
static const char *const TF_NAMES[] = { "info", "rings", "hud" };
#define N_TF ((int)(sizeof TF_NAMES / sizeof TF_NAMES[0]))
int pc_tf_count(void) { return N_TF; }
const char *pc_tf_name(int i) { return i >= 0 && i < N_TF ? TF_NAMES[i] : TF_NAMES[0]; }
int pc_tf_parse(const char *name)
{
    for (int i = 0; name && i < N_TF; i++) if (!strcmp(name, TF_NAMES[i])) return i;
    return 0;
}

void pc_cf_hands(int h, int m, int s, int ms, pc_cf_hands_t *out)
{
    long sec_ms = (long)s * 1000 + ms, min_ms = (long)m * 60000 + sec_ms, hr_ms = (long)(h % 12) * 3600000 + min_ms;
    out->second = (int)(sec_ms * 3600 / 60000 % 3600);
    out->minute = (int)(min_ms / 1000 * 3600 / 3600 % 3600);           /* per whole second */
    out->hour = (int)(hr_ms / 1000 * 3600 / 43200 % 3600);
}

void pc_cf_point(int cx, int cy, int a, int r, int *x, int *y)
{
    double t = a * (M_PI / 1800.0);
    *x = cx + (int)lround(r * sin(t));
    *y = cy - (int)lround(r * cos(t));
}

int pc_cf_hand_boxes(int cx, int cy, int a, int r0, int r1, int hw, int nseg, pc_cf_box_t *out, int max)
{
    int n = nseg < max ? nseg : max;
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++) {
        int ra = r0 + (r1 - r0) * i / n, rb = r0 + (r1 - r0) * (i + 1) / n, xa, ya, xb, yb;
        pc_cf_point(cx, cy, a, ra, &xa, &ya);
        pc_cf_point(cx, cy, a, rb, &xb, &yb);
        int pad = hw + 2;                                          /* anti-aliased edge and rounding */
        out[i].x1 = (xa < xb ? xa : xb) - pad; out[i].x2 = (xa > xb ? xa : xb) + pad;
        out[i].y1 = (ya < yb ? ya : yb) - pad; out[i].y2 = (ya > yb ? ya : yb) + pad;
    }
    return n;
}

bool pc_cf_is_day(int now_min, int rise_min, int set_min, int polar)
{
    if (polar) return polar > 0;
    if (rise_min <= set_min) return now_min >= rise_min && now_min < set_min;
    return now_min >= rise_min || now_min < set_min;               /* the day wraps midnight (far west of the zone) */
}

int pc_cf_sun_deg(int now_min, int rise_min, int set_min, int polar)
{
    if (polar) {                                                   /* noon at the top / midnight at the bottom by the clock */
        return (int)((long)((now_min + 720) % 1440) * 3600 / 1440);
    }
    int day = ((set_min - rise_min) % 1440 + 1440) % 1440, night = 1440 - day;
    if (!day || !night) return 0;
    if (pc_cf_is_day(now_min, rise_min, set_min, 0)) {
        int t = ((now_min - rise_min) % 1440 + 1440) % 1440;
        return (2700 + t * 1800 / day) % 3600;                     /* 270 -> 360/0 -> 90 */
    }
    int t = ((now_min - set_min) % 1440 + 1440) % 1440;
    return 900 + t * 1800 / night;                                 /* 90 -> 180 -> 270 */
}

void pc_cf_rings(int h, int m, int s, int ms, pc_cf_rings_t *out)
{
    long sec_ms = (long)s * 1000 + ms;
    out->second = (int)(sec_ms / 60);
    out->minute = (int)(((long)m * 60000 + sec_ms) / 3600);
    out->hour = (int)(((long)(h % 12) * 3600 + m * 60 + s) * 1000 / 43200);
}
