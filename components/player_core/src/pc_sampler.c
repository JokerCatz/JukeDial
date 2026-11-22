// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_sampler.h"
#include <math.h>
#include <string.h>

#define Q15 32768

void pc_sampler_init(pc_sampler_t *s, uint32_t out_rate)
{
    memset(s, 0, sizeof *s);
    s->rate = out_rate;
    s->max = PC_SAMP_VOICES;
}

static void fade_out(pc_sampler_t *s, pc_svoice_t *v, int ms)
{
    int32_t n = (int32_t)(s->rate * (uint32_t)ms / 1000u);
    int32_t st = Q15 / (n > 0 ? n : 1);
    if (st < 1) st = 1;
    if (!v->fade_step || st > v->fade_step) v->fade_step = st;
}

/* 2^(n/12), n = 0..11: pitch without pow() on the audio task (16 note-ons in one block cost 0.3 ms with it) */
static const float SEMI[12] = { 1.0f, 1.0594631f, 1.1224620f, 1.1892071f, 1.2599210f, 1.3348399f,
                                1.4142136f, 1.4983071f, 1.5874011f, 1.6817928f, 1.7817974f, 1.8877486f };

int pc_sampler_note_on(pc_sampler_t *s, const pc_sample_t *smp, int note, int center, int vel, int ch, bool one_shot)
{
    if (vel <= 0 || !smp || !smp->frames) return -1;
    for (int i = 0; i < PC_SAMP_VOICES; i++)              /* the same note again: the old one gets out of the way */
        if (s->v[i].on && s->v[i].note == note && s->v[i].ch == ch) fade_out(s, &s->v[i], 2);
    int k = -1;
    for (int i = 0; i < s->max && k < 0; i++) if (!s->v[i].on) k = i;
    if (k < 0) {                                          /* take over the oldest */
        k = 0;
        for (int i = 1; i < s->max; i++) if (s->v[i].age < s->v[k].age) k = i;
    }
    pc_svoice_t *v = &s->v[k];
    int d = note - center, oct = d >= 0 ? d / 12 : -((11 - d) / 12);
    float r = SEMI[d - oct * 12] * ((float)smp->rate / (float)s->rate);   /* 0.08 .. 24, then octaves */
    if (oct > 0) r *= (float)(1 << (oct > 8 ? 8 : oct));
    else if (oct < 0) r /= (float)(1 << (-oct > 12 ? 12 : -oct));
    if (r > 64.0f) r = 64.0f;
    v->step_i = (uint32_t)r;
    v->step_f = (uint32_t)((r - (float)v->step_i) * 4294967296.0f);
    float g = (float)vel / 127.0f;
    v->s = smp;
    v->idx = v->frac = 0;
    v->gain = (int32_t)(g * g * 0.5f * Q15);
    v->fade = Q15;
    v->fade_step = 0;
    v->age = ++s->clock;
    v->note = (uint8_t)note;
    v->ch = (uint8_t)ch;
    v->one_shot = one_shot;
    v->on = true;
    return k;
}

void pc_sampler_note_off(pc_sampler_t *s, int ch, int note)
{
    for (int i = 0; i < PC_SAMP_VOICES; i++) {
        pc_svoice_t *v = &s->v[i];
        if (v->on && !v->one_shot && v->note == note && v->ch == ch) fade_out(s, v, 50);
    }
}

void pc_sampler_all_off(pc_sampler_t *s) { for (int i = 0; i < PC_SAMP_VOICES; i++) if (s->v[i].on) fade_out(s, &s->v[i], 2); }
void pc_sampler_kill(pc_sampler_t *s) { for (int i = 0; i < PC_SAMP_VOICES; i++) s->v[i].on = false; }

void pc_sampler_set_voices(pc_sampler_t *s, int n)
{
    s->max = n < 1 ? 1 : n > PC_SAMP_VOICES ? PC_SAMP_VOICES : n;
    for (int i = s->max; i < PC_SAMP_VOICES; i++) if (s->v[i].on) fade_out(s, &s->v[i], 2);
}
int pc_sampler_voices(const pc_sampler_t *s) { return s->max; }

int pc_sampler_active(const pc_sampler_t *s)
{
    int n = 0;
    for (int i = 0; i < PC_SAMP_VOICES; i++) n += s->v[i].on;
    return n;
}

void pc_sampler_render(pc_sampler_t *s, int32_t *mix, int frames)
{
    for (int i = 0; i < PC_SAMP_VOICES; i++) {
        pc_svoice_t *v = &s->v[i];
        if (!v->on) continue;
        const int16_t *d = v->s->data;
        uint32_t len = v->s->frames, idx = v->idx, frac = v->frac, si = v->step_i, sf = v->step_f;
        int32_t g = v->gain, fade = v->fade, fs = v->fade_step;
        bool st = v->s->channels == 2;
        int f = 0;
        for (; f < frames; f++) {
            if (idx >= len) { v->on = false; break; }
            int32_t gg = g;
            if (fs) {
                fade -= fs;
                if (fade <= 0) { v->on = false; break; }
                gg = g * fade >> 15;
            }
            uint32_t k1 = idx + 1 < len ? idx + 1 : idx;
            int32_t fr = (int32_t)(frac >> 17);                /* 15 bits: (b - a) * fr stays in 32 bits */
            int32_t l, r;
            if (st) {
                int32_t a = d[2 * idx], b = d[2 * k1], c = d[2 * idx + 1], e = d[2 * k1 + 1];
                l = a + ((b - a) * fr >> 15);
                r = c + ((e - c) * fr >> 15);
            } else {
                int32_t a = d[idx], b = d[k1];
                l = r = a + ((b - a) * fr >> 15);
            }
            mix[2 * f] += l * gg >> 15;
            mix[2 * f + 1] += r * gg >> 15;
            uint32_t nf = frac + sf;
            idx += si + (nf < frac);
            frac = nf;
        }
        v->idx = idx;
        v->frac = frac;
        v->fade = fade;
    }
}
