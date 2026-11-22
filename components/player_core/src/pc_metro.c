// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_metro.h"
#include <math.h>
#include <string.h>

#define CLICK_MS 20u
#define CLICK_ACCENT_HZ 1760u
#define CLICK_BEAT_HZ 880u
#define CLICK_AMP 7000

void pc_metro_init(pc_metro_t *m, uint32_t rate)
{
    memset(m, 0, sizeof *m);
    m->rate = rate;
    m->bpb = 4;
    pc_metro_set_bpm(m, 120);
}

void pc_metro_set_bpm(pc_metro_t *m, double bpm)
{
    if (bpm < 30) bpm = 30;
    if (bpm > 300) bpm = 300;
    double old = m->interval;
    m->interval = m->rate * 60.0 / bpm;
    /* from the last committed beat (the next one is still planned from it) */
    if (m->running && m->idx > 0 && old > 0) m->next_pos += m->interval - old;
}

void pc_metro_set_bpb(pc_metro_t *m, int bpb) { m->bpb = bpb < 1 ? 1 : bpb > 16 ? 16 : bpb; }

void pc_metro_start(pc_metro_t *m, int64_t first_pos)
{
    m->running = true;
    m->pending = false;
    m->idx = 0;
    m->next_pos = (double)first_pos;
}

void pc_metro_stop(pc_metro_t *m)
{
    m->running = false;
    m->pending = false;
    m->c_left = 0;
}

static void commit(pc_metro_t *m, int64_t pos, uint32_t k, pc_beat_t *out)
{
    m->next.pos = pos;
    m->next.idx = k;
    m->next.beat = (uint8_t)(k % (uint32_t)m->bpb);
    m->next.accent = m->next.beat == 0;
    m->pending = true;
    if (out) *out = m->next;
}

int pc_metro_plan(pc_metro_t *m, int64_t now, int64_t horizon, pc_beat_t *out)
{
    if (!m->running || m->pending) return 0;
    int64_t pos = llround(m->next_pos);
    if (pos >= horizon) return 0;
    if (pos < now) pos = now;
    commit(m, pos, m->idx++, out);
    m->next_pos = (double)pos + m->interval;
    return 1;
}

int pc_metro_commit(pc_metro_t *m, int64_t now, int64_t pos, uint32_t k, pc_beat_t *out)
{
    if (!m->running || m->pending) return 0;
    commit(m, pos < now ? now : pos, k, out);
    m->idx = k + 1;
    m->next_pos = (double)m->next.pos + m->interval;     /* switching to the internal tempo carries on from here */
    return 1;
}

void pc_metro_render(pc_metro_t *m, int32_t *mix, int frames, int64_t pos, int32_t gain_q15)
{
    for (int f = 0; f < frames; f++) {
        if (m->pending && m->next.pos <= pos + f) {
            m->pending = false;
            m->c_total = m->c_left = m->rate * CLICK_MS / 1000u;
            m->c_half = m->rate / ((m->next.accent ? CLICK_ACCENT_HZ : CLICK_BEAT_HZ) * 2u);
            if (!m->c_half) m->c_half = 1;
            m->c_phase = 0;
        }
        if (!m->c_left) {
            if (!m->pending) return;                         /* nothing more in this block */
            continue;
        }
        int32_t amp = (int32_t)((int64_t)CLICK_AMP * m->c_left / m->c_total * gain_q15 >> 15);
        int32_t s = (m->c_phase / m->c_half) & 1u ? -amp : amp;
        if (++m->c_phase >= m->c_half * 2u) m->c_phase = 0;
        m->c_left--;
        mix[2 * f] += s;
        mix[2 * f + 1] += s;
    }
}

void pc_aclock_init(pc_aclock_t *a, uint32_t rate, int64_t latency_us)
{
    memset(a, 0, sizeof *a);
    a->rate = rate;
    a->lat_us = latency_us;
}

void pc_aclock_mark(pc_aclock_t *a, int64_t pos, int64_t t)
{
    int64_t c = t - pos * 1000000 / a->rate;
    if (!a->have) { a->have = true; a->off_us = a->win_min = a->prev_min = c; a->win_t = t; return; }
    if (c < a->win_min) a->win_min = c;
    if (t - a->win_t >= 1000000) { a->prev_min = a->win_min; a->win_min = c; a->win_t = t; }
    a->off_us = a->prev_min < a->win_min ? a->prev_min : a->win_min;
}

int64_t pc_aclock_time(const pc_aclock_t *a, int64_t pos) { return a->off_us + pos * 1000000 / a->rate + a->lat_us; }
int64_t pc_aclock_pos(const pc_aclock_t *a, int64_t t) { return (t - a->lat_us - a->off_us) * a->rate / 1000000; }
