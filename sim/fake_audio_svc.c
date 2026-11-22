// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
/* Fakes audio_svc (I2S output task on core 1: decoding, mixing, volume, spectrum). */
/* Behaviour: the play state machine on the virtual clock (position, pause, NEAR / NEXT / ENDED / FADED events, gapless queue, release of sources); spectrum from a synthetic signal through the real pc_spectrum_bars; the toy sound effects through the real sfx_mix into a scratch block (counted, not heard). */
/* Paused query mirrors the firmware: audio_svc_paused() is the last pause command (a play, mixer or stop clears it), true even with no source loaded, as on the board. */
/* Empty: all decoding and output (render is never called), volume, sink (only its plan: simsink; plus the Bluetooth resampler table model, rsq_model below), post hook, per-source gain, sub-tracks. A source whose backend cannot open still plays (180 s);
 * "simaudiorate <hz> <n>" makes the next n decoder sources report that rate; the firmware's accept decision and plan follow (pc_src_rate, then pc_pipe_plan with
 * the firmware's pc_plan_args, burst 0): above PC_SRC_MAX_RATE (or a plan of 0) refused as on the board (the APP play refused line, then AUD_ERROR), else the
 * APP pipe line once per plan change; "simsink 1|0" is the fake's sink flag (the plan for a sink, 48k: re-planned at once, the board does it at the next block);
 * "simaudiofail <n>" makes the next n decoder sources end with AUD_ERROR at the next tick instead (the board's undecodable file).
 * hires T11, the fake DAC of the rate test: "simdac <hz>" = a listener who taps rate_test_tap() 200 ms after a tone 2 (a mixer at 48000 / 96000 / 192000 / 384000) of a
 * tier <= hz started (a tone that underruns, see below, is not confirmed: the listener hears the break); "simdac fault <hz>" = audio_svc_info().underruns_src grows by one per tick while a mixer at a rate >= hz plays (reset when a source starts);
 * "simdac glitch" = one underrun: at once while a mixer plays, else at the start of the next mixer (a reconfigure's own underrun: the rate test must not count it);
 * "simhw <hz>" = the DAC clock ceiling drops to hz (audio_svc_info().hw_cap; >= PC_AUDIO_MAX_RATE restores it). "simdac off" clears the DAC settings. The tiers themselves come from hw_cap (PC_AUDIO_MAX_RATE). audio_svc_status().pos_byte follows the stream's average rate. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_svc.h"
#include "bsp/bsp_audio.h"
#include "cfg.h"
#include "pc/pc_dacmax.h"
#include "esp_timer.h"
#include "pc/pc_pipe_rules.h"
#include "pc/pc_lowrate.h"
#include "pc/pc_rate.h"
#include "pc/pc_vmute.h"
#include "pc/pc_volsel.h"
#include "pc/pc_rsq.h"
#include "pc/pc_spectrum.h"
#include "rate_test.h"
#include "sfx.h"
#include "sim.h"
#include "out_status.h"

#define SPEC_N 1024                     /* same ring length as the board */
#define DEFAULT_LEN_MS 180000           /* a source whose backend does not know its length */
#define FADE_MS 10000
#define TICK_US 20000

typedef struct {
    bool used;
    const pc_backend_t *be;
    pc_reader_t r;
    aud_opts_t o;
    aud_release_fn release;
    void *release_arg;
    uint32_t gen;
    int32_t len_ms;                     /* -1: no end (a mixer) */
    uint32_t rate;                      /* the source's own rate */
    pc_pipe_t plan;                     /* its pipeline (pc_pipe_plan); a mixer: { rate, 0, native } */
    uint32_t burst;                     /* the source's largest read (simaudiorate's optional third argument; the board: the backend's burst, pc_pipe_plan's native-at-192k rule) */
    uint32_t cap;                       /* the ceiling it was planned with (the board: P.cap, taken when the source opens) */
    const char *fmt;                    /* audio_svc_info().fmt: the backend's name, "Synth" for a mixer */
    uint8_t bits;                       /* the backend's bits() */
    uint32_t kbps;                      /* reader size * 8 / length, 0 when unknown (as the board) */
    bool near_sent;
    bool refused;                       /* its rate is above the limit: it ends with AUD_ERROR at the next tick (the firmware: a failed play) */
} src_t;

static src_t s_cur, s_nxt;
static uint32_t s_cap_req = PC_AUDIO_MAX_RATE;   /* audio_svc_set_cap; applied to a source when it opens, as the board (P.cap) */
static aud_state_t s_state = AUD_IDLE;
static uint32_t s_gen;
static QueueHandle_t s_evt;
static esp_timer_handle_t s_timer;
static int64_t s_base_us, s_since_us;   /* position = s_base_us + (now - s_since_us) while playing */
static int32_t s_last_pos_ms, s_last_len_ms;
static int64_t s_fade_end_us;           /* 0: not fading */
static int s_vol;
static uint32_t s_dac_ok, s_dac_fault;   /* simdac: the listener's ceiling and the rate from which the output underruns (0: off) */
static bool s_glitch_armed;             /* simdac glitch: an underrun at the next mixer start */
static uint32_t s_hw_cap = PC_AUDIO_MAX_RATE;   /* simhw */
static uint32_t s_underruns;             /* audio_svc_info().underruns_src: since the current source started */
static int64_t s_listen_us;              /* the listener taps at this time (0: none armed) */
static uint32_t s_listen_gen;

static void emit(aud_state_t type, uint32_t gen)
{
    aud_event_t e = { .type = type, .gen = gen };
    xQueueSend(s_evt, &e, 0);
}

static void drop(src_t *s)
{
    if (!s->used) return;
    s->used = false;
    if (s->release) s->release(s->release_arg);
}

static int64_t pos_us(void)
{
    int64_t p = s_base_us + (s_state == AUD_PLAYING ? sim_now_us() - s_since_us : 0);
    if (s_cur.used && s_cur.len_ms >= 0 && p > (int64_t)s_cur.len_ms * 1000) p = (int64_t)s_cur.len_ms * 1000;
    return p;
}

/* Fill a source: ask the backend (open, length, rate, close at once - nothing is ever read from it) */
static uint32_t s_rate_over;             /* simaudiorate: the rate the next s_rate_left decoder sources report */
static int s_rate_left;
static uint32_t s_burst_over;            /* simaudiorate's burst for those sources (0: none, the fake reads nothing) */
bool fake_audio_rate_next(uint32_t hz, int n, uint32_t burst) { if (n < 0) return false; s_rate_over = hz; s_rate_left = n; s_burst_over = burst; return true; }

static bool s_sink;                      /* simsink / audio_svc_set_sink: the output is a sink (the board: the audio task's per-block snapshot) */
static uint32_t s_src_logged = 44100;    /* the plan the last APP pipe line showed; boot: 44.1k native, as the board */
static pc_pipe_t s_plan_logged = { 44100, 0, PC_PIPE_NATIVE };

/* the firmware's plan (audio_svc.c plan_of): the same pc_plan_args + pc_pipe_plan. cap = the ceiling the source opened with (dac_max_rate); the fake DAC takes every
 * clock (hw_cap = PC_AUDIO_MAX_RATE); the burst is the one simaudiorate gave (default 0: the fake reads nothing from a source; above 96k a burst over the knob's limit halves, hires T12) */
#ifdef SIM_FP
#define SIM_DAC_CAPS (&pc_dac_caps_fakepod)
#else
#define SIM_DAC_CAPS (&pc_dac_caps_round)
#endif
static pc_pipe_t plan_of(uint32_t rate, uint32_t cap, uint32_t burst)
{
    pc_plan_args_t a = pc_plan_args(rate, burst, s_sink, cap, PC_AUDIO_MAX_RATE);
    pc_dac_clamp_args(SIM_DAC_CAPS, &a);
    pc_pipe_t p = pc_pipe_plan(rate, a.out_max, burst, a.limit_why);
    return pc_dac_fix(SIM_DAC_CAPS, rate, pc_dac_fix_max(SIM_DAC_CAPS, s_sink, cap, PC_AUDIO_MAX_RATE), s_sink, p, NULL);   /* LOWRATE: the firmware's fix_plan (the same shared rule, the board's rate table) */
}

static void plan_log(const src_t *s)     /* the board's line, once per plan change */
{
    if (s->rate == s_src_logged && s->plan.pipe == s_plan_logged.pipe && s->plan.halvings == s_plan_logged.halvings && s->plan.why == s_plan_logged.why) return;
    s_src_logged = s->rate;
    s_plan_logged = s->plan;
    printf("APP pipe src=%lu pipe=%lu halvings=%u why=%s\n", (unsigned long)s->rate, (unsigned long)s->plan.pipe, (unsigned)s->plan.halvings,
           pc_pipe_why_name(s->plan.why));
}

static uint32_t s_low_rate;
static bool s_low_hard;
uint32_t audio_svc_take_low_rate(void) { uint32_t r = s_low_rate; s_low_rate = 0; return r; }
bool audio_svc_low_is_hard(void) { return s_low_hard; }
uint8_t audio_svc_take_dac_note(uint32_t *arg) { if (arg) *arg = 0; return 0; }
/* the firmware's accept decision (audio_svc.c src_rate + plan_set): a rate of 0 is the fake's "unknown" (44.1 kHz); above PC_SRC_MAX_RATE or a plan of 0 is refused */
static bool refuse_rate(uint32_t rate, uint32_t burst)
{
    if (!rate) return false;
#ifdef SIM_FP
    if (pc_dac_over_hard(SIM_DAC_CAPS, s_sink, rate, pc_dacmax_norm(cfg_int("dac_hard_rate", 0), 384000))) {   /* DACFS: the firmware's plan_set hard limit */
        printf("APP dac rate: %lu Hz is above the hard limit (dac_hard_rate): source refused\n", (unsigned long)rate);
        s_low_rate = rate; s_low_hard = true;
        return true;
    }
#endif
    if (rate <= PC_SRC_MAX_RATE && pc_dac_unsupported(SIM_DAC_CAPS, plan_of(rate, s_cap_req, burst).pipe, s_sink)) {   /* the firmware's plan_set: no rate fits */
        printf("APP dac rate: %lu Hz is not a rate the DAC takes (no resampling for it): source refused\n", (unsigned long)rate);
        s_low_rate = rate; s_low_hard = false;
        return true;
    }
    if (rate <= PC_SRC_MAX_RATE && plan_of(rate, s_cap_req, burst).pipe) return false;
    printf("APP play refused rate=%lu max=%d\n", (unsigned long)rate, PC_SRC_MAX_RATE);
    return true;
}

static int32_t s_len_over_ms;            /* simaudiolen: the length the next s_len_left sources get (a song that ends early) */
static int s_len_left;
bool fake_audio_len_next(int ms, int n) { if (ms <= 0 || n < 0) return false; s_len_over_ms = ms; s_len_left = n; return true; }

static bool refuse_mixer(uint32_t rate)  /* CMD_MIXER: a mixer is never halved, its own rate needs a clock row (pc_mixer_rate_ok) */
{
    if (!rate || pc_mixer_rate_ok(rate)) return false;
    printf("APP play refused rate=%lu max=%d\n", (unsigned long)rate, PC_AUDIO_MAX_RATE);
    return true;
}

static void fill(src_t *s, const pc_backend_t *be, const pc_reader_t *r, const aud_opts_t *o, aud_release_fn rel, void *arg, uint32_t gen)
{
    memset(s, 0, sizeof *s);
    s->used = true;
    s->be = be;
    s->r = *r;
    if (r->u == (const void *)r) s->r.u = &s->r;      /* a memory reader points at itself */
    s->o = o ? *o : (aud_opts_t){ .gain = 1.0f };
    s->release = rel;
    s->release_arg = arg;
    s->gen = gen;
    s->len_ms = DEFAULT_LEN_MS;
    s->rate = 44100;
    s->fmt = be && be->name ? be->name : "?";
    void *h = NULL;
    if (be && be->open && be->open(&h, &s->r, 0) == 0) {
        int32_t len = be->length_ms ? be->length_ms(h) : -1;
        uint32_t rate = be->rate ? be->rate(h) : 0;
        if (s_rate_left > 0) { s_rate_left--; rate = s_rate_over; s->burst = s_burst_over; }
        s->refused = refuse_rate(rate, s->burst);
        if (len > 0) s->len_ms = len;
        s->bits = be->bits ? be->bits(h) : 0;
        s->kbps = len > 0 && r->size ? (uint32_t)((uint64_t)r->size * 8 / (uint64_t)len) : 0;
        if (rate) s->rate = rate;
        if (be->close) be->close(h);
    }
    if (s_len_left > 0) { s_len_left--; s->len_ms = s_len_over_ms; }
    s->plan = s->refused ? (pc_pipe_t){ 0, 0, PC_PIPE_NATIVE } : plan_of(s->rate, s->cap = s_cap_req, s->burst);
}

static int s_fail_next;                  /* simaudiofail: decoder sources still to fail */
bool fake_audio_fail_next(int n) { if (n < 0) return false; s_fail_next = n; return true; }
void fake_audio_underrun(void) { s_underruns++; }   /* simunderrun (hires T8e): one underrun of the playing source */
static int s_xfade_ms;                   /* the crossfade length asked for (audio_svc_set_xfade) */
static uint32_t s_xf_gen;                /* the queued track whose AUD_XFADE has been sent */
static bool s_cur_fails;                 /* the current source ends with AUD_ERROR at the next tick */

static void begin(void)                 /* the current source starts from 0 */
{
    s_state = AUD_PLAYING;
    s_base_us = 0;
    s_since_us = sim_now_us();
    s_fade_end_us = 0;
}

static void end_current(aud_state_t ev)
{
    s_last_pos_ms = (int32_t)(pos_us() / 1000);
    s_last_len_ms = s_cur.len_ms;
    uint32_t gen = s_cur.gen;
    s_state = ev == AUD_FADED ? AUD_IDLE : AUD_ENDED;
    s_fade_end_us = 0;
    emit(ev, gen);
    drop(&s_nxt);
    drop(&s_cur);
}

static bool tone2_rate(uint32_t r) { return r == 48000 || r == 96000 || r == 192000 || r == 384000; }

/* hires T10b fix 1: the Bluetooth sink's resampler table as the board runs it (bt_svc.c hp_sink + rsq_build_task: the same pc_rsq_swap rules and
 * log lines, task=sim). While the sink is on, each tick ends with one block at the plan's rate on a 48k link, then one builder step with the
 * board's hints (audio_svc_info: the plan of the moment, and the queued next track through pc_sink_pipe_rate). The block comes first because on
 * the board a build (about 26 ms) takes longer than a block; the fake builds at once. The two blocks are allocated at the first sink on, kept. */
#define SIM_LINK 48000
static struct { pc_rsq_swap_t sw; pc_rsq_pair_t waited; uint32_t dropped, dropped_total; bool setup; } s_rq;
static void rsq_setup(void)
{
    if (s_rq.setup) return;
    s_rq.setup = true;
    pc_rsq_t *a = malloc(pc_rsq_size()), *b = malloc(pc_rsq_size());
    if (!a || !b) { free(a); free(b); a = b = NULL; }
    pc_rsq_swap_setup(&s_rq.sw, a, b);
}
static void rsq_builder_step(void)                                  /* the builder step (rsq_build_task) */
{
    aud_info_t ai;
    audio_svc_info(&ai);
    pc_rsq_pair_t now, next, p;
    pc_rsq_hints(true, ai.src_rate, ai.pipe_rate, ai.next_rate, SIM_LINK, &now, &next);   /* the board's rule (bt_svc.c rsq_build_task) */
    pc_rsq_t *b = pc_rsq_swap_spare(&s_rq.sw, now, next, &p);
    if (!b) return;
    int64_t t0 = esp_timer_get_time();
    int err = pc_rsq_init(b, p.in, p.out);
    if (err) { printf("APP rsq fail in=%lu out=%lu err=%d\n", (unsigned long)p.in, (unsigned long)p.out, err); return; }
    printf("APP rsq init in=%lu out=%lu us=%lu task=sim\n", (unsigned long)p.in, (unsigned long)p.out, (unsigned long)(esp_timer_get_time() - t0));
    pc_rsq_swap_publish(&s_rq.sw, p);
}
/* hires 10c, as bt_svc.c hp_rsq_sink(on): the sink start builds the plan's table before the first block (the board waits for it, bounded) */
static void rsq_sink_start(void)
{
    rsq_setup();
    aud_info_t ai;
    audio_svc_info(&ai);
    pc_rsq_pair_t now, next;
    pc_rsq_hints(true, ai.src_rate, ai.pipe_rate, ai.next_rate, SIM_LINK, &now, &next);
    if (!now.in || now.in == now.out) return;
    if (pc_rsq_swap_spare_busy(&s_rq.sw) && !pc_rsq_swap_has(&s_rq.sw, now)) { printf("APP rsq sink-start in=%lu out=%lu skipped (full cache)\n", (unsigned long)now.in, (unsigned long)now.out); return; }   /* as bt_svc.c */
    for (int i = 0; i < 3 && !pc_rsq_swap_has(&s_rq.sw, now); i++) rsq_builder_step();
    printf("APP rsq sink-start in=%lu out=%lu ready=%d wait_us=0\n", (unsigned long)now.in, (unsigned long)now.out, pc_rsq_swap_has(&s_rq.sw, now));
}
static void rsq_model(void)
{
    if (!s_sink) return;
    rsq_setup();
    if (s_cur.used && !s_cur.refused && s_state == AUD_PLAYING) {   /* the block (hp_sink) */
        uint32_t rate = s_cur.plan.pipe;
        pc_rsq_t *q;
        pc_rsq_pick_t k = pc_rsq_swap_take(&s_rq.sw, rate, SIM_LINK, &q);
        if (k == PC_RSQ_WAIT) {
            if (s_rq.waited.in != rate || s_rq.waited.out != SIM_LINK) {
                s_rq.waited.in = rate;
                s_rq.waited.out = SIM_LINK;
                printf("APP rsq wait in=%lu out=%lu\n", (unsigned long)rate, (unsigned long)SIM_LINK);
            }
            s_rq.dropped++;
            s_rq.dropped_total++;
        } else {                                                    /* as hp_sink: any block that does not wait ends the stretch */
            if (k == PC_RSQ_SWAP) printf("APP rsq swap in=%lu out=%lu dropped=%lu\n", (unsigned long)rate, (unsigned long)SIM_LINK, (unsigned long)s_rq.dropped);
            s_rq.dropped = 0;
            s_rq.waited.in = s_rq.waited.out = 0;
        }
    }
    rsq_builder_step();
}
uint32_t fake_rsq_dropped(void) { return s_rq.dropped_total; }

static void tick_play(void)
{
    if (s_cur.used && s_state == AUD_PLAYING && s_cur.len_ms < 0 && s_dac_fault && s_cur.rate >= s_dac_fault) s_underruns++;   /* a mixer the fake DAC cannot keep up with */
    if (s_listen_us && sim_now_us() >= s_listen_us) {            /* the listener heard both tones */
        s_listen_us = 0;
        if (s_cur.used && s_cur.gen == s_listen_gen) rate_test_tap();
    }
    if (!s_cur.used || s_state != AUD_PLAYING) return;
    if (s_cur_fails) { s_cur_fails = false; end_current(AUD_ERROR); return; }
    if (s_fade_end_us && sim_now_us() >= s_fade_end_us) { end_current(AUD_FADED); return; }
    if (s_cur.len_ms < 0) return;
    int64_t left_us = (int64_t)s_cur.len_ms * 1000 - pos_us();
    if (s_xfade_ms > 0 && s_nxt.used && !s_fade_end_us && left_us > 0 && left_us <= (int64_t)s_xfade_ms * 1000 && s_xf_gen != s_nxt.gen) {   /* the board: the next track starts fading in under this one */
        s_xf_gen = s_nxt.gen;
        emit(AUD_XFADE, s_nxt.gen);
    }
    if (left_us <= 0) {
        if (s_nxt.used && !s_fade_end_us) {                    /* gapless: the queued source carries on */
            src_t n = s_nxt;
            s_nxt.used = false;
            s_last_pos_ms = s_cur.len_ms;
            drop(&s_cur);
            s_cur = n;
            s_cur.plan = plan_of(s_cur.rate, s_cur.cap = s_cap_req, s_cur.burst);   /* as the board: planned for the output of the moment */
            plan_log(&s_cur);
            begin();
            emit(AUD_NEXT, s_cur.gen);
        } else end_current(s_fade_end_us ? AUD_FADED : AUD_ENDED);
        return;
    }
    if (s_cur.o.near_ms > 0 && !s_cur.near_sent && left_us <= (int64_t)s_cur.o.near_ms * 1000) {
        s_cur.near_sent = true;
        emit(AUD_NEAR, s_cur.gen);
    }
}

/* the toy sound effects (sfx.c, the firmware's file): the board mixes them into every audio block before the gain; here every tick
 * (20 ms of 48 kHz) takes what was posted and renders it into a scratch block, so `sfx` counts the blocks the effects changed (mixed=) */
static float s_sfx_blk[2 * 960];
static void tick(void *arg)
{
    (void)arg;
    tick_play();
    rsq_model();
    memset(s_sfx_blk, 0, sizeof s_sfx_blk);
    sfx_mix(s_sfx_blk, 960, 48000, 1.0f);
}

bool fake_audio_hw(uint32_t hz) { if (!hz) return false; s_hw_cap = hz < PC_AUDIO_MAX_RATE ? hz : PC_AUDIO_MAX_RATE; return true; }

bool fake_audio_dac(const char *args)    /* simdac <hz> | simdac fault <hz> | simdac off */
{
    unsigned hz;
    if (!strcmp(args, "off")) { s_dac_ok = s_dac_fault = 0; s_glitch_armed = false; return true; }
    if (!strcmp(args, "glitch")) {
        if (s_cur.used && s_cur.len_ms < 0 && !s_cur.refused) s_underruns++; else s_glitch_armed = true;
        return true;
    }
    if (sscanf(args, "fault %u", &hz) == 1 && hz) { s_dac_fault = hz; return true; }
    if (sscanf(args, "%u", &hz) == 1 && hz) { s_dac_ok = hz; return true; }
    return false;
}

static int s_speed_pct = 100;   /* Track B: what the owner asked for (the board's audio task applies it; here it is only recorded, and played back by `speed` in the console) */
void audio_svc_set_speed(int pct) { if (pct != s_speed_pct) printf("APP audio speed request=%d%%\n", pct); s_speed_pct = pct; }
void audio_svc_set_xfade(int ms) { if (ms != s_xfade_ms) printf("APP audio xfade request=%dms\n", ms); s_xfade_ms = ms; }
void audio_svc_set_cap(uint32_t hz)
{
    s_cap_req = hz;
    bool playing = s_cur.used && (s_state == AUD_PLAYING || s_state == AUD_PAUSED);
    printf(playing ? "APP audio cap=%lu (next track)\n" : "APP audio cap=%lu\n", (unsigned long)hz);
}

void audio_svc_start(void)
{
    sfx_init();                         /* the toy sound bank (sfx.c, the firmware's file), as audio_svc_start on the board */
    s_cap_req = pc_dacmax_norm(cfg_int("dac_max_rate", 0), BSP_DAC_RATE_DEFAULT);   /* as audio_svc_start on the board */
    if (s_timer) {                      /* started again (tests reset the virtual clock): re-arm the tick on the current clock */
        esp_timer_stop(s_timer);
        esp_timer_start_periodic(s_timer, TICK_US);
        return;
    }
    s_evt = xQueueCreate(8, sizeof(aud_event_t));
    esp_timer_create_args_t a = { .callback = tick, .name = "sim_audio" };
    esp_timer_create(&a, &s_timer);
    esp_timer_start_periodic(s_timer, TICK_US);
}

static bool s_paused_cmd;                 /* audio_svc_paused: the last pause command, as the firmware keeps it */
static int (*s_mix_render)(float *, int, void *);   /* the mixer's render callback of the current source (the external mode's sound card pulls it through fake_audio_pull) */
static void *s_mix_u;
static aud_process_fn s_post_fn;                      /* audio_svc_set_post: the effect chain / the sound card's fixed limiter */
static void *s_post_u;
static uint32_t take(const pc_backend_t *be, const pc_reader_t *r, const aud_opts_t *o, aud_release_fn release, void *release_arg, bool mixer, uint32_t rate)
{
    s_mix_render = NULL;
    drop(&s_nxt);
    drop(&s_cur);
    s_gen++;
    if (mixer) {
        memset(&s_cur, 0, sizeof s_cur);
        s_cur.used = true;
        s_cur.gen = s_gen;
        s_cur.len_ms = -1;
        s_cur.fmt = "Synth";
        s_cur.rate = rate ? rate : 44100;
        s_cur.refused = refuse_mixer(rate);
        s_cur.plan = (pc_pipe_t){ s_cur.rate, 0, PC_PIPE_NATIVE };
        s_cur.cap = s_cap_req;
        s_cur.release = release;
        s_cur.release_arg = release_arg;
    } else {
        pc_reader_t none = { 0 };
        fill(&s_cur, be, r ? r : &none, o, release, release_arg, s_gen);
    }
    if (!s_cur.refused) plan_log(&s_cur);
    s_underruns = 0;
    if (mixer && s_glitch_armed) { s_glitch_armed = false; s_underruns++; }
    s_listen_us = 0;
    if (mixer && s_dac_ok && !s_cur.refused && tone2_rate(s_cur.rate) && s_cur.rate <= s_dac_ok && !(s_dac_fault && s_cur.rate >= s_dac_fault)) { s_listen_us = sim_now_us() + 200000; s_listen_gen = s_gen; }
    s_cur_fails = s_cur.refused || (!mixer && s_fail_next > 0);
    if (s_cur_fails) s_fail_next--;
    begin();
    return s_gen;
}

uint32_t audio_svc_play(const pc_backend_t *be, const pc_reader_t *r, uint32_t start_off, int track, aud_release_fn release, void *release_arg)
{
    return audio_svc_play_ex(be, r, start_off, track, NULL, release, release_arg);
}
uint32_t audio_svc_play_ex(const pc_backend_t *be, const pc_reader_t *r, uint32_t start_off, int track, const aud_opts_t *o, aud_release_fn release, void *release_arg)
{
    (void)track;
    s_paused_cmd = false;
    uint32_t gen = take(be, r, o, release, release_arg, false, 0);
    if (s_cur.len_ms >= 0 && start_off) {              /* start_off is a byte offset; the position follows the stream's average rate */
        uint32_t size = r && r->size ? r->size : 0;
        if (size) { s_base_us = (int64_t)((double)s_cur.len_ms * 1000.0 * ((double)start_off / (double)size)); if (s_base_us > (int64_t)s_cur.len_ms * 1000) s_base_us = (int64_t)s_cur.len_ms * 1000; }
    }
    return gen;
}
uint32_t audio_svc_queue_next(uint32_t after_gen, const pc_backend_t *be, const pc_reader_t *r, const aud_opts_t *o, aud_release_fn release, void *release_arg)
{
    if (!s_cur.used || s_cur.gen != after_gen || s_fade_end_us) {   /* the source it was to follow is gone */
        if (release) release(release_arg);
        return 0;
    }
    drop(&s_nxt);
    fill(&s_nxt, be, r, o, release, release_arg, ++s_gen);
    if (s_nxt.refused) drop(&s_nxt);                      /* as the firmware: a refused pre-open is not queued (it is refused again at its turn) */
    return s_gen;
}
uint32_t audio_svc_play_mixer(int (*render)(float *stereo, int frames, void *u), void *u, uint32_t rate, aud_release_fn release, void *release_arg)
{
    s_paused_cmd = false;
    uint32_t g = take(NULL, NULL, NULL, release, release_arg, true, rate);
    s_mix_render = render;
    s_mix_u = u;
    return g;
}
void audio_svc_set_gain(uint32_t gen, float gain)
{
    if (s_cur.used && s_cur.gen == gen) s_cur.o.gain = gain;
    if (s_nxt.used && s_nxt.gen == gen) s_nxt.o.gain = gain;
}
int audio_svc_next_track(int track) { (void)track; return -1; }
void audio_svc_stop(bool fade_10s)
{
    s_paused_cmd = false;
    drop(&s_nxt);
    if (fade_10s && s_cur.used) { if (!s_fade_end_us) s_fade_end_us = sim_now_us() + (int64_t)FADE_MS * 1000; return; }
    drop(&s_cur);
    s_state = AUD_IDLE;
    s_base_us = 0;
    s_fade_end_us = 0;
    s_last_pos_ms = 0;
    s_last_len_ms = 0;
}
void audio_svc_pause(bool pause)
{
    s_paused_cmd = pause;
    if (!s_cur.used) return;
    if (pause && s_state == AUD_PLAYING) { s_base_us = pos_us(); s_state = AUD_PAUSED; }
    else if (!pause && s_state == AUD_PAUSED) { s_since_us = sim_now_us(); s_state = AUD_PLAYING; }
}
bool audio_svc_paused(void) { return s_paused_cmd; }
void audio_svc_wired_changed(void)   /* BTAUDIO: the board re-plans the source it plays with the new ceiling and re-opens the I2S (a short mute); the fake re-plans the same way */
{
    printf("APP audio wired reopen\n");
    bsp_audio_dac_config(cfg_int("dac_hpf", 1), cfg_int("dac_fs", 0), cfg_int("dac_amute", 1));   /* the board hands the DAC options on here; the shim chip then "reads back" them */
    if (s_cur.used && !s_cur.refused && s_cur.be) { s_cur.cap = s_cap_req; s_cur.plan = plan_of(s_cur.rate, s_cur.cap, s_cur.burst); plan_log(&s_cur); }
}
bool fake_audio_sink(bool on)            /* simsink 1|0; audio_svc_set_sink too: the current source is re-planned (a mixer is never halved) */
{
    s_sink = on;
    pc_rsq_swap_restart(&s_rq.sw);
    if (on) rsq_sink_start();                         /* as bt_svc.c hp_rsq_sink: the table kept, the history starts silent */
    if (s_cur.used && !s_cur.refused && s_cur.be) { s_cur.plan = plan_of(s_cur.rate, s_cur.cap, s_cur.burst); plan_log(&s_cur); }   /* a re-plan keeps the cap the source opened with */
    return true;
}
void audio_svc_set_sink(aud_sink_fn fn) { fake_audio_sink(fn != NULL); }
/* BTAUDIO: the fake records what the board's plan_set does (out_status_wired_set), once per read: the same plan, the same pure rule */
static void os_sync(void)
{
    if (!s_cur.used || s_cur.refused) { out_status_wired_clear(); return; }
    pc_os_wired_in_t d;
    memset(&d, 0, sizeof d);
    d.src_rate = s_cur.rate; d.caps = SIM_DAC_CAPS; d.cap = s_cur.cap ? s_cur.cap : s_cap_req; d.hw_cap = s_hw_cap; d.plan = s_cur.plan;
    d.up_from = s_cur.plan.pipe != s_cur.rate && !s_cur.plan.halvings ? s_cur.rate : 0;
#ifdef SIM_FP
    d.hard = pc_dacmax_norm(cfg_int("dac_hard_rate", 0), 384000);
#endif
    out_status_wired_set(&d);
}
void audio_svc_info(aud_info_t *out)
{
    os_sync();
    memset(out, 0, sizeof *out);
    bool on = s_cur.used && !s_cur.refused;
    out->src_rate = on ? s_cur.rate : 0;
    out->pipe_rate = on ? s_cur.plan.pipe : 44100;
    out->out_rate = s_sink ? 0 : out->pipe_rate;          /* the board releases the I2S channel for a sink */
    out->out_bits = s_sink ? 0 : 32;
    out->bclk = out->out_rate * 64;                         /* the fake DAC: exactly 64 fs */
    out->hw_cap = s_hw_cap;
    out->cap = on ? s_cur.cap : s_cap_req;
    out->pipe_why = on ? s_cur.plan.why : PC_PIPE_NATIVE;
    out->halvings = on ? s_cur.plan.halvings : 0;
    out->sink = s_sink;
    out->underruns_src = on ? s_underruns : 0;
    out->fmt = on ? s_cur.fmt : NULL;
    out->src_bits = on ? s_cur.bits : 0;
    out->kbps = on ? s_cur.kbps : 0;
    out->released = s_sink;                                 /* the board releases the I2S channel for a sink */
    out->next_rate = s_nxt.used ? s_nxt.rate : 0;          /* T10b fix 1: the queued gapless next track (a refused one is never queued) */
}
/* BTVOL: the same ceiling as the board's audio_svc.c (pc_volhold); the "block" of the fake audio task is one audio_svc_block_mark() call, unless
 * "simblock 0" stops it (the audio task has not made a block yet: an output change stays held) until "simblock 1". */
static pc_volhold_t s_hold;
static bool s_blk_stopped;
static uint32_t s_blk_mark;
void audio_svc_set_volume(int vol) { s_vol = pc_volhold_apply(&s_hold, vol); }
void audio_svc_volume_hold(int cap) { pc_volhold_begin(&s_hold, cap); if (s_vol > cap) audio_svc_set_volume(audio_svc_volume_req()); }
void audio_svc_volume_release(void) { const int w = pc_volhold_end(&s_hold); if (w >= 0) audio_svc_set_volume(w); }
bool audio_svc_volume_held(void) { return pc_volhold_on(&s_hold); }
int audio_svc_volume_req(void) { return pc_volhold_req(&s_hold, s_vol); }
uint32_t audio_svc_block_mark(void) { return s_blk_stopped ? s_blk_mark : ++s_blk_mark; }
void fake_audio_blocks(bool run) { s_blk_stopped = !run; }
int fake_audio_volume(void) { return s_vol; }
uint32_t audio_svc_live_gen(void) { return s_cur.used ? s_cur.gen : 0; }   /* the fake opens a source at once: live = published; 0 after a stop */
void audio_svc_status(aud_status_t *out)
{
    memset(out, 0, sizeof *out);
    out->state = s_state;
    out->gen = s_cur.used ? s_cur.gen : 0;                 /* the board publishes 0 after a stop (release_current), so does the fake */
    out->rate = s_cur.used && s_cur.plan.pipe ? s_cur.plan.pipe : s_cur.used ? s_cur.rate : 44100;   /* the board: s_rate, the pipeline */
    out->tracks = 1;
    out->len_ms = -1;
    if (s_cur.used && s_cur.len_ms >= 0) {          /* a mixer has no position */
        out->pos_ms = (int32_t)(pos_us() / 1000);
        out->len_ms = s_cur.len_ms;
        if (s_cur.len_ms > 0 && s_cur.r.size) out->pos_byte = (uint32_t)((double)s_cur.r.size * (double)out->pos_ms / (double)s_cur.len_ms);   /* the average rate (the board: the backend's byte position) */
    } else if (!s_cur.used) {
        out->pos_ms = s_last_pos_ms;
        out->len_ms = s_last_len_ms;
    }
    (void)s_vol;
}
void audio_svc_take_acc(aud_acc_t *out) { memset(out, 0, sizeof *out); }
QueueHandle_t audio_svc_events(void) { return s_evt; }

/* ---- spectrum: three slow sine sweeps plus a little noise (seeded), written into a SPEC_N ring and read back like the board does ---- */
static float s_ring[SPEC_N];
static int64_t s_ring_n = -1;           /* index of the next sample to write; -1: ring holds nothing yet */

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

/* sample n (time n / rate): a function of the track position and the seed only */
/* SIM_GUIDE=1 (the guide's screenshots): a music-like signal, 118 bpm, Am F C G: pad, bass, 16th arpeggio, kick, snare, hats; broadband, a function of the sample index and the seed */
static float synth_music(int64_t n, uint32_t rate)
{
    static const double CH[4][3] = { { 220.0, 261.63, 329.63 }, { 174.61, 220.0, 261.63 }, { 261.63, 329.63, 392.0 }, { 196.0, 246.94, 293.66 } };
    double t = (double)n / (double)rate, beat = 60.0 / 118.0, b = t / beat, v = 0.0;
    int bar = (int)(b / 4.0), ci = bar % 4; double inbeat = b - floor(b), tb = inbeat * beat;
    double s16 = b * 4.0 - floor(b * 4.0), t16 = s16 * beat / 4.0;
    for (int k = 0; k < 3; k++) for (int hh = 1; hh <= 14; hh++) v += (0.04 / sqrt((double)hh)) * sin(2.0 * M_PI * CH[ci][k] * hh * t + k);          /* pad */
    double root = CH[ci][0] / 4.0; double e8 = exp(-fmod(b * 2.0, 1.0) * 2.5);
    v += 0.22 * e8 * (sin(2.0 * M_PI * root * t) + 0.4 * sin(4.0 * M_PI * root * t));                                                 /* bass on the 8ths */
    int step = (int)(b * 4.0) % 4; double an = CH[ci][(step * 2) % 3] * 2.0;
    v += 0.14 * exp(-t16 * 16.0) * (sin(2.0 * M_PI * an * t) + 0.5 * sin(4.0 * M_PI * an * t) + 0.3 * sin(6.0 * M_PI * an * t));     /* arpeggio */
    v += 0.5 * exp(-tb * 14.0) * sin(2.0 * M_PI * (45.0 * tb + (110.0 * (1.0 - exp(-tb * 30.0)) / 30.0)) );                               /* kick on every beat */
    uint32_t r = mix32((uint32_t)n * 2654435761u ^ sim_seed()), r2 = mix32((uint32_t)(n - 1) * 2654435761u ^ sim_seed());
    double nz = (double)(r >> 8) / 8388608.0 - 1.0, nz2 = (double)(r2 >> 8) / 8388608.0 - 1.0;
    int beati = (int)b % 4;
    if (beati == 1 || beati == 3) v += 0.22 * exp(-tb * 16.0) * nz + 0.12 * exp(-tb * 20.0) * sin(2.0 * M_PI * 190.0 * t);           /* snare */
    double te = fmod(b * 2.0, 1.0) * beat / 2.0;
    v += 0.5 * exp(-te * 40.0) * (nz - nz2) + 0.05 * (nz - nz2);                                                                                          /* hats on the 8ths */
    return (float)(v * 0.8);
}

static float synth(int64_t n, uint32_t rate)
{
    static int guide = -1;
    if (guide < 0) guide = getenv("SIM_GUIDE") != NULL;
    if (guide) return synth_music(n, rate);
    static const struct { double f0, f1, period, amp; } T[3] = {
        { 90.0, 420.0, 7.0, 0.30 }, { 600.0, 2400.0, 11.0, 0.22 }, { 3000.0, 9000.0, 17.0, 0.12 },
    };
    double t = (double)n / (double)rate, v = 0.0;
    for (int k = 0; k < 3; k++) {
        double w = 2.0 * M_PI / T[k].period;                  /* f(t) = f0 + (f1 - f0) * (0.5 - 0.5 cos(w t)); phase = integral of f */
        double ph = T[k].f0 * t + (T[k].f1 - T[k].f0) * (0.5 * t - sin(w * t) / (2.0 * w));
        v += T[k].amp * sin(2.0 * M_PI * ph);
    }
    uint32_t r = mix32((uint32_t)n * 2654435761u ^ sim_seed());
    v += 0.03 * ((double)(r >> 8) / 8388608.0 - 1.0);          /* uniform noise, about -35 dBFS peak */
    return (float)v;
}

/* the taps' rate, as the board's: the pipeline rate, averaged down to <= 48k (pc_tap_decim) */
static uint32_t tap_rate(void)
{
    uint32_t r = s_cur.used && s_cur.plan.pipe ? s_cur.plan.pipe : s_cur.used ? s_cur.rate : 44100;
    return r / (uint32_t)pc_tap_decim(r);
}

static int ring_at(int64_t n) { return (int)(((n % SPEC_N) + SPEC_N) % SPEC_N); }

static int s_spec_calls;                                   /* how many times the spectrum was asked for (simspec prints it: the voices kind must not ask) */
int fake_audio_spec_calls(void) { return s_spec_calls; }
void audio_svc_spectrum_db(float floor_db, float bars[64])
{
    s_spec_calls++;
    if (!s_cur.used || s_state != AUD_PLAYING) {              /* paused, stopped, ended: the ring holds silence */
        memset(s_ring, 0, sizeof s_ring);
        s_ring_n = -1;
    } else {
        int64_t now_n = pos_us() * (int64_t)tap_rate() / 1000000;
        if (s_ring_n < 0 || s_ring_n > now_n || now_n - s_ring_n > SPEC_N) s_ring_n = now_n - SPEC_N;
        for (; s_ring_n < now_n; s_ring_n++) s_ring[ring_at(s_ring_n)] = s_ring_n < 0 ? 0.0f : synth(s_ring_n, tap_rate());
    }
    float mono[SPEC_N];
    int w = s_ring_n < 0 ? 0 : ring_at(s_ring_n);            /* the oldest sample of the window sits at s_ring_n % SPEC_N */
    for (int i = 0; i < SPEC_N; i++) mono[i] = s_ring[(w + i) % SPEC_N];
    pc_spectrum_bars_db(mono, SPEC_N, tap_rate(), floor_db, bars);
}
/* newest n (<= SPEC_N) samples of the same synthetic signal; the same ring the spectrum reads. Silence when nothing plays (the board's ring holds zeros then). */
/* Voice levels (the chip / tracker meter data): a fake table per chip family, selected by simvoices. Each voice is a decaying pulse
 * of its own period and phase on the virtual clock, silent for the last 40 % of every period; on top of that voice (t / 700 ms) mod n
 * is held silent, so at least one voice rests at any time. */
static const char *const NES_NAMES[] = { "Square 1", "Square 2", "Triangle", "Noise", "DMC" };   /* what the real adapter gives: S1 S2 T N D */
static const char *const GB_NAMES[] = { "Square 1", "Square 2", "Wave", "Noise" };
static struct { int n; const char *const *names; bool pan; } s_vset;

bool fake_audio_voices_set(const char *set)
{
    if (!strcmp(set, "off")) s_vset.n = 0, s_vset.names = NULL, s_vset.pan = false;
    else if (!strcmp(set, "nes")) s_vset.n = 5, s_vset.names = NES_NAMES, s_vset.pan = false;
    else if (!strcmp(set, "gb")) s_vset.n = 4, s_vset.names = GB_NAMES, s_vset.pan = false;
    else if (!strcmp(set, "spc")) s_vset.n = 8, s_vset.names = NULL, s_vset.pan = true;     /* named 1..8; the SPC and the trackers have a pan */
    else if (!strcmp(set, "one")) s_vset.n = 1, s_vset.names = NULL, s_vset.pan = false;     /* a single voice, named 1 */
    else if (!strcmp(set, "max")) s_vset.n = PC_VOICES_MAX, s_vset.names = NULL, s_vset.pan = true;
    else if (!strcmp(set, "sixteen")) s_vset.n = 16, s_vset.names = NULL, s_vset.pan = true;   /* the old maximum: the voices bars and ring tests keep their 16 */
    else if (!strcmp(set, "mid")) s_vset.n = 12, s_vset.names = NULL, s_vset.pan = true;     /* two columns of the voice rows */
    else return false;
    pc_vmute_reset();                                  /* a new fake source: every voice on, as a backend taking a source over does */
    return true;
}

int audio_svc_voices(pc_voices_t *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < PC_VOICES_MAX; i++) pc_voice_clear_extra(out, i);
    int n = s_vset.n;
    if (n <= 0) return 0;
    int64_t t = sim_now_us() / 1000;
    int rest = n > 1 ? (int)((t / 700) % n) : -1;      /* a single voice never rests */
    for (int i = 0; i < n; i++) {
        pc_voice_short(s_vset.names ? s_vset.names[i] : NULL, i, out->name[i]);
        int per = 400 + 90 * i;
        int ph = (int)((t + 137 * i) % per), live = per * 6 / 10;
        int lv = (i == rest || ph >= live) ? 0 : 255 * (live - ph) / live;
        if (pc_vmute_mask() >> i & 1) lv = 0;           /* a muted voice is silent, as the backends read it */
        out->level[i] = (uint8_t)lv;
        /* the visualiser's extras: a note per period (a walk over two octaves, a different one per voice), one strike per period, the last voice of the NES set is noise */
        const int cycle = (int)((t + 137 * i) / per);
        const bool noise = s_vset.names == NES_NAMES && i >= 3;
        if (!noise) {
            out->cents[i] = (int16_t)(4800 + 100 * ((cycle * 5 + i * 3) % 24));
            out->flags[i] = PC_VF_PITCH;
        } else out->flags[i] = PC_VF_NOISE;
        out->strikes[i] = (uint8_t)cycle;
        if (s_vset.pan) out->pan[i] = (int8_t)(((i * 53) % 255) - 127);
    }
    if (s_paused_cmd) memset(out->level, 0, sizeof out->level);   /* as the firmware: a paused source reads 0 (names and n stay) */
    out->n = (uint8_t)n;
    return n;
}

int audio_svc_wave(float *out, int n)
{
    if (!out || n <= 0) return 0;
    if (n > SPEC_N) n = SPEC_N;
    if (!s_cur.used || s_state != AUD_PLAYING) { memset(out, 0, (size_t)n * sizeof(float)); return n; }
    int64_t now_n = pos_us() * (int64_t)tap_rate() / 1000000;
    for (int i = 0; i < n; i++) { int64_t k = now_n - n + i; out[i] = k < 0 ? 0.0f : synth(k, tap_rate()); }
    return n;
}
/* stereo tap: L = the synthetic signal under a slow envelope (6 s), R = the same signal 17 samples later under another one (4 s, 0.6 at most): the phase scope
 * is a slanted band, not a line. 0 while off, silence when nothing plays. */
static bool s_stereo_on;
static int64_t s_stereo_since = -1;     /* sample index when the tap went on; only samples after it are returned (like the board's ring) */
bool audio_svc_want_stereo(bool on)
{
    if (on && !s_stereo_on) s_stereo_since = s_cur.used && s_state == AUD_PLAYING ? pos_us() * (int64_t)tap_rate() / 1000000 : -1;
    if (on != s_stereo_on) printf("SIM stereo %d\n", on);   /* the tap's switch-ons and -offs, for the tests (a momentary off shows) */
    s_stereo_on = on;
    return on;
}
int audio_svc_stereo(int16_t *lr, int frames)
{
    if (!s_stereo_on || !lr || frames <= 0) return 0;
    if (frames > SPEC_N) frames = SPEC_N;
    if (!s_cur.used || s_state != AUD_PLAYING) { memset(lr, 0, (size_t)frames * 2 * sizeof(int16_t)); return frames; }
    int64_t now_n = pos_us() * (int64_t)tap_rate() / 1000000;
    if (s_stereo_since >= 0) {                           /* playing at switch-on: nothing until the "writer" has advanced */
        int64_t got = now_n - s_stereo_since;
        if (got <= 0) return 0;
        if (got < frames) frames = (int)got;
    }
    for (int i = 0; i < frames; i++) {
        int64_t k = now_n - frames + i;
        double t = (double)k / (double)tap_rate();
        float el = (float)(0.15 + 0.85 * (0.5 + 0.5 * sin(2.0 * M_PI * t / 6.0))), er = (float)(0.6 * (0.1 + 0.9 * (0.5 + 0.5 * sin(2.0 * M_PI * t / 4.0 + 1.0))));
        lr[2 * i] = k < 0 ? 0 : (int16_t)(el * synth(k, tap_rate()) * 32767.0f);
        lr[2 * i + 1] = k < 0 ? 0 : (int16_t)(er * synth(k + 17, tap_rate()) * 32767.0f);
    }
    return frames;
}
void audio_svc_spectrum(float bars[64]) { audio_svc_spectrum_db(-60.0f, bars); }
void audio_svc_set_post(aud_process_fn fn, void *u) { s_post_fn = fn; s_post_u = u; }
int audio_svc_volume_last(void) { return s_vol; }
/* One 10 ms block as the audio task makes it for the external mode's sound card (simulator): the mixer's render, the post hook (src -1 and a silent buffer when no mixer plays: the idle
 * blocks), no device volume (the caller applies fake_audio_volume). fake_audio_mixer_rate: the rate of the playing mixer, 0 = none (or refused). Returns the frames the render gave. */
uint32_t fake_audio_mixer_rate(void) { return s_cur.used && s_cur.len_ms < 0 && !s_cur.refused && s_mix_render ? s_cur.rate : 0; }
int fake_audio_pull(float *buf, int frames, uint32_t rate)
{
    int n = 0;
    if (fake_audio_mixer_rate() == rate && s_mix_render) n = s_mix_render(buf, frames, s_mix_u);
    else memset(buf, 0, (size_t)frames * 2 * sizeof(float));
    if (n < 0) n = 0;
    if (s_post_fn) s_post_fn(buf, frames, rate, n ? 2 /* PC_SRC_NOISE: a mixer */ : -1, s_post_u);
    if (!n) memset(buf, 0, (size_t)frames * 2 * sizeof(float));
    return n;
}
