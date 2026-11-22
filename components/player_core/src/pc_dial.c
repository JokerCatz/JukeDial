// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_dial.h"

int pc_dial_wrap(int v, int lo, int hi, bool wrap)
{
    if (hi < lo) return lo;
    if (!wrap) return v < lo ? lo : v > hi ? hi : v;
    const int n = hi - lo + 1;
    int r = (v - lo) % n;
    if (r < 0) r += n;
    return lo + r;
}
int pc_dial_step(int v, int d, int lo, int hi, bool wrap) { return pc_dial_wrap(v + d, lo, hi, wrap); }
int pc_dial_dist(int a, int b, int lo, int hi, bool wrap)
{
    int d = a > b ? a - b : b - a;
    if (wrap) {
        const int n = hi - lo + 1;
        d %= n;
        if (d > n - d) d = n - d;
    }
    return d;
}
int pc_dial_grade(int d, int reach)
{
    if (d <= 0) return 3;
    if (reach <= 0) return 0;
    int near = reach / 3;
    if (near < 1) near = 1;
    return d <= near ? 2 : d <= reach ? 1 : 0;
}
int pc_dial_angle10(int v, int lo, int hi, bool wrap, int a0, int span)
{
    if (hi < lo) return a0 * 10;
    if (wrap) {
        v = pc_dial_wrap(v, lo, hi, true);
        return a0 * 10 + (v - lo) * span * 10 / (hi - lo + 1);
    }
    if (hi == lo) return a0 * 10;
    v = pc_dial_wrap(v, lo, hi, false);
    return a0 * 10 + (v - lo) * span * 10 / (hi - lo);
}

void pc_meter_init(pc_meter_t *m, int v) { m->shown = m->target = m->peak = v * 16; m->hold = 0; m->shift = 2; m->nopeak = false; }
void pc_meter_config(pc_meter_t *m, int smooth_shift, bool peak)
{
    m->shift = (uint8_t)(smooth_shift <= 0 ? 2 : smooth_shift > 6 ? 6 : smooth_shift);
    m->nopeak = !peak;
}
void pc_meter_feed(pc_meter_t *m, int v) { m->target = v * 16; }
bool pc_meter_step(pc_meter_t *m)
{
    bool moved = false;
    if (m->shown != m->target) {
        int32_t d = (m->target - m->shown) / (1 << m->shift);
        if (d == 0) d = m->target > m->shown ? 1 : -1;
        m->shown += d;
        moved = true;
    }
    if (m->nopeak) { m->peak = m->shown; m->hold = 0; return moved; }
    if (m->shown >= m->peak) {
        if (m->shown > m->peak) moved = true;
        m->peak = m->shown;
        m->hold = PC_METER_HOLD;
    } else if (m->hold > 0) {
        m->hold--;
        moved = true;
    } else {
        int32_t f = (m->peak - m->shown) / 16;
        m->peak -= f < 1 ? 1 : f;
        moved = true;
    }
    return moved;
}
int pc_meter_shown(const pc_meter_t *m) { return (m->shown + 8) >> 4; }
int pc_meter_peak(const pc_meter_t *m) { return (m->peak + 8) >> 4; }
int pc_dial_cue(int dist, int reach, int strength)
{
    if (reach <= 0 || dist < 0 || dist >= reach) return 0;
    if (strength > 100) strength = 100;
    if (strength <= 0) return 0;
    return 255 * strength * (reach - dist) / (100 * reach);
}
