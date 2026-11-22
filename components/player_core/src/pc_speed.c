// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_speed.h"
#include <math.h>
#include <string.h>

void pc_speed_init(pc_speed_t *s, uint32_t rate, float speed)
{
    if (rate > PC_SPEED_MAX_RATE) rate = PC_SPEED_MAX_RATE;
    if (!(speed >= PC_SPEED_MIN)) speed = PC_SPEED_MIN;
    if (speed > PC_SPEED_MAX) speed = PC_SPEED_MAX;
    s->ov = (int)(rate / 100);
    s->seq = (int)(rate * 4 / 100);
    s->seek = (int)(rate * 12 / 1000);
    s->speed = speed;
    s->in_n = 0;
    s->skip = 0;
    s->nom = 0;
    s->have_tail = s->eos = s->done = false;
    s->out_n = s->out_r = 0;
    for (int i = 0; i < s->ov; i++) s->win[i] = 0.5f - 0.5f * cosf((float)M_PI * ((float)i + 0.5f) / (float)s->ov);
}

static void pull(pc_speed_t *s, int need_in, pc_speed_feed_fn feed, void *u)   /* grow in[] to need_in frames, or until the source ends */
{
    if (need_in > PC_SPEED_IN_MAX) need_in = PC_SPEED_IN_MAX;
    while (s->skip > 0 && !s->eos) {                                 /* input the speed jumps over (in_n is 0 here): read and forget */
        int ch = s->skip < PC_SPEED_IN_MAX ? s->skip : PC_SPEED_IN_MAX;
        int got = feed(u, s->in, ch);
        if (got < 0) got = 0;
        if (got > ch) got = ch;
        s->skip -= got;
        if (got < ch) s->eos = true;
    }
    while (!s->eos && s->in_n < need_in) {
        int want = need_in - s->in_n;
        int got = feed(u, s->in + 2 * s->in_n, want);
        if (got < 0) got = 0;
        if (got > want) got = want;
        s->in_n += got;
        if (got < want) s->eos = true;
    }
}

static inline float mono(const float *p) { return p[0] + p[1]; }

/* the offset p (frames into in[], lo <= p <= hi) whose ov frames follow the saved tail best */
static int search(pc_speed_t *s, int lo, int hi)
{
    const int D = PC_SPEED_DEC, ov = s->ov, nd = ov / D;
    for (int i = 0; i < nd; i++) {                                   /* reference: the tail, decimated mono */
        float a = 0;
        for (int k = 0; k < D; k++) a += mono(s->tail + 2 * (i * D + k));
        s->dref[i] = a;
    }
    int span = (hi - lo) / D + nd + 1;                               /* decimated input over every candidate */
    for (int i = 0; i < span; i++) {
        float a = 0;
        const float *p = s->in + 2 * (lo + i * D);
        for (int k = 0; k < D; k++) a += mono(p + 2 * k);
        s->din[i] = a;
    }
    int nc = (hi - lo) / D + 1, bi = 0;
    float en = 0, best = -1e30f;
    for (int i = 0; i < nd; i++) en += s->din[i] * s->din[i];
    for (int c = 0; c < nc; c++) {
        float dot = 0;
        const float *d = s->din + c;
        for (int i = 0; i < nd; i++) dot += s->dref[i] * d[i];
        float sc = dot / sqrtf(en + 1e-6f);
        if (sc > best) { best = sc; bi = c; }
        en += d[nd] * d[nd] - d[0] * d[0];
        if (en < 0) en = 0;
    }
    int c0 = lo + bi * D, bp = c0;                                   /* refine at full rate around the coarse pick */
    float rb = -1e30f;
    for (int p = c0 - D; p <= c0 + D; p++) {
        if (p < lo || p > hi) continue;
        float dot = 0, e = 0;
        const float *q = s->in + 2 * p;
        for (int i = 0; i < ov; i += 2) { float m = mono(q + 2 * i); dot += mono(s->tail + 2 * i) * m; e += m * m; }   /* every 2nd frame: enough for 1/48000 s steps */
        float sc = dot / sqrtf(e + 1e-6f);
        if (sc > rb) { rb = sc; bp = p; }
    }
    return bp;
}

static void emit(pc_speed_t *s, const float *src, int frames)
{
    memcpy(s->out + 2 * s->out_n, src, (size_t)frames * 2 * sizeof(float));
    s->out_n += frames;
}

static void drop(pc_speed_t *s, int k)                               /* forget the first k input frames */
{
    if (k <= 0) return;
    s->nom -= k;
    if (k > s->in_n) { s->skip += k - s->in_n; k = s->in_n; }
    memmove(s->in, s->in + 2 * k, (size_t)(s->in_n - k) * 2 * sizeof(float));
    s->in_n -= k;
}

static void iterate(pc_speed_t *s, pc_speed_feed_fn feed, void *u)   /* appends seq - ov frames (or the flush) to out[] */
{
    const int ov = s->ov, seq = s->seq, hop = seq - ov;
    if (s->out_r) {                                                   /* compact the output queue */
        memmove(s->out, s->out + 2 * s->out_r, (size_t)(s->out_n - s->out_r) * 2 * sizeof(float));
        s->out_n -= s->out_r;
        s->out_r = 0;
    }
    if (!s->have_tail) {                                              /* the first grain: straight copy */
        pull(s, seq, feed, u);
        if (s->in_n < seq) {                                          /* a very short source: pass it as it is */
            emit(s, s->in, s->in_n);
            s->in_n = 0;
            s->done = true;
            return;
        }
        emit(s, s->in, hop);
        memcpy(s->tail, s->in + 2 * hop, (size_t)ov * 2 * sizeof(float));
        s->have_tail = true;
        s->nom = (double)hop * s->speed;
        drop(s, (int)floor(s->nom) - s->seek);
        return;
    }
    int lo = (int)floor(s->nom) - s->seek;
    if (lo < 0) lo = 0;
    int hi = (int)floor(s->nom) + s->seek;
    pull(s, hi + seq, feed, u);
    if (s->in_n < hi + seq) {                                         /* the source ended inside the search window */
        int from = (int)floor(s->nom);
        if (from > s->in_n) from = s->in_n;
        for (int i = 0; i < ov; i++) {                                /* the tail fades out while the rest of the input fades in */
            float w = s->win[i];
            int j = from + i;
            for (int c = 0; c < 2; c++) s->tail[2 * i + c] = s->tail[2 * i + c] * (1.0f - w) + (j < s->in_n ? s->in[2 * j + c] : 0.0f) * w;
        }
        emit(s, s->tail, ov);
        if (from + ov < s->in_n) emit(s, s->in + 2 * (from + ov), s->in_n - from - ov);
        s->in_n = 0;
        s->done = true;
        return;
    }
    int p = search(s, lo, hi);
    for (int i = 0; i < ov; i++) {
        float w = s->win[i];
        s->out[2 * (s->out_n + i)] = s->tail[2 * i] * (1.0f - w) + s->in[2 * (p + i)] * w;
        s->out[2 * (s->out_n + i) + 1] = s->tail[2 * i + 1] * (1.0f - w) + s->in[2 * (p + i) + 1] * w;
    }
    s->out_n += ov;
    emit(s, s->in + 2 * (p + ov), seq - 2 * ov);
    memcpy(s->tail, s->in + 2 * (p + seq - ov), (size_t)ov * 2 * sizeof(float));
    s->nom += (double)hop * s->speed;
    drop(s, (int)floor(s->nom) - s->seek);
}

int pc_speed_run(pc_speed_t *s, float *out, int frames, pc_speed_feed_fn feed, void *u, bool *ended)
{
    if (frames > PC_SPEED_RUN_MAX) frames = PC_SPEED_RUN_MAX;
    while (s->out_n - s->out_r < frames && !s->done) iterate(s, feed, u);
    int n = s->out_n - s->out_r;
    if (n > frames) n = frames;
    memcpy(out, s->out + 2 * s->out_r, (size_t)n * 2 * sizeof(float));
    s->out_r += n;
    if (ended) *ended = s->done && s->out_r >= s->out_n;
    return n;
}

static const int SPEEDS[PC_SPEED_STEPS] = { 75, 100, 125, 150, 200 };
static const int XFADES[PC_XFADE_STEPS] = { 0, 2, 4, 6 };
static int idx_of(const int *v, int n, int x, int dflt) { for (int i = 0; i < n; i++) if (v[i] == x) return i; return dflt; }
int pc_speed_norm(int pct) { return SPEEDS[idx_of(SPEEDS, PC_SPEED_STEPS, pct, 1)]; }
int pc_speed_step(int pct, int d) { return SPEEDS[((idx_of(SPEEDS, PC_SPEED_STEPS, pct, 1) + d) % PC_SPEED_STEPS + PC_SPEED_STEPS) % PC_SPEED_STEPS]; }
int pc_xfade_norm(int sec) { return XFADES[idx_of(XFADES, PC_XFADE_STEPS, sec, 0)]; }
int pc_xfade_step(int sec, int d) { return XFADES[((idx_of(XFADES, PC_XFADE_STEPS, sec, 0) + d) % PC_XFADE_STEPS + PC_XFADE_STEPS) % PC_XFADE_STEPS]; }
bool pc_speed_ok(uint8_t fx_mode_bits) { return !(fx_mode_bits & PC_SPEED_OFF_MODES); }

float pc_xfade_progress(int64_t left, int64_t total)
{
    if (total <= 0 || left <= 0) return 1.0f;
    if (left >= total) return 0.0f;
    return 1.0f - (float)left / (float)total;
}

static float clamp01(float p) { return p < 0.0f ? 0.0f : p > 1.0f ? 1.0f : p; }

static float knee(float v)                                           /* identity up to 0.8, then a tanh shoulder that stays under 1.0 */
{
    float m = v < 0 ? -v : v;
    if (m <= 0.8f) return v;
    float r = 0.8f + 0.2f * tanhf((m - 0.8f) * 5.0f);
    return v < 0 ? -r : r;
}

void pc_xfade_mix(float *a, const float *b, int frames, float p0, float p1)
{
    if (frames <= 0) return;
    const float h = (float)M_PI * 0.5f, t0 = clamp01(p0) * h, t1 = clamp01(p1) * h, dt = (t1 - t0) / (float)frames;
    float c = cosf(t0), s = sinf(t0);
    const float cd = cosf(dt), sd = sinf(dt);
    for (int i = 0; i < frames; i++) {
        a[2 * i] = knee(a[2 * i] * c + b[2 * i] * s);
        a[2 * i + 1] = knee(a[2 * i + 1] * c + b[2 * i + 1] * s);
        float c2 = c * cd - s * sd;
        s = s * cd + c * sd;
        c = c2;
    }
}

void pc_xfade_finish(float *b, int frames, float p)
{
    if (frames <= 0) return;
    float g = sinf(clamp01(p) * (float)M_PI * 0.5f);
    const float step = (1.0f - g) / (float)frames;
    for (int i = 0; i < frames; i++) { g += step; b[2 * i] *= g; b[2 * i + 1] *= g; }
}
