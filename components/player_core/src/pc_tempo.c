// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_tempo.h"
#include <math.h>
#include <string.h>

void pc_tap_reset(pc_tap_t *t) { memset(t, 0, sizeof *t); }

int pc_tap(pc_tap_t *t, int64_t ms)
{
    if (t->n) {
        int64_t last = t->t[t->n - 1], iv = ms - last;
        if (iv < 120) return 0;                                  /* bounce */
        if (iv > 2000) t->n = 0;                                 /* pause: a new run */
        else if (t->n >= 2) {
            int64_t mean = (last - t->t[0]) / (t->n - 1);
            if (iv * 2 < mean || iv > mean * 2) { t->t[0] = last; t->n = 1; }   /* rhythm changed */
        }
    }
    if (t->n == 5) { memmove(t->t, t->t + 1, 4 * sizeof t->t[0]); t->n = 4; }
    t->t[t->n++] = ms;
    if (t->n < 4) return 0;
    double bpm = 60000.0 * (t->n - 1) / (double)(t->t[t->n - 1] - t->t[0]);
    int b = (int)lround(bpm);
    return b < PC_BPM_MIN ? PC_BPM_MIN : b > PC_BPM_MAX ? PC_BPM_MAX : b;
}

void pc_clk_reset(pc_clk_t *c) { memset(c, 0, sizeof *c); }

static double predict(const pc_clk_t *c, double k) { return (double)c->t0 + c->a + c->b * (k - (double)c->k0); }

static void refit(pc_clk_t *c)
{
    int m = c->count;
    if (m < 2) { c->fit_n = 0; return; }
    uint32_t k0 = c->n - (uint32_t)m;
    int64_t t0 = c->t[k0 % PC_CLK_WIN];
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < m; i++) {
        double x = i, y = (double)(c->t[(k0 + (uint32_t)i) % PC_CLK_WIN] - t0);
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double d = m * sxx - sx * sx;
    if (d <= 0) { c->fit_n = 0; return; }
    double b = (m * sxy - sx * sy) / d;
    if (b <= 0) { c->fit_n = 0; return; }
    c->b = b;
    c->a = (sy - b * sx) / m;
    c->t0 = t0;
    c->k0 = k0;
    c->fit_n = m;
}

void pc_clk_tick(pc_clk_t *c, int64_t t)
{
    if (c->count) {
        int64_t last = c->t[(c->n - 1) % PC_CLK_WIN];
        if (t - last > 1000000) {                                /* the clock went away: start over */
            c->count = 0;
            c->fit_n = 0;
            c->off_run = 0;
            if (c->running) c->start = c->n;                     /* the grid restarts here */
        }
    }
    if (c->fit_n >= 6) {                                         /* tempo jump: 3 ticks off the line, same side */
        double r = (double)t - predict(c, c->n), lim = c->b / 4 > 3000 ? c->b / 4 : 3000;
        int side = r > 0 ? 1 : -1;
        if (fabs(r) > lim) {
            c->off_run = side == c->side ? c->off_run + 1 : 1;
            c->side = side;
            if (c->off_run >= 3) { if (c->count > 2) c->count = 2; c->off_run = 0; }
        } else c->off_run = 0;
    }
    c->t[c->n % PC_CLK_WIN] = t;
    c->n++;
    if (c->count < PC_CLK_WIN) c->count++;
    refit(c);
}

void pc_clk_start(pc_clk_t *c) { c->running = true; c->start = c->n; }
void pc_clk_continue(pc_clk_t *c) { c->running = true; }
void pc_clk_stop(pc_clk_t *c) { c->running = false; }

bool pc_clk_locked(const pc_clk_t *c, int64_t now)
{
    if (c->fit_n < 6 || !c->count) return false;
    int64_t last = c->t[(c->n - 1) % PC_CLK_WIN];
    double lim = 3 * c->b > 500000 ? 3 * c->b : 500000;
    return (double)(now - last) < lim;
}

double pc_clk_period_us(const pc_clk_t *c) { return c->fit_n >= 2 ? c->b : 0; }
double pc_clk_bpm(const pc_clk_t *c) { return c->fit_n >= 2 ? 60e6 / (24.0 * c->b) : 0; }

bool pc_clk_beat_time(const pc_clk_t *c, uint32_t k, int64_t *t)
{
    if (c->fit_n < 2) return false;
    *t = (int64_t)llround(predict(c, (double)c->start + 24.0 * k));
    return true;
}

uint32_t pc_clk_beat_after(const pc_clk_t *c, int64_t t)
{
    if (c->fit_n < 2) return 0;
    double tick = ((double)(t - c->t0) - c->a) / c->b + (double)c->k0;
    double beats = (tick - (double)c->start) / 24.0;
    return beats <= 0 ? 0 : (uint32_t)ceil(beats);
}
