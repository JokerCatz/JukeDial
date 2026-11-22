// SPDX-License-Identifier: LGPL-2.1-or-later
/* Toy sound effects: the chiptune synth (pc_sfx.h). The sounds are the bank's text (app/main/sfx_bank.def); this file only plays a parsed bank.
 * Per sample a sounding voice costs a phase step, a wave lookup (plus the polyBLEP near a pulse edge, the AY envelope when it has one) and an
 * amplitude ramp; everything else (pitch, envelope) runs at a 1 ms control tick. */
#include "pc/pc_sfx.h"
#include <math.h>
#include <string.h>

const float pc_sfx_ay_vol[16] = { 0.0f, 0.0137f, 0.0205f, 0.0291f, 0.0423f, 0.0618f, 0.0847f, 0.1369f,
                                  0.1691f, 0.2647f, 0.3527f, 0.4499f, 0.5704f, 0.6873f, 0.8482f, 1.0f };

/* the NES triangle: 32 steps, 15 .. 0 .. 15 */
static const float TRI32[32] = {
    1.0f, 0.866667f, 0.733333f, 0.6f, 0.466667f, 0.333333f, 0.2f, 0.066667f, -0.066667f, -0.2f, -0.333333f, -0.466667f, -0.6f, -0.733333f, -0.866667f, -1.0f,
    -1.0f, -0.866667f, -0.733333f, -0.6f, -0.466667f, -0.333333f, -0.2f, -0.066667f, 0.066667f, 0.2f, 0.333333f, 0.466667f, 0.6f, 0.733333f, 0.866667f, 1.0f,
};

void pc_sfx_init(pc_sfx_t *s, const pc_sfx_bank_t *b)
{
    memset(s, 0, sizeof *s);
    s->bank = b;
    s->band = PC_SFX_BAND_DEFAULT;
}

static uint32_t at_rate(float ms, uint32_t rate) { return (uint32_t)(ms * 0.001f * (float)rate + 0.5f); }

static void note_begin(pc_sfx_voice_t *v)        /* the current note's boundaries at the voice's rate; the AY envelope restarts */
{
    const pc_sfx_track_t *t = v->tr;
    v->on_at = at_rate(v->ev->t_on, v->rate);
    v->off_at = at_rate(v->ev->t_off, v->rate);
    if (v->off_at <= v->on_at) v->off_at = v->on_at + 1;
    v->vamp = t->level * (t->ay ? pc_sfx_ay_vol[v->ev->vol & 15] : (float)(v->ev->vol & 15) / 15.0f);
    v->eph = 0.0f;
    v->ctl = 0;                                  /* a control tick at the note's first sample */
}

bool pc_sfx_start(pc_sfx_t *s, int id)
{
    const pc_sfx_bank_t *b = s->bank;
    if (!b || id < 0 || id >= b->n) return false;
    const pc_sfx_entry_t *e = &b->e[id];
    for (int k = 0; k < e->ntr; k++) {
        int pick = 0;
        for (int v = 0; v < PC_SFX_VOICES; v++) {                    /* a free voice, else the oldest */
            if (!s->v[v].tr) { pick = v; break; }
            if (s->v[v].order < s->v[pick].order) pick = v;
        }
        pc_sfx_voice_t *v = &s->v[pick];
        if (v->tr) s->stolen++;
        memset(v, 0, sizeof *v);
        v->tr = &e->tr[k];
        v->ev = &b->ev[e->tr[k].ev0];
        v->left = (uint16_t)(e->tr[k].nev - 1);
        v->lfsr = 1;
        v->nz = 1.0f;
        v->rate = 0;                                                  /* set by the first block (its rate) */
        v->order = ++s->order;
    }
    s->last[s->started % PC_SFX_LAST] = (uint8_t)id;
    s->started++;
    return true;
}

bool pc_sfx_active(const pc_sfx_t *s)
{
    for (int v = 0; v < PC_SFX_VOICES; v++) if (s->v[v].tr) return true;
    return false;
}

/* the envelope of a note at x ms (dur ms long, the release ends at dur): attack (linear), decay to the sustain level and release (squared) */
static float env_ad(const pc_sfx_track_t *t, float x)
{
    if (x < t->att) return x / t->att;
    x -= t->att;
    if (x < t->dec) { float u = 1.0f - x / t->dec; return t->sus + (1.0f - t->sus) * u * u; }
    return t->sus;
}
static float env_at(const pc_sfx_track_t *t, float x, float dur)
{
    if (x >= dur) return 0.0f;
    float rs = dur - t->rel;
    if (x < rs) return env_ad(t, x);
    float u = (dur - x) / t->rel;
    return env_ad(t, rs > 0.0f ? rs : 0.0f) * u * u;
}

/* one control tick (1 ms): this tick's pitch and the amplitude ramp to the envelope at its end */
static void tick(pc_sfx_voice_t *v, float inv_rate, uint16_t n)
{
    const pc_sfx_track_t *t = v->tr;
    float x = (float)(v->pos - v->on_at) * inv_rate;                  /* s since the note's start */
    float f = v->ev->hz, tr = x, base = f;
    if (t->rep_ms > 0.0f) {                                           /* ZzFX repeat: the program restarts, the jumps so far stay */
        float rep = t->rep_ms * 0.001f;
        float k = floorf(x / rep);
        tr = x - k * rep;
        if (t->jump_hz != 0.0f && t->jump_ms < t->rep_ms) base += k * t->jump_hz;
    }
    f = base * (t->oct != 0.0f ? exp2f(t->oct * tr) : 1.0f) + t->lin * tr + 0.5f * t->acc * tr * tr;
    if (t->jump_hz != 0.0f && tr * 1000.0f >= t->jump_ms) f += t->jump_hz;
    if (t->arp_n) {
        int i = (int)(x * 1000.0f / t->arp_ms) % t->arp_n;
        if (t->arp[i]) f *= exp2f((float)t->arp[i] / 12.0f);
    }
    if (t->vib > 0.0f && x * 1000.0f >= t->vib_ms) f *= 1.0f + t->vib * sinf(6.2831853f * t->vib_hz * (x - t->vib_ms * 0.001f));
    f = fabsf(f);
    float rate = 1.0f / inv_rate;
    float top = t->wave == PC_SFX_NOISE ? rate : 0.45f * rate;        /* tones below Nyquist (the polyBLEP needs inc < 0.5); noise at most a step a sample */
    if (f > top) f = top;
    v->inc = f * inv_rate;
    v->iinc = v->inc > 1e-9f ? 1.0f / v->inc : 0.0f;
    if (t->wave == PC_SFX_AYMIX) v->ninc = (t->nz_hz < rate ? t->nz_hz : rate) * inv_rate;
    if (t->env) v->einc = t->env_hz * inv_rate;
    float dur = (float)(v->off_at - v->on_at) * inv_rate * 1000.0f;
    float target = v->vamp * env_at(t, (x + (float)n * inv_rate) * 1000.0f, dur);
    v->damp = (target - v->amp) / (float)n;
    v->ctl = n;
}

static inline float blep(float t, float dt, float idt)   /* 2-sample polyBLEP residual at phase t of an edge at 0 */
{
    if (t < dt) { t *= idt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) * idt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

static inline void lfsr_step(pc_sfx_voice_t *v, int mode)
{
    uint32_t l = v->lfsr, b;
    if (mode == PC_SFX_NZ_AY) { b = (l ^ (l >> 3)) & 1u; l = (l >> 1) | (b << 16); }      /* AY: 17 bits, taps 0 and 3 */
    else { b = (l ^ (l >> (mode == PC_SFX_NZ_SHORT ? 6 : 1))) & 1u; l = (l >> 1) | (b << 14); }   /* NES: 15 bits, taps 0 and 1 (long) / 6 (short) */
    v->lfsr = l;
}

static inline float ay_env(int shape, pc_sfx_voice_t *v)          /* the AY's 16 envelope shapes (hold, alternate, attack, continue), 16 log steps */
{
    float c = v->eph;
    v->eph += v->einc;
    if (v->eph >= 2.0f) v->eph = (shape & 8) && !(shape & 1) ? v->eph - 2.0f : 1.5f;   /* repeating shapes wrap (parity kept), the others stay done */
    int cyc = (int)c;
    float fr = c - (float)cyc;
    bool up = shape & 4;
    if (cyc > 0) {
        if (!(shape & 8)) return 0.0f;                                /* one cycle, then silent */
        if (shape & 1) return (up != ((shape & 2) != 0)) ? 1.0f : 0.0f;   /* hold: the end of the first cycle, or its opposite */
        if ((shape & 2) && (cyc & 1)) up = !up;                       /* alternate */
    }
    float e = up ? fr : 1.0f - fr;
    return pc_sfx_ay_vol[(int)(e * 15.999f)];
}

static float voice_sample(pc_sfx_t *s, pc_sfx_voice_t *v, float inv_rate, uint16_t ctl_n)
{
    const pc_sfx_track_t *t = v->tr;
    if (!v->ctl) tick(v, inv_rate, ctl_n);
    v->ctl--;
    float w, ph = v->ph;
    switch (t->wave) {
    case PC_SFX_PULSE: {
        float d = (float)t->duty * (1.0f / 16.0f);
        w = ph < d ? 1.0f : -1.0f;
        if (s->band != PC_SFX_BAND_NAIVE) {
            float t2 = ph - d;
            if (t2 < 0.0f) t2 += 1.0f;
            w += blep(ph, v->inc, v->iinc) - blep(t2, v->inc, v->iinc);
            w = w > 1.0f ? 1.0f : w < -1.0f ? -1.0f : w;
        }
        break;
    }
    case PC_SFX_TRI: w = TRI32[(int)(ph * 32.0f) & 31]; break;
    case PC_SFX_WAVE: {                                               /* the GB wave table; its wrap (the saw's big edge) band-limited like a pulse edge */
        const float *tab = s->bank->wave[t->sub];
        w = tab[(int)(ph * 32.0f) & 31];
        if (s->band != PC_SFX_BAND_NAIVE) {
            w += 0.5f * (tab[0] - tab[31]) * blep(ph, v->inc, v->iinc);
            w = w > 1.0f ? 1.0f : w < -1.0f ? -1.0f : w;               /* the correction lands on the current step, not the wrap's: kept within +-1 */
        }
        break;
    }
    case PC_SFX_AYMIX:                                                /* the AY mixer with tone and noise on: high only when both are */
        w = (ph < 0.5f && (v->lfsr & 1u)) ? 0.75f : -0.25f;
        v->nph += v->ninc;
        if (v->nph >= 1.0f) { v->nph -= (float)(int)v->nph; lfsr_step(v, PC_SFX_NZ_AY); }
        break;
    default: w = v->nz; break;
    }
    ph += v->inc;
    if (ph >= 1.0f) {
        ph -= (float)(int)ph;
        if (t->wave == PC_SFX_NOISE) { lfsr_step(v, t->sub); v->nz = (v->lfsr & 1u) ? 1.0f : -1.0f; }
    }
    v->ph = ph;
    v->amp += v->damp;
    float a = v->amp;
    if (t->env) a *= ay_env(t->env - 1, v);
    return a * w;
}

bool pc_sfx_mix(pc_sfx_t *s, float *lr, int frames, uint32_t rate) { return pc_sfx_mix_gain(s, lr, frames, rate, 1.0f); }

pc_sfx_route_t pc_sfx_route(bool paused, float device, float fade)
{
    pc_sfx_route_t r = { false, 1.0f };
    if (!paused) return r;
    r.post = true;
    float d = device >= 1.0f ? 1.0f : device > 0.0f ? device : 0.0f;   /* NaN fails both tests: 0 */
    float f = fade >= 1.0f ? 1.0f : fade > 0.0f ? fade : 0.0f;
    r.gain = d * f;
    return r;
}

static inline float add_ceil(float m, float x)   /* music + toy sound, held at full scale (or at the music's own value beyond it) */
{
    float o = m + x, hi = m > PC_SFX_CEIL ? m : PC_SFX_CEIL, lo = m < -PC_SFX_CEIL ? m : -PC_SFX_CEIL;
    return o > hi ? hi : o < lo ? lo : o;
}

/* one sample of every sounding voice at the synth's rate (ir); *on: something sounded */
static float synth_one(pc_sfx_t *s, float inv_rate, uint16_t ctl_n, bool *on)
{
    float sum = 0.0f;
    *on = false;
    for (int k = 0; k < PC_SFX_VOICES; k++) {
        pc_sfx_voice_t *v = &s->v[k];
        if (!v->tr) continue;
        while (v->pos >= v->off_at) {                                 /* the note ended: the next one, or the voice is free */
            if (!v->left) { v->tr = NULL; break; }
            v->left--;
            v->ev++;
            note_begin(v);
        }
        if (!v->tr) continue;
        if (v->pos < v->on_at) { v->pos++; continue; }                /* waiting for its note */
        sum += voice_sample(s, v, inv_rate, ctl_n);
        v->pos++;
        *on = true;
    }
    if (!*on) return 0.0f;
    sum *= PC_SFX_PEAK;
    if (s->band == PC_SFX_BAND_BLEP_LP) { s->lp += s->lp_a * (sum - s->lp); sum = s->lp; }
    return sum > PC_SFX_CLAMP ? PC_SFX_CLAMP : sum < -PC_SFX_CLAMP ? -PC_SFX_CLAMP : sum;
}

/* The synth runs at the block's rate up to 48 kHz; above it at rate / m with m = ceil(rate / 48000) (88.2k and 96k: half, 176.4k and 192k: a
 * quarter: 44.1k or 48k), the output samples
 * between two synth samples linearly interpolated: the cost of a block stays about the cost at 48 kHz (chip voices have nothing above ~12 kHz
 * worth the price; the images of the interpolation are above 24 kHz and at least ~30 dB down). */
bool pc_sfx_mix_gain(pc_sfx_t *s, float *lr, int frames, uint32_t rate, float g)
{
    if (!rate || frames <= 0 || !pc_sfx_active(s)) { s->lp = s->cur = s->prev = 0.0f; s->sub = 0; return false; }
    const uint32_t m = (rate + 47999u) / 48000u, ir = rate / m;   /* the synth rate never above 48 kHz */
    const float inv_rate = 1.0f / (float)ir, inv_m = 1.0f / (float)m;
    const uint16_t ctl_n = (uint16_t)(ir / 1000u > 0 ? ir / 1000u : 1u);
    if (s->lp_rate != ir) { s->lp_rate = ir; s->lp_a = 1.0f - expf(-6.2831853f * 14000.0f * inv_rate); }   /* the NES's own ~14 kHz output low-pass */
    if (s->sub >= m) s->sub = 0;
    for (int k = 0; k < PC_SFX_VOICES; k++) {                         /* a rate change: the same time at the new rate */
        pc_sfx_voice_t *v = &s->v[k];
        if (!v->tr || v->rate == ir) continue;
        if (v->rate) v->pos = (uint32_t)((uint64_t)v->pos * ir / v->rate);
        v->rate = ir;
        note_begin(v);
    }
    bool any = false;
    for (int i = 0; i < frames; i++) {
        if (!s->sub) {
            bool on;
            s->prev = s->cur;
            s->cur = synth_one(s, inv_rate, ctl_n, &on);
            s->on = on || s->prev != 0.0f;                            /* a ramp from the last sounding sample still sounds */
        }
        float x = m == 1 ? s->cur : s->prev + (s->cur - s->prev) * ((float)s->sub * inv_m);
        if (++s->sub >= m) s->sub = 0;
        if (!s->on) continue;
        any = true;
        x *= g;
        lr[2 * i] = add_ceil(lr[2 * i], x);
        lr[2 * i + 1] = add_ceil(lr[2 * i + 1], x);
    }
    return any;
}

void pc_sfx_q_init(pc_sfx_q_t *q) { memset(q, 0, sizeof *q); }

/* single producer / single consumer: each index has one writer; the slot is written before w is published (release) and read after w is
 * seen (acquire); r is published only after the slot was read, so the producer never overwrites an unread slot */
bool pc_sfx_q_post(pc_sfx_q_t *q, uint8_t id, uint32_t now_ms)
{
    uint32_t w = __atomic_load_n(&q->w, __ATOMIC_RELAXED), r = __atomic_load_n(&q->r, __ATOMIC_ACQUIRE);
    if (w - r >= PC_SFX_QLEN) return false;
    q->id[w & (PC_SFX_QLEN - 1)] = id;
    q->ms[w & (PC_SFX_QLEN - 1)] = now_ms;
    __atomic_store_n(&q->w, w + 1, __ATOMIC_RELEASE);
    return true;
}

int pc_sfx_q_take(pc_sfx_q_t *q, uint32_t now_ms, uint32_t *stale)
{
    for (;;) {
        uint32_t r = __atomic_load_n(&q->r, __ATOMIC_RELAXED), w = __atomic_load_n(&q->w, __ATOMIC_ACQUIRE);
        if (r == w) return -1;
        int id = q->id[r & (PC_SFX_QLEN - 1)];
        uint32_t ms = q->ms[r & (PC_SFX_QLEN - 1)];
        __atomic_store_n(&q->r, r + 1, __ATOMIC_RELEASE);
        if ((int32_t)(now_ms - ms) > (int32_t)PC_SFX_STALE_MS) { if (stale) (*stale)++; continue; }   /* too old: dropped, not played late. Signed: a post
                                                                 * stamped after the consumer read its clock (the other core, a ms edge) is fresh, not 49 days old */
        return id;
    }
}
