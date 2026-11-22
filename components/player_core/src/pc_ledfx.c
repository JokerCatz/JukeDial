// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_ledfx.h"
#include <math.h>
#include <string.h>

static const char *const NAMES[PC_FX_COUNT] = { "pitch", "rainbow", "ripple", "comet", "spectrum", "off" };
const char *pc_fx_name(pc_fx_mode_t m) { return m >= 0 && m < PC_FX_COUNT ? NAMES[m] : NAMES[0]; }
pc_fx_mode_t pc_fx_parse(const char *s)
{
    for (int i = 0; s && i < PC_FX_COUNT; i++) if (!strcmp(s, NAMES[i])) return (pc_fx_mode_t)i;
    return PC_FX_PITCH;
}

#define PITCH_FALL 2.0f        /* per second: full to dark in 500 ms (as stage 1) */
#define RAINBOW_FALL 1.4f
#define GLOW 0.35f             /* rainbow: the neighbours */
#define MARK_FALL 2.0f
#define WAVE_SPEED 14.0f       /* segments per second */
#define WAVE_LIFE 0.9f
#define COMET_SPEED 12.0f
#define COMET_LIFE 1.6f
#define COMET_TAIL 3.5f

static float clamp01(float x) { return x > 0 ? (x < 1 ? x : 1) : 0; }

uint32_t pc_fx_mix(uint32_t a, uint32_t b, float t)
{
    t = clamp01(t);
    uint32_t o = 0;
    for (int s = 16; s >= 0; s -= 8) {
        float x = (float)(a >> s & 255), y = (float)(b >> s & 255);
        o |= (uint32_t)lroundf(x + (y - x) * t) << s;
    }
    return o;
}

int pc_fx_note_seg(int note) { return note < 0 || note > 127 ? -1 : note % 12; }

uint32_t pc_fx_pc_color(int note)
{
    int h = (note % 12) * 30;                           /* C red, G cyan-ish ... around the wheel */
    float s = 0.75f, c = s, x = c * (1 - fabsf(fmodf((float)h / 60.0f, 2.0f) - 1)), m = 1 - c, r = 0, g = 0, b = 0;
    switch (h / 60) {
    case 0: r = c, g = x; break;
    case 1: r = x, g = c; break;
    case 2: g = c, b = x; break;
    case 3: g = x, b = c; break;
    case 4: r = x, b = c; break;
    default: r = c, b = x; break;
    }
    return (uint32_t)lroundf((r + m) * 255) << 16 | (uint32_t)lroundf((g + m) * 255) << 8 | (uint32_t)lroundf((b + m) * 255);
}

void pc_fx_init(pc_ledfx_t *f, pc_fx_mode_t m)
{
    memset(f, 0, sizeof *f);
    f->mode = m >= 0 && m < PC_FX_COUNT ? m : PC_FX_PITCH;
    f->last_note = -1;
}

void pc_fx_set_mode(pc_ledfx_t *f, pc_fx_mode_t m)
{
    float mark[PC_FX_SEGS];
    uint32_t mcol[PC_FX_SEGS];
    memcpy(mark, f->mark, sizeof mark);
    memcpy(mcol, f->mcol, sizeof mcol);
    pc_fx_init(f, m);
    memcpy(f->mark, mark, sizeof mark);
    memcpy(f->mcol, mcol, sizeof mcol);
}

static void light(pc_ledfx_t *f, int k, float lv, uint32_t col)
{
    if (k < 0 || k >= PC_FX_SEGS || lv < f->lvl[k]) return;
    f->lvl[k] = lv;
    f->col[k] = col;
}

static void spawn(pc_fx_mover_t *pool, int n, float pos, float amp, float dir, uint32_t col)
{
    int k = 0;
    for (int i = 0; i < n; i++) {                       /* a free one, else the oldest */
        if (pool[i].amp <= 0) { k = i; break; }
        if (pool[i].age > pool[k].age) k = i;
    }
    pool[k] = (pc_fx_mover_t){ pos, 0, amp, dir, col };
}

void pc_fx_note(pc_ledfx_t *f, int note, int vel, uint32_t col)
{
    int k = pc_fx_note_seg(note);
    if (k < 0) return;
    float lv = 0.25f + 0.75f * (float)(vel < 0 ? 0 : vel > 127 ? 127 : vel) / 127.0f;   /* soft notes still show */
    switch (f->mode) {
    case PC_FX_PITCH: light(f, k, lv, col); break;
    case PC_FX_RAINBOW:
        col = pc_fx_pc_color(note);
        light(f, k, lv, col);
        light(f, k - 1, lv * GLOW, col);
        light(f, k + 1, lv * GLOW, col);
        break;
    case PC_FX_RIPPLE: spawn(f->wave, PC_FX_WAVES, (float)k, lv, 1, col); break;
    case PC_FX_COMET:
        spawn(f->comet, PC_FX_COMETS, (float)k, lv, f->last_note >= 0 && note < f->last_note ? -1.0f : 1.0f, col);
        break;
    default: break;                                    /* spectrum, off: notes do not draw */
    }
    f->last_note = note;
}

void pc_fx_mark(pc_ledfx_t *f, int seg, uint32_t col, float level)
{
    if (seg < 0 || seg >= PC_FX_SEGS) return;
    level = clamp01(level);
    if (level < f->mark[seg]) return;
    f->mark[seg] = level;
    f->mcol[seg] = col;
}

void pc_fx_spectrum(pc_ledfx_t *f, const float *bars, int n)
{
    for (int k = 0; k < PC_FX_SEGS; k++) {
        int a = k * n / PC_FX_SEGS, b = (k + 1) * n / PC_FX_SEGS;
        float v = 0;
        for (int i = a; i < b; i++) if (bars[i] > v) v = bars[i];
        v = clamp01((v - 0.3f) / 0.7f);                 /* -42..0 dB: chip harmonics fill the -60 dB floor */
        f->band[k] += (v - f->band[k]) * (v > f->band[k] ? 0.6f : 0.15f);   /* rise fast, fall slowly */
    }
}

static void age(pc_fx_mover_t *pool, int n, float dt, float speed, float life, float lo, float hi)
{
    for (int i = 0; i < n; i++) {
        pc_fx_mover_t *m = &pool[i];
        if (m->amp <= 0) continue;
        m->age += dt;
        m->pos += m->dir * speed * dt;
        if (m->age >= life || (lo < hi && (m->pos < lo || m->pos > hi))) m->amp = 0;
    }
}

void pc_fx_tick(pc_ledfx_t *f, int ms)
{
    float dt = (float)ms / 1000.0f, d = (f->mode == PC_FX_RAINBOW ? RAINBOW_FALL : PITCH_FALL) * dt;
    for (int k = 0; k < PC_FX_SEGS; k++) {
        f->lvl[k] = f->lvl[k] > d ? f->lvl[k] - d : 0;
        f->mark[k] = f->mark[k] > MARK_FALL * dt ? f->mark[k] - MARK_FALL * dt : 0;
    }
    age(f->wave, PC_FX_WAVES, dt, 0, WAVE_LIFE, 0, 0);   /* ripples stay where they started; their radius grows */
    age(f->comet, PC_FX_COMETS, dt, COMET_SPEED, COMET_LIFE, -COMET_TAIL, PC_FX_SEGS - 1 + COMET_TAIL);
    if (f->mode != PC_FX_SPECTRUM) memset(f->band, 0, sizeof f->band);
}

bool pc_fx_busy(const pc_ledfx_t *f)
{
    for (int k = 0; k < PC_FX_SEGS; k++)
        if (f->lvl[k] > 0 || f->mark[k] > 0 || (f->mode == PC_FX_SPECTRUM && f->band[k] > 0.01f)) return true;
    for (int i = 0; i < PC_FX_WAVES; i++) if (f->wave[i].amp > 0) return true;
    for (int i = 0; i < PC_FX_COMETS; i++) if (f->comet[i].amp > 0) return true;
    return false;
}

static float rel_lum(uint32_t c)
{
    return 0.2126f * powf((float)(c >> 16 & 255) / 255.0f, 2.2f) + 0.7152f * powf((float)(c >> 8 & 255) / 255.0f, 2.2f) + 0.0722f * powf((float)(c & 255) / 255.0f, 2.2f);
}
/* a lit LED on a light page: its colour darkened toward the text colour until it holds 3:1 against the page (hue kept); a dark theme: unchanged
 * (the same rule as ui_hue_on) */
uint32_t pc_fx_ink(const pc_theme_t *th, uint32_t c)
{
    if (!pc_theme_is_light(th)) return c;
    const float lb = rel_lum(th->bg) + 0.05f;
    for (int i = 0; i < 12; i++) {
        const float lc = rel_lum(c) + 0.05f;
        if ((lc > lb ? lc / lb : lb / lc) >= 3.0f) break;
        c = pc_fx_mix(c, th->fg, 0.1875f);
    }
    return c;
}

void pc_fx_render(const pc_ledfx_t *f, uint32_t unlit, pc_ramp_t ramp, const pc_theme_t *th, uint32_t out[PC_FX_SEGS])
{
    float lv[PC_FX_SEGS];
    uint32_t col[PC_FX_SEGS];
    memcpy(lv, f->lvl, sizeof lv);
    memcpy(col, f->col, sizeof col);
    for (int i = 0; i < PC_FX_WAVES; i++) {            /* a ring growing both ways, fading as it goes */
        const pc_fx_mover_t *w = &f->wave[i];
        if (w->amp <= 0) continue;
        float r = WAVE_SPEED * w->age, a = w->amp * (1 - w->age / WAVE_LIFE);
        for (int k = 0; k < PC_FX_SEGS; k++) {
            float v = a * (1 - fabsf(fabsf((float)k - w->pos) - r) / 1.2f);
            if (v > lv[k]) { lv[k] = v; col[k] = w->col; }
        }
    }
    for (int i = 0; i < PC_FX_COMETS; i++) {           /* a sharp head, a tail behind it */
        const pc_fx_mover_t *c = &f->comet[i];
        if (c->amp <= 0) continue;
        float a = c->amp * sqrtf(1 - c->age / COMET_LIFE);
        for (int k = 0; k < PC_FX_SEGS; k++) {
            float u = (c->pos - (float)k) * c->dir;     /* > 0: behind the head */
            float v = a * (u < 0 ? 1 + 2 * u : 1 - u / COMET_TAIL);
            if (v > lv[k]) { lv[k] = v; col[k] = c->col; }
        }
    }
    if (f->mode == PC_FX_SPECTRUM)
        for (int k = 0; k < PC_FX_SEGS; k++)
            if (f->band[k] > lv[k]) { lv[k] = f->band[k]; col[k] = pc_ramp_color_ex(ramp, th, f->band[k], false); }
    for (int k = 0; k < PC_FX_SEGS; k++) {
        if (f->mark[k] > lv[k]) { lv[k] = f->mark[k]; col[k] = f->mcol[k]; }
        out[k] = lv[k] > 0 ? pc_fx_mix(unlit, pc_fx_ink(th, col[k]), lv[k]) : unlit;
    }
}
