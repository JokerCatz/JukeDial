// SPDX-License-Identifier: LGPL-2.1-or-later
/* Test tone (console `tone HZ`, `tone 0` stops): a sine at -6 dBFS, 48 kHz, as a playback owner, so it goes
 * through the whole output path (volume, effects, the Bluetooth encoder) and the test peer can check what it
 * decodes. Not in the UI. */
#include "test_tone.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "audio_svc.h"
#include "player_ctl.h"

#define TONE_RATE 48000
typedef struct { float c, s, cr, sr; } tone_t;   /* a unit phasor turned by one step per sample */
static uint32_t s_gen;
static int s_hz;
static const now_ops_t s_ops;

static int render(float *out, int frames, void *u)          /* audio task: no double maths (no double FPU) */
{
    tone_t *t = u;
    for (int i = 0; i < frames; i++) {
        float v = 0.5f * t->s;
        out[2 * i] = out[2 * i + 1] = v;
        float c = t->c * t->cr - t->s * t->sr;
        t->s = t->s * t->cr + t->c * t->sr;
        t->c = c;
    }
    float m = 1.5f - 0.5f * (t->c * t->c + t->s * t->s);         /* back to |1| once a block */
    t->c *= m;
    t->s *= m;
    return frames;
}
static void release(void *u) { free(u); }
static void forget(void) { s_hz = 0; }
static void on_event(const aud_event_t *e) { if (e->gen == s_gen && s_hz) { s_hz = 0; player_ctl_release(); } }

int test_tone_play(int hz)
{
    if (hz <= 0) {
        if (s_hz) { audio_svc_stop(false); s_hz = 0; player_ctl_release(); player_ctl_changed(); }
        return 0;
    }
    if (hz >= TONE_RATE / 2) return -1;
    tone_t *t = calloc(1, sizeof *t);
    if (!t) return -1;
    t->c = 1;
    t->cr = (float)cos(2 * M_PI * hz / TONE_RATE);
    t->sr = (float)sin(2 * M_PI * hz / TONE_RATE);
    player_ctl_take(&s_ops, forget, on_event);
    s_gen = audio_svc_play_mixer(render, t, TONE_RATE, release, t);
    if (!s_gen) { player_ctl_release(); player_ctl_changed(); return -1; }
    s_hz = hz;
    printf("APP tone %d Hz -6 dBFS rate=%d\n", hz, TONE_RATE);
    player_ctl_changed();
    return 0;
}

static void title(char *out, size_t cap) { snprintf(out, cap, "Test tone %d Hz", s_hz); }
static void stop(void) { test_tone_play(0); }
static bool playing(void) { return s_hz > 0; }
static const now_ops_t s_ops = { .title = title, .stop = stop, .playing = playing, .no_art = true };
