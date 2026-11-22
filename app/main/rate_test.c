// SPDX-License-Identifier: LGPL-2.1-or-later
/* The rate test service (hires T11, H18). One tier at a time, as pc_ratetest says: tone 1 (880 Hz) at the tier's 44.1-family rate, tone 2 (1320 Hz) at its
 * 48-family rate; each tone is a mixer on the audio service at that rate (a mixer is never halved, so the stored ceiling does not limit the test).
 * rate_test_poll (the page's 50 ms timer: the test only runs while its page is up) starts the tones on the timeline, ends a tier at its timeout and watches the output: the I2S underruns and failed
 * writes since a tone's own baseline (taken after its rate switch: a reconfigure may count one underrun before the first write), a drop of the DAC clock
 * ceiling (hw_cap) and a refused / failed mixer all end the test as a FAULT; a Bluetooth sink coming ends it as LEFT. The result (the last confirmed tier) is
 * stored only when the test ends by TOP, TIMEOUT or FAULT with a tier confirmed: dac_max_rate and audio_svc_set_cap. LEFT stores nothing.
 * The playing library track is remembered at the start (folder, index, byte) and resumed exactly once at the end; a play command of another owner during the
 * run ends it as LEFT and is not resumed over (its source plays). The tone state is a small PSRAM block per mixer. */
#include "rate_test.h"
#include <stdio.h>
#include <stdlib.h>
#include "audio_svc.h"
#include "cfg.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "mp3_ctl.h"
#include "pc/pc_rate.h"
#include "player_ctl.h"

#define BASE_MS PC_RT_BASELINE_MS  /* tone 2's own baseline (its rate switch) is taken this long after its start; tone 1's is the tier's (pc_ratetest_baseline_at) */

typedef struct { double phase, hz; uint32_t pos, len, rate; } tone_t;   /* the mixer's user block (PSRAM) */

static pc_ratetest_t s_rt;
static bool s_have, s_active, s_refused, s_ev_fault, s_want;
static int64_t s_base_us;                 /* the test's clock: ms since this */
static uint32_t s_hw0;                    /* hw_cap at the start */
static uint32_t s_tone_rate, s_tone_gen;  /* the mixer playing (rate 0: none yet) */
static int64_t s_tone_us;
static bool s_base_ok, s_tone2;
static uint32_t s_base_u, s_base_w;
static bool s_lib_paused;                 /* ... and it was paused: resumed paused */
static bool s_lib;                        /* a library track was playing at the start */
static int s_lib_folder, s_lib_idx;
static uint32_t s_lib_byte;
static const now_ops_t s_ops;

static int32_t now_ms(void) { return (int32_t)((esp_timer_get_time() - s_base_us) / 1000); }

static int render(float *out, int frames, void *u)          /* audio task */
{
    tone_t *t = u;
    pc_ratetest_render(out, frames, t->rate, &t->phase, &t->pos, t->len, t->hz);
    return frames;
}
static void release(void *u) { heap_caps_free(u); }

static void tone_sync(int32_t now)      /* the tone the timeline asks for now, started once */
{
    uint32_t r = pc_ratetest_rate_now(&s_rt, now);
    if (!r || r == s_tone_rate) return;
    tone_t *t = heap_caps_malloc(sizeof *t, MALLOC_CAP_SPIRAM);
    if (!t) { pc_ratetest_fault(&s_rt); return; }
    *t = (tone_t){ .hz = pc_ratetest_tone_hz(&s_rt, now), .len = (uint32_t)((uint64_t)r * PC_RT_TONE_MS / 1000), .rate = r };
    s_tone_rate = r;
    s_tone_us = esp_timer_get_time();
    s_base_ok = false;
    s_tone2 = t->hz == 1320;
    s_ev_fault = false;
    printf("APP ratetest tier=%lu tone=%lu\n", (unsigned long)s_rt.tiers[s_rt.tier], (unsigned long)t->hz);
    s_tone_gen = audio_svc_play_mixer(render, t, r, release, t);   /* a refused request released the block itself */
    if (!s_tone_gen) pc_ratetest_fault(&s_rt);
}

static const char *why_name(int e) { return e == PC_RT_END_TOP ? "top" : e == PC_RT_END_TIMEOUT ? "timeout" : e == PC_RT_END_FAULT ? "fault" : "left"; }

/* the run is DONE: ours = this service still owns playback (false: another owner took it, its source plays) */
static void conclude(bool ours)
{
    s_active = false;
    s_tone_rate = 0;
    s_tone_gen = 0;
    if (ours) {
        audio_svc_stop(false);
        player_ctl_release();
        player_ctl_changed();
    }
    bool store = s_rt.end != PC_RT_END_LEFT && s_rt.confirmed > 0;
    if (store) {
        cfg_set_int("dac_max_rate", (int)s_rt.confirmed);
        audio_svc_set_cap(s_rt.confirmed);
    }
    printf("APP ratetest end result=%lu why=%s\n", (unsigned long)(s_rt.end == PC_RT_END_LEFT ? 0 : s_rt.confirmed), why_name(s_rt.end));
    bool resume = s_lib && ours;
    s_lib = false;
    if (resume) {
        mp3_ctl_play_folder_track_at(s_lib_folder, s_lib_idx, s_lib_byte);   /* once */
        if (s_lib_paused) audio_svc_pause(true);                              /* a paused track comes back paused */
    }
}

void rate_test_poll(void)
{
    if (!s_active) return;
    int32_t now = now_ms();
    aud_info_t a;
    audio_svc_info(&a);
    if (a.sink) {                                              /* a Bluetooth sink came: the test is about the wire */
        pc_ratetest_leave(&s_rt);
        conclude(true);
        return;
    }
    if (a.hw_cap < s_hw0 || s_ev_fault) pc_ratetest_fault(&s_rt);          /* the DAC clock ceiling dropped ("APP hires off"), or the mixer failed */
    if (s_rt.state == PC_RT_RUN && s_tone_rate) {
        if (!s_base_ok) {
            /* counters are read once the switch to the tone's rate has settled: the reconfigure's own underruns (1-2 at a change into 96k) never count; the info
             * snapshot refreshes every ~160 ms, so a baseline 300 ms in includes them. Tone 1: the tier's rule (pure); tone 2 switches the rate again: its own 300 ms. */
            bool due = a.src_rate == s_tone_rate && (s_tone2 ? (esp_timer_get_time() - s_tone_us) / 1000 >= BASE_MS : pc_ratetest_baseline_at(&s_rt, now));
            if (due) {
                s_base_ok = true;
                s_base_u = a.underruns_src;
                s_base_w = a.wr_timeouts;
            }
        } else if (a.underruns_src > s_base_u || a.wr_timeouts > s_base_w) {
            pc_ratetest_fault(&s_rt);
        }
    }
    pc_ratetest_tick(&s_rt, now);
    if (s_rt.state == PC_RT_RUN) tone_sync(now);
    if (s_rt.state == PC_RT_DONE) conclude(true);
}

static void forget(void)                                       /* another owner took playback: the run ends, its source plays */
{
    if (!s_active) return;
    pc_ratetest_leave(&s_rt);
    conclude(false);
}
static void on_event(const aud_event_t *e) { if (s_active && e->gen == s_tone_gen && e->type == AUD_ERROR) s_ev_fault = true; }
static void title(char *out, size_t cap) { snprintf(out, cap, "Rate test"); }
static void stop_op(void) { rate_test_stop(); }
static bool playing(void) { return s_active; }
static const now_ops_t s_ops = { .title = title, .stop = stop_op, .playing = playing, .no_art = true };

bool rate_test_start(void)
{
    if (s_active) return true;
    aud_info_t a;
    audio_svc_info(&a);
    s_refused = a.sink;
    if (a.sink) {
        s_have = false;                                        /* the page shows the reason, not the last run */
        printf("APP ratetest refused: bluetooth\n");
        return false;
    }
    uint32_t max = a.hw_cap < PC_AUDIO_MAX_RATE ? a.hw_cap : PC_AUDIO_MAX_RATE;
    s_hw0 = a.hw_cap;
    s_lib = mp3_ctl_current() >= 0;
    if (s_lib) {
        aud_status_t st;
        audio_svc_status(&st);
        s_lib_folder = mp3_ctl_folder();
        s_lib_idx = mp3_ctl_current();
        s_lib_byte = st.pos_byte;
        s_lib_paused = st.state == AUD_PAUSED || audio_svc_paused();
    }
    s_base_us = esp_timer_get_time();
    pc_ratetest_init(&s_rt, max, 0);
    s_have = true;
    s_tone_rate = 0;
    s_tone_gen = 0;
    s_ev_fault = false;
    s_active = true;
    if (s_rt.state == PC_RT_DONE) { conclude(false); return true; }   /* no tier fits: nothing to play, nothing stored */
    player_ctl_take(&s_ops, forget, on_event);
    player_ctl_changed();
    tone_sync(0);
    if (s_rt.state == PC_RT_DONE) conclude(true);
    return true;
}

void rate_test_tap(void)
{
    if (!s_active) return;
    int32_t now = now_ms();
    if (!pc_ratetest_tap(&s_rt, now)) return;
    printf("APP ratetest confirm tier=%lu\n", (unsigned long)s_rt.confirmed);
    if (s_rt.state == PC_RT_RUN) { s_tone_rate = 0; tone_sync(now); }      /* the next tier's tone 1 at once */
    if (s_rt.state == PC_RT_DONE) conclude(true);
}

void rate_test_stop(void)
{
    if (!s_active) return;
    pc_ratetest_leave(&s_rt);
    conclude(true);
}

const pc_ratetest_t *rate_test_state(void) { return s_have ? &s_rt : NULL; }
bool rate_test_refused(void) { return s_refused; }
void rate_test_want(void) { s_want = true; }
bool rate_test_want_take(void) { bool w = s_want; s_want = false; return w; }

void rate_test_show(void)
{
    if (!s_have) { printf("APP ratetest state none refused=%d\n", s_refused); return; }
    printf("APP ratetest state=%s tier=%lu confirmed=%lu tiers=%d end=%s refused=%d\n", s_rt.state == PC_RT_RUN ? "run" : "done", (unsigned long)s_rt.tiers[s_rt.tier],
           (unsigned long)s_rt.confirmed, s_rt.ntiers, s_rt.state == PC_RT_RUN ? "-" : why_name(s_rt.end), s_refused);
}
