// SPDX-License-Identifier: LGPL-2.1-or-later
/* Reels: weighted pick, strips, spin motion, 3x3 slot paylines (pc_reel.h). */
#include "pc/pc_reel.h"
#include <math.h>
#include <string.h>

int pc_weighted_pick(pc_rng_t *r, const uint16_t *w, int n)
{
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) sum += w[i];
    if (n <= 0 || !sum) return -1;
    uint32_t x = pc_rng_below(r, sum);
    for (int i = 0; i < n; i++) { if (x < w[i]) return i; x -= w[i]; }
    return n - 1;
}

/* one try: weighted by what is left, never the previous symbol (nor, last, the first); 0 when stuck */
static int strip_try(pc_rng_t *r, const uint8_t *counts, int nsym, uint8_t *strip, int len)
{
    uint16_t left[256], w[256];
    for (int k = 0; k < nsym; k++) left[k] = counts[k];
    for (int i = 0; i < len; i++) {
        int rest = len - i, forced = -1;
        for (int k = 0; k < nsym; k++) {
            w[k] = (i && strip[i - 1] == k) || (i == len - 1 && strip[0] == k) ? 0 : left[k];
            if (w[k] && 2 * left[k] > rest) forced = k;          /* must go now or it cannot be spread */
        }
        int k = forced >= 0 ? forced : pc_weighted_pick(r, w, nsym);
        if (k < 0) return 0;
        strip[i] = (uint8_t)k;
        left[k]--;
    }
    return 1;
}

int pc_reel_strip(pc_rng_t *r, const uint8_t *counts, int nsym, uint8_t *strip, int len)
{
    int sum = 0;
    for (int k = 0; k < nsym; k++) sum += counts[k];
    if (sum != len || nsym <= 0 || nsym > 256) return -1;
    for (int a = 0; a < 200; a++) if (strip_try(r, counts, nsym, strip, len)) return len;
    int i = 0;                                                 /* cannot be spread: just the right counts */
    for (int k = 0; k < nsym; k++) for (int c = 0; c < counts[k]; c++) strip[i++] = (uint8_t)k;
    return len;
}

/* ---- motion ---- */
const pc_reel_cfg_t pc_reel_default = {
    .v_nom = 30.0f, .accel_ms = 250, .spin_ms = 1300, .stagger_ms = 450, .decel_ms = 420, .settle_ms = 160,
    .overshoot = 0.22f, .early_stagger_ms = 220,
};

static double cruise_pos(const pc_reel_motion_t *m, double t)
{
    return m->p0 + m->v * (double)(m->t_acc - m->t0) / 2.0 + m->v * (t - (double)m->t_acc);
}

static double wrap(double x, int len) { double y = fmod(x, (double)len); return y < 0 ? y + len : y; }

void pc_reel_plan(pc_reel_motion_t *m, double p0, int stop, int len, int32_t t0, int i, const pc_reel_cfg_t *c)
{
    memset(m, 0, sizeof *m);
    int spin = c->spin_ms > c->accel_ms ? c->spin_ms : c->accel_ms;
    m->t0 = t0;
    m->t_acc = t0 + c->accel_ms;
    m->t_dec = t0 + spin + i * c->stagger_ms;
    m->t_set = m->t_dec + c->decel_ms;
    m->t_end = m->t_set + c->settle_ms;
    m->p0 = p0;
    m->ov = c->overshoot;
    double k = c->accel_ms / 2.0 + (double)(m->t_dec - m->t_acc) + c->decel_ms / 3.0;   /* ms of cruise-speed travel */
    double lo = p0 + c->v_nom / 1000.0 * k - m->ov - len / 2.0;                         /* nearest landing to v_nom */
    m->target = ceil(lo) + wrap((double)stop - ceil(lo), len);
    m->v = (m->target + m->ov - p0) / k;
    m->s = m->v * c->decel_ms / 3.0;
}

bool pc_reel_stop_early(pc_reel_motion_t *m, int len, int32_t now, int i, const pc_reel_cfg_t *c)
{
    if (now >= m->t_dec) return false;
    int32_t e = (now > m->t_acc ? now : m->t_acc) + i * c->early_stagger_ms;
    if (e >= m->t_dec) return false;
    /* where slowing must begin (mod len) so that a normal slow-down lands on the same stop */
    double want = m->target + m->ov - m->v * c->decel_ms / 3.0;
    double d = wrap(want - cruise_pos(m, e), len);
    int32_t td = e + (int32_t)ceil(d / m->v);
    if (td >= m->t_dec) return false;
    double pd = cruise_pos(m, td);
    double tgt = m->target - len * floor((m->target - pd) / len);   /* same stop, the first one ahead of pd */
    while (tgt + m->ov - pd < m->v * c->decel_ms / 3.0 - 0.5) tgt += len;
    m->target = tgt;
    m->s = tgt + m->ov - pd;
    int32_t dur = (int32_t)ceil(3.0 * m->s / m->v);           /* start speed 3s/dur <= v: no jump */
    m->t_dec = td;
    m->t_set = td + dur;
    m->t_end = m->t_set + c->settle_ms;
    return true;
}

double pc_reel_pos(const pc_reel_motion_t *m, int32_t t)
{
    if (t <= m->t0) return m->p0;
    if (t < m->t_acc) { double a = (double)(m->t_acc - m->t0), dt = (double)(t - m->t0); return m->p0 + m->v * dt * dt / (2.0 * a); }
    if (t < m->t_dec) return cruise_pos(m, t);
    if (t < m->t_set) {
        double u = (double)(t - m->t_dec) / (double)(m->t_set - m->t_dec), q = 1.0 - u;
        return cruise_pos(m, m->t_dec) + m->s * (1.0 - q * q * q);
    }
    if (t < m->t_end) {
        double u = (double)(t - m->t_set) / (double)(m->t_end - m->t_set);
        return m->target + m->ov * (1.0 - u * u * (3.0 - 2.0 * u));
    }
    return m->target;
}

int pc_reel_stop_at(double pos, int len) { return (int)wrap(floor(pos + 1e-9), len); }

/* ---- slot ---- */
const uint8_t pc_slot_counts[PC_SYM_N] = { 6, 5, 5, 4, 3, 2, 1, 1 };
const uint16_t pc_slot_pay3[PC_SYM_N] = { 5, 10, 15, 20, 30, 60, 250, 120 };
const uint8_t pc_slot_line_rows[PC_SLOT_LINES][PC_SLOT_REELS] = { { 0, 0, 0 }, { 1, 1, 1 }, { 2, 2, 2 }, { 0, 1, 2 }, { 2, 1, 0 } };

void pc_slot_grid(const uint8_t *const strips[PC_SLOT_REELS], int len, const int stops[PC_SLOT_REELS], uint8_t grid[PC_SLOT_ROWS][PC_SLOT_REELS])
{
    for (int r = 0; r < PC_SLOT_REELS; r++)
        for (int row = 0; row < PC_SLOT_ROWS; row++) grid[row][r] = strips[r][(stops[r] + row) % len];
}

/* the one rule: fills w (sym, cells, wild, pay) and returns true when the line pays */
static bool line_win(const uint8_t s[PC_SLOT_REELS], pc_slot_win_t *w)
{
    int t = -1, clovers = 0;
    bool all = true;
    for (int r = 0; r < PC_SLOT_REELS; r++) {
        if (s[r] == PC_SYM_CLOVER) { clovers++; continue; }
        if (t < 0) t = s[r]; else if (s[r] != t) all = false;
    }
    if (all) {
        w->sym = (uint8_t)(t < 0 ? PC_SYM_CLOVER : t);
        w->cells = PC_SLOT_REELS;
        w->wild = (uint8_t)clovers;
        w->pay = pc_slot_pay3[w->sym];
        return true;
    }
    bool c0 = s[0] == PC_SYM_CHERRY || s[0] == PC_SYM_CLOVER, c1 = s[1] == PC_SYM_CHERRY || s[1] == PC_SYM_CLOVER;
    if (!(c0 && c1)) return false;
    w->sym = PC_SYM_CHERRY;
    w->cells = 2;
    w->wild = (uint8_t)((s[0] == PC_SYM_CLOVER) + (s[1] == PC_SYM_CLOVER));
    w->pay = PC_SLOT_PAY_CHERRY2;
    return true;
}

int pc_slot_wins(const uint8_t grid[PC_SLOT_ROWS][PC_SLOT_REELS], pc_slot_win_t out[PC_SLOT_LINES])
{
    int n = 0;
    for (int l = 0; l < PC_SLOT_LINES; l++) {
        uint8_t s[PC_SLOT_REELS];
        for (int r = 0; r < PC_SLOT_REELS; r++) s[r] = grid[pc_slot_line_rows[l][r]][r];
        pc_slot_win_t w;
        memset(&w, 0, sizeof w);
        if (!line_win(s, &w)) continue;
        w.line = (uint8_t)l;
        out[n++] = w;
    }
    return n;
}

int pc_slot_eval(const uint8_t grid[PC_SLOT_ROWS][PC_SLOT_REELS], int out[PC_SLOT_LINES])
{
    pc_slot_win_t w[PC_SLOT_LINES];
    int n = pc_slot_wins(grid, w), tot = 0;
    if (out) memset(out, 0, sizeof(int) * PC_SLOT_LINES);
    for (int k = 0; k < n; k++) { if (out) out[w[k].line] = w[k].pay; tot += w[k].pay; }
    return tot;
}
