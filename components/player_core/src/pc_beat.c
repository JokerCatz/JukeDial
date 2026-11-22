// SPDX-License-Identifier: LGPL-2.1-or-later
/* pc_beat: see include/pc/pc_beat.h. Integers for time, floats for the flux; every loop is bounded by the constants (no malloc). */
#include <math.h>
#include <string.h>
#include "pc/pc_beat.h"

#define LAG_MIN 33            /* 10 ms bins: 330 ms = 180 BPM */
#define LAG_MAX 100           /* 1000 ms = 60 BPM */
#define BIN_N   (3 * LAG_MAX + 8)
#define EST_EVERY_MS 500
#define FLUX_FLOOR 0.02f

void pc_beat_init(pc_beat_t *b) { memset(b, 0, sizeof *b); }
void pc_beat_latency(pc_beat_t *b, int32_t ms) { b->latency = ms; }

static float flux_of(pc_beat_t *b, const float *bars)
{
    float f = 0;
    for (int i = 0; i < PC_BEAT_BANDS; i++) {
        float d = bars[i] - b->prev[i];
        if (d > 0) f += d;
        b->prev[i] = bars[i];
    }
    return f / (float)PC_BEAT_BANDS;
}

/* the onsets inside the window, oldest first; returns how many */
static int gather(const pc_beat_t *b, uint32_t t[PC_BEAT_ONSETS])
{
    int n = 0;
    for (int k = 0; k < b->n_on; k++) {
        uint32_t v = b->onset[(b->i_on + PC_BEAT_ONSETS - b->n_on + k) % PC_BEAT_ONSETS];
        if (b->now_ms - v <= PC_BEAT_WINDOW_MS) t[n++] = v;
    }
    return n;
}

/* distance of x to the nearest multiple of p (signed offset in -p/2..p/2) */
static float offset(float x, float p) { float r = fmodf(x, p); if (r > p * 0.5f) r -= p; else if (r < -p * 0.5f) r += p; return r; }

static void estimate(pc_beat_t *b)
{
    uint32_t t[PC_BEAT_ONSETS];
    int n = gather(b, t);
    if (n < 4 || b->now_ms - b->last_onset > 3000) { b->bpm = 0; b->confidence = 0; b->grid = false; b->stable = 0; return; }
    uint8_t bin[BIN_N];
    memset(bin, 0, sizeof bin);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            uint32_t d = t[j] - t[i];
            if (d >= 10 * (uint32_t)(BIN_N - 1)) break;
            int k = (int)((d + 5) / 10);
            if (bin[k] < 255) bin[k]++;
        }
    int best = 0;
    float bs = 0;
    for (int L = LAG_MIN; L <= LAG_MAX; L++) {
        float s = 0;
        for (int m = 1; m <= 3; m++) {
            int c = m * L, sum = 0;
            for (int q = c - 4; q <= c + 4; q++) if (q >= 0 && q < BIN_N) sum += bin[q];
            s += (float)sum / (float)m;
        }
        if (s > bs) { bs = s; best = L; }
    }
    if (!best) { b->bpm = 0; b->confidence = 0; b->grid = false; b->stable = 0; return; }
    /* refine: the mean period of the pairs that match the lag at one, two and three beats */
    float acc = 0, cnt = 0, L0 = 10.0f * (float)best;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            float d = (float)(t[j] - t[i]);
            for (int m = 1; m <= 3; m++)
                if (fabsf(d - L0 * (float)m) <= 40.0f) { acc += d / (float)m; cnt += 1; break; }
        }
    float P = cnt > 0 ? acc / cnt : L0;
    if (P < 60000.0f / PC_BEAT_BPM_MAX) P = 60000.0f / PC_BEAT_BPM_MAX;
    if (P > 60000.0f / PC_BEAT_BPM_MIN) P = 60000.0f / PC_BEAT_BPM_MIN;
    /* the anchor: the circular mean of the onsets' positions in the period */
    float sx = 0, sy = 0;
    const float tau = 6.2831853f;
    for (int i = 0; i < n; i++) { float a = tau * fmodf((float)(t[i] - t[0]), P) / P; sx += cosf(a); sy += sinf(a); }
    float am = atan2f(sy, sx);
    if (am < 0) am += tau;
    uint32_t anchor = t[0] + (uint32_t)(am / tau * P + 0.5f);       /* a clock time at which a grid beat sits (relative to the oldest onset: no wrap) */
    int on = 0;
    for (int i = 0; i < n; i++) if (fabsf(offset((float)(int32_t)(t[i] - anchor), P)) <= (float)PC_BEAT_TOL_MS) on++;
    float conf = (float)on / (float)n;
    if (n < 5) conf *= (float)n / 5.0f;
    float nb = 60000.0f / P;
    b->stable = (b->bpm > 0 && fabsf(nb - b->bpm) <= 0.02f * b->bpm) ? (uint8_t)(b->stable < 250 ? b->stable + 1 : 250) : 0;   /* the tempo must hold still for a second */
    b->bpm = nb;
    b->confidence = conf;
    b->period = P;
    b->anchor = anchor;
    if (!b->grid) b->phase_prev = pc_beat_phase(b);
    b->grid = true;
}

static float phase_at(const pc_beat_t *b)
{
    float x = (float)((int32_t)(b->now_ms - b->anchor) - b->latency);
    float r = fmodf(x, b->period);
    if (r < 0) r += b->period;
    return r / b->period;
}
bool pc_beat_locked(const pc_beat_t *b) { return b->grid && b->bpm > 0 && b->stable >= PC_BEAT_STABLE && b->confidence >= PC_BEAT_CONF_MIN; }
float pc_beat_phase(const pc_beat_t *b) { return pc_beat_locked(b) ? phase_at(b) : 0.0f; }
uint32_t pc_beat_next_ms(const pc_beat_t *b)
{
    if (!pc_beat_locked(b)) return 0;
    return b->now_ms + (uint32_t)((1.0f - phase_at(b)) * b->period + 0.5f);
}

uint32_t pc_beat_step(pc_beat_t *b, const float bars[PC_BEAT_BARS], uint32_t dt_ms)
{
    uint32_t ev = 0;
    b->now_ms += dt_ms;
    float f = flux_of(b, bars);
    float mean = b->hn ? b->hsum / (float)b->hn : 0;
    float thr = 1.5f * mean + FLUX_FLOOR;
    if (b->hn >= 4 && f > thr && (!b->have_onset || b->now_ms - b->last_onset >= PC_BEAT_REFRACT_MS)) {
        b->onset[b->i_on] = b->now_ms;
        b->i_on = (uint8_t)((b->i_on + 1) % PC_BEAT_ONSETS);
        if (b->n_on < PC_BEAT_ONSETS) b->n_on++;
        b->last_onset = b->now_ms;
        b->have_onset = true;
        b->onset_flux = f;
        ev |= PC_BEAT_ONSET;
    }
    if (b->hn == PC_BEAT_HIST) b->hsum -= b->hist[b->hi]; else b->hn++;     /* the running sum of the last PC_BEAT_HIST fluxes */
    b->hist[b->hi] = f;
    b->hsum += f;
    b->hi = (uint8_t)((b->hi + 1) % PC_BEAT_HIST);
    if (b->now_ms - b->est_ms >= EST_EVERY_MS) { b->est_ms = b->now_ms; estimate(b); }
    if (pc_beat_locked(b)) {
        float ph = phase_at(b);
        if (ph < b->phase_prev && b->phase_prev - ph > 0.5f) ev |= PC_BEAT_TICK;
        b->phase_prev = ph;
    }
    return ev;
}
