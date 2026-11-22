// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extstream.h"
#include <math.h>
#include <string.h>

#define LD(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define ST(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)

int pc_extstream_start_volume(int saved)
{
    if (saved < 0) saved = 0;
    return saved > PC_EXTSTREAM_START_VOL ? PC_EXTSTREAM_START_VOL : saved;
}

void pc_extstream_init(pc_extstream_t *s, void *buf, uint32_t cap)
{
    memset(s, 0, sizeof *s);
    s->buf = buf;
    s->cap = buf ? cap - cap % 24 : 0;
}

static uint32_t pack(uint32_t rate, int bytes)
{
    if (rate < 1 || rate > 384000 || bytes < 2 || bytes > 4) return 0;
    return (rate << 4) | (uint32_t)bytes;
}
void pc_extstream_set_format(pc_extstream_t *s, uint32_t rate, int bytes)
{
    ST(&s->cfg, pack(rate, bytes));
    pc_extstream_flush(s);
}
void pc_extstream_flush(pc_extstream_t *s)
{
    ST(&s->flush_to, s->wcount);
    ST(&s->epoch, s->epoch + 1);
}
uint32_t pc_extstream_rate(const pc_extstream_t *s) { return LD(&s->cfg) >> 4; }
int pc_extstream_bytes(const pc_extstream_t *s) { return (int)(LD(&s->cfg) & 15u); }

uint32_t pc_extstream_write(pc_extstream_t *s, const uint8_t *data, uint32_t bytes)
{
    uint32_t cfg = LD(&s->cfg);
    if (!cfg || !s->cap) return 0;
    uint32_t rate = cfg >> 4, fb = 2u * (cfg & 15u);
    bytes -= bytes % fb;
    uint32_t high = (uint32_t)((uint64_t)rate * PC_EXTSTREAM_HIGH_MS / 1000u) * fb;
    if (high > s->cap) high = s->cap;
    uint32_t fill = LD(&s->fill), room = high > fill ? high - fill : 0;
    room -= room % fb;
    uint32_t take = bytes < room ? bytes : room;
    if (take < bytes) {
        if (LD(&s->primed) && LD(&s->seen_epoch) == LD(&s->epoch)) { s->overruns++; s->drops += (bytes - take) / fb; }   /* nothing playing, or a flush the reader has not taken yet (a rate change: the mixer is restarting): the ring is only waiting, not an overrun */
    }
    uint32_t first = s->cap - s->wr;
    if (first > take) first = take;
    memcpy(s->buf + s->wr, data, first);
    if (take > first) memcpy(s->buf, data + first, take - first);
    s->wr = (s->wr + take) % s->cap;
    s->wcount += take;
    __atomic_fetch_add(&s->fill, take, __ATOMIC_RELEASE);
    return take;
}

uint32_t pc_extstream_fill_frames(const pc_extstream_t *s)
{
    uint32_t cfg = LD(&s->cfg);
    return cfg ? LD(&s->fill) / (2u * (cfg & 15u)) : 0;
}
uint32_t pc_extstream_target_frames(const pc_extstream_t *s)
{
    uint32_t cfg = LD(&s->cfg);
    return (uint32_t)((uint64_t)(cfg >> 4) * PC_EXTSTREAM_TARGET_MS / 1000u);
}

bool pc_extstream_playing(const pc_extstream_t *s) { return LD(&s->primed) != 0; }
static float clamp_gain(float g) { return g > 0.0f ? (g < 1.0f ? g : 1.0f) : 0.0f; }   /* NaN is not > 0: 0 */

static void reader_flush(pc_extstream_t *s)       /* skip what was written before the request (not what came after it) */
{
    int32_t n = (int32_t)(LD(&s->flush_to) - s->rcount);
    uint32_t fill = LD(&s->fill);
    if (n <= 0) return;
    if ((uint32_t)n > fill) n = (int32_t)fill;
    if (s->cap) s->rd = (s->rd + (uint32_t)n) % s->cap;
    s->rcount += (uint32_t)n;
    __atomic_fetch_sub(&s->fill, (uint32_t)n, __ATOMIC_RELEASE);
}

void pc_extstream_discard(pc_extstream_t *s)
{
    uint32_t n = LD(&s->fill);
    if (s->cap) s->rd = (s->rd + n) % s->cap;
    s->rcount += n;
    __atomic_fetch_sub(&s->fill, n, __ATOMIC_RELEASE);
    ST(&s->primed, 0);
}

int pc_extstream_render(pc_extstream_t *s, float *out, int frames, float gain_l, float gain_r)
{
    if (frames <= 0) return frames;
    uint32_t ep, cfg;
    memset(out, 0, (size_t)frames * 2 * sizeof *out);
    for (int tries = 0;; tries++) {            /* seqlock: the writer stores the format, then bumps the epoch; read the epoch, the format, the epoch again */
        ep = LD(&s->epoch);
        cfg = LD(&s->cfg);
        if (LD(&s->epoch) == ep) break;
        if (tries >= 2) return frames;         /* the writer is busy changing it: this block is silence */
    }
    if (ep != s->seen_epoch) {
        reader_flush(s);
        s->seen_cfg = cfg;
        ST(&s->seen_epoch, ep);
        ST(&s->primed, 0);
    } else if (cfg != s->seen_cfg) return frames;     /* the writer is between storing the format and requesting the flush: this block waits */
    if (!cfg || !s->cap) return frames;
    uint32_t rate = cfg >> 4, bytes = cfg & 15u, fb = 2u * bytes;
    float tl = clamp_gain(gain_l), tr = clamp_gain(gain_r);
    if (!LD(&s->primed)) {
        if (LD(&s->fill) / fb < (uint32_t)((uint64_t)rate * PC_EXTSTREAM_TARGET_MS / 1000u)) return frames;
        ST(&s->primed, 1);
        s->gl = s->gr = 0.0f;                  /* the first block after a (re)start ramps up from silence */
    }
    uint32_t avail = LD(&s->fill) / fb;
    int n = avail < (uint32_t)frames ? (int)avail : frames;
    const float inv = bytes == 2 ? 1.0f / 32768.0f : bytes == 3 ? 1.0f / 8388608.0f : 1.0f / 2147483648.0f;
    float gl0 = s->gl, gr0 = s->gr;
    uint32_t rd = s->rd;
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) / (float)frames;
        float gl = gl0 + (tl - gl0) * t, gr = gr0 + (tr - gr0) * t;
        for (int ch = 0; ch < 2; ch++) {
            uint8_t t[4];
            const uint8_t *p = s->buf + rd;
            if (rd + bytes > s->cap) { for (uint32_t k = 0; k < bytes; k++) t[k] = s->buf[(rd + k) % s->cap]; p = t; }   /* after a change of format the read index is not a multiple of the new frame size: a sample can straddle the ring's end */
            int32_t v;
            if (bytes == 2) v = (int16_t)(uint16_t)(p[0] | p[1] << 8);
            else if (bytes == 3) v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
            else v = (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
            out[2 * i + ch] = (float)v * inv * (ch ? gr : gl);
            rd += bytes;
            if (rd >= s->cap) rd -= s->cap;
        }
    }
    s->rd = rd;
    s->gl = tl;
    s->gr = tr;
    s->rcount += (uint32_t)n * fb;
    __atomic_fetch_sub(&s->fill, (uint32_t)n * fb, __ATOMIC_RELEASE);
    if (n < frames) {
        int k = n < 64 ? n : 64;               /* the ring ran dry mid-block: the last frames fade to zero instead of a cut */
        for (int i = 0; i < k; i++) { float g = (float)(k - 1 - i) / (float)k; out[2 * (n - k + i)] *= g; out[2 * (n - k + i) + 1] *= g; }
        s->underruns++;
        ST(&s->primed, 0);
    }
    return frames;
}

/* ---- feedback ---- */
#define FB_KP 2.0f        /* samples/s per frame of level error: with KI a critically damped loop, time constant 1 s (the spec's PI) */
#define FB_KI 1.0f        /* samples/s per frame-second */
#define FB_SPS_MAX 8000.0f   /* +- 1 sample per micro-frame */

uint32_t pc_extfb_nominal(uint32_t rate) { return (uint32_t)((((uint64_t)rate << 16) + 4000u) / 8000u); }
void pc_extfb_init(pc_extfb_t *f, uint32_t rate)
{
    memset(f, 0, sizeof *f);
    f->rate = rate;
    f->nominal = pc_extfb_nominal(rate);
    f->value = f->nominal;
    f->target = (uint32_t)((uint64_t)rate * PC_EXTSTREAM_TARGET_MS / 1000u);
}
uint32_t pc_extfb_update(pc_extfb_t *f, uint64_t now_us, uint32_t fill, bool playing)
{
    if (!f->t0) { f->t0 = f->t_last = now_us ? now_us : 1; f->last_fill = (float)fill; f->area = 0; return f->value; }
    float seg = 0.5f * (f->last_fill + (float)fill);                    /* the fill over the time since the last call: the mean of its ends */
    if (now_us > f->t_last) { f->area += seg * (float)(now_us - f->t_last) * 1e-6f; f->t_last = now_us; }
    f->last_fill = (float)fill;
    /* windows of exactly PC_EXTFB_WINDOW_US, tiled: the part of the last segment past a window's end belongs to the next one (the audio task reads 10 ms at a time: the sawtooth
     * of those reads averages out only over exactly one block, whatever the pump's jitter) */
    while (f->t_last - f->t0 >= PC_EXTFB_WINDOW_US) {
        const float T = (float)PC_EXTFB_WINDOW_US * 1e-6f;
        float over = (float)(f->t_last - (f->t0 + PC_EXTFB_WINDOW_US)) * 1e-6f;
        float e = (f->area - seg * over) / T - (float)f->target;
        if (playing) f->integ += e * T;        /* not while the ring is only filling: an empty ring would wind it up to the clamp */
        float imax = (float)f->rate * 0.002f / FB_KI;   /* +-2000 ppm of the rate: more than any crystal pair is off */
        if (f->integ > imax) f->integ = imax; else if (f->integ < -imax) f->integ = -imax;   /* anti-windup: the correction cannot pass the clamp below anyway */
        float corr = -(FB_KP * e + FB_KI * f->integ);
        if (corr > FB_SPS_MAX) corr = FB_SPS_MAX; else if (corr < -FB_SPS_MAX) corr = -FB_SPS_MAX;
        int64_t v = (int64_t)f->nominal + (int64_t)lrintf(corr * (65536.0f / 8000.0f));
        int64_t lo = (int64_t)f->nominal - 65536, hi = (int64_t)f->nominal + 65536;
        if (v < lo) v = lo; else if (v > hi) v = hi;
        f->value = (uint32_t)v;
        f->t0 += PC_EXTFB_WINDOW_US;
        f->area = seg * over;
    }
    return f->value;
}
