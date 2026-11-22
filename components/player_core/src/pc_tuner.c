// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_tuner.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool pc_pitch_detect(const int16_t *x, int n, uint32_t rate, float *scratch, pc_pitch_t *out)
{
    out->hz = 0.0f; out->clarity = 0.0f; out->rms = 0.0f;
    if (!x || !scratch || n < PC_PITCH_NEED || rate < 8000) return false;
    const int16_t *s = x + (n - PC_PITCH_NEED);
    float *v = scratch;                              /* PC_PITCH_NEED samples, mean removed */
    float *d = scratch + PC_PITCH_NEED;              /* raw difference d[0..TAU_MAX+1] */
    double sum = 0.0;
    for (int i = 0; i < PC_PITCH_NEED; i++) sum += s[i];
    const float mean = (float)(sum / PC_PITCH_NEED);
    double e = 0.0;
    for (int i = 0; i < PC_PITCH_NEED; i++) { v[i] = (float)s[i] - mean; e += (double)v[i] * v[i]; }
    out->rms = (float)sqrt(e / PC_PITCH_NEED);
    if (out->rms < PC_PITCH_GATE_RMS) return false;

    int tmin = (int)(rate / PC_PITCH_HZ_MAX);        /* 2 kHz */
    if (tmin < 2) tmin = 2;
    int tmax = (int)(rate / PC_PITCH_HZ_MIN);        /* 40 Hz */
    if (tmax > PC_PITCH_TAU_MAX) tmax = PC_PITCH_TAU_MAX;
    d[0] = 0.0f;
    float run = 0.0f, best_dp = 1e9f;
    int best = 0, found = 0;
    int last = tmax + 1;
    for (int tau = 1; tau <= last; tau++) {
        float acc = 0.0f;
        const float *a = v, *b = v + tau;
        for (int j = 0; j < PC_PITCH_WIN; j++) { float t = a[j] - b[j]; acc += t * t; }
        d[tau] = acc;
        run += acc;
        const float dp = run > 0.0f ? acc * (float)tau / run : 1.0f;
        if (tau >= tmin && !found) {
            if (dp < PC_PITCH_THRESHOLD) { found = 1; best = tau; best_dp = dp; }
            else if (dp < best_dp && tau <= tmax) { best_dp = dp; best = tau; }
        } else if (found == 1) {
            if (dp < best_dp) { best = tau; best_dp = dp; }          /* still walking down into the minimum */
            else { found = 2; break; }                               /* it rose: best is the local minimum, d[best+1] is known */
        }
    }
    if (best <= 1) return false;
    if (found == 0 && best_dp > 0.35f) return false; /* no dip: noise or an unvoiced sound */
    if (best >= last) return false;
    float t = (float)best;
    {
        /* parabola through the raw difference around the minimum (lags best-1, best, best+1; d[best+1] exists: a found minimum broke after it, the others stop below `last`) */
        const float y0 = d[best - 1], y1 = d[best], y2 = d[best + 1];
        const float den = y0 - 2.0f * y1 + y2;
        if (den > 1e-9f) {
            float dt = 0.5f * (y0 - y2) / den;
            if (dt > 1.0f) dt = 1.0f; else if (dt < -1.0f) dt = -1.0f;
            t += dt;
        }
    }
    out->hz = (float)rate / t;
    out->clarity = 1.0f - best_dp < 0.0f ? 0.0f : 1.0f - best_dp;
    if (out->hz < PC_PITCH_HZ_MIN || out->hz > PC_PITCH_HZ_MAX) { out->hz = 0.0f; return false; }
    return true;
}

float pc_note_hz(int midi, float a4) { return a4 * powf(2.0f, (float)(midi - 69) / 12.0f); }

int pc_note_from_hz(float hz, float a4, float *cents)
{
    if (!(hz > 0.0f) || !(a4 > 0.0f)) { if (cents) *cents = 0.0f; return 0; }
    const double m = 69.0 + 12.0 * log2((double)hz / (double)a4);
    int r = (int)floor(m + 0.5);
    if (r < 0) r = 0; else if (r > 127) r = 127;
    if (cents) { double c = (m - r) * 100.0; *cents = (float)(c > 50.0 ? 50.0 : c < -50.0 ? -50.0 : c); }
    return r;
}

const char *pc_note_name(int midi, char *buf, int n)
{
    static const char *const N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (midi < 0) midi = 0;
    snprintf(buf, (size_t)n, "%s%d", N[midi % 12], midi / 12 - 1);
    return buf;
}

bool pc_in_tune(float cents) { return cents <= PC_TUNE_CENTS && cents >= -PC_TUNE_CENTS; }

void pc_tone_set(pc_tone_t *t, int wave, float hz, bool on)
{
    t->wave = (uint8_t)(wave >= 0 && wave < PC_TONE_WAVES ? wave : PC_TONE_SINE);
    t->hz = hz;
    t->on = on && hz > 0.0f;
}
bool pc_tone_active(const pc_tone_t *t) { return t->on || t->amp > 0.0f; }

static inline float add_ceil(float m, float x)   /* the toy sound's rule (pc_sfx.c): held at full scale, or at the music's own value beyond it */
{
    float o = m + x, hi = m > 1.0f ? m : 1.0f, lo = m < -1.0f ? m : -1.0f;
    return o > hi ? hi : o < lo ? lo : o;
}
static inline float blep(float t, float dt)      /* polyBLEP residual at a step edge, t = phase distance after the edge */
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static float wave_at(uint8_t w, float ph, float dt)
{
    switch (w) {
    case PC_TONE_SINE: return sinf(6.2831853f * ph);
    case PC_TONE_SQ50: case PC_TONE_SQ25: {
        const float duty = w == PC_TONE_SQ50 ? 0.5f : 0.25f;
        float y = ph < duty ? 1.0f : -1.0f;
        y += blep(ph, dt);
        float p2 = ph - duty; if (p2 < 0.0f) p2 += 1.0f;
        y -= blep(p2, dt);
        return y;
    }
    case PC_TONE_TRI: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    default: { float y = 2.0f * ph - 1.0f; return y - blep(ph, dt) * 1.0f; }   /* saw */
    }
}
bool pc_tone_mix(pc_tone_t *t, float *lr, int frames, uint32_t rate, float g)
{
    if (!rate || frames <= 0 || !pc_tone_active(t)) return false;
    const float dt = t->hz / (float)rate;
    const float step = 1.0f / (0.010f * (float)rate);    /* 10 ms ramp */
    for (int i = 0; i < frames; i++) {
        if (t->on) { t->amp += step; if (t->amp > 1.0f) t->amp = 1.0f; }
        else { t->amp -= step; if (t->amp <= 0.0f) { t->amp = 0.0f; break; } }
        float y = wave_at(t->wave, t->ph, dt) * t->amp * PC_TONE_PEAK * g;
        t->ph += dt;
        if (t->ph >= 1.0f) t->ph -= 1.0f;
        lr[2 * i] = add_ceil(lr[2 * i], y);
        lr[2 * i + 1] = add_ceil(lr[2 * i + 1], y);
    }
    if (t->amp <= 0.0f && !t->on) t->ph = 0.0f;
    return true;
}
