// SPDX-License-Identifier: LGPL-2.1-or-later
#include "audio_svc.h"
#include "backend_gme.h"
#include "backend_sdec.h"
#include "pc/pc_layerset.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bsp/bsp_audio.h"
#include "bsp/bsp_pins.h"
#include "cfg.h"
#include "cpu_load.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "pc/pc_burst.h"
#include "pc/pc_dacmax.h"
#include "pc/pc_extvoice.h"
#include "pc/pc_gapless.h"
#include "pc/pc_halve.h"
#include "pc/pc_lowrate.h"
#include "pc/pc_latest.h"
#include "pc/pc_mix.h"
#include "pc/pc_sfx.h"
#include "sfx.h"
#include "pc/pc_pipe_rules.h"
#include "pc/pc_rate.h"
#include "pc/pc_spectrum.h"
#include "pc/pc_remote.h"
#include "pc/pc_speed.h"
#include "esp_attr.h"
#include "pc/pc_volume.h"
#include "pc/pc_volsel.h"
#include "pc/pc_cs43131.h"
#include "pc/pc_outstat.h"
#include "out_status.h"

#define MAX_BLOCK PC_AUDIO_MAX_BLOCK   /* 10 ms at the highest rate we play (pc_rate.h) */
#define SPEC_N 1024
#define FADE_MS 10000

typedef enum { CMD_PLAY, CMD_MIXER, CMD_TRACK, CMD_STOP, CMD_PAUSE, CMD_QUEUE, CMD_GAIN, CMD_OPENED, CMD_OPEN_FAILED } cmd_type_t;

/* A library source watches its own reads: how far the decoder has read tells the time left when the backend
 * does not know its length (gapless pre-open). Heap, so the backend's copy of the reader stays valid. */
typedef struct { pc_reader_t in; uint32_t end, bytes; } rdw_t;
static int rdw_read(void *u, uint32_t off, void *buf, uint32_t n)
{
    rdw_t *w = u;
    int got = w->in.read(w->in.u, off, buf, n);
    if (got > 0) w->bytes += (uint32_t)got;
    if (got > 0 && off + (uint32_t)got > w->end) w->end = off + (uint32_t)got;
    return got;
}
/* a pre-opened next source (gapless), handed from the opener task to the audio task */
typedef struct {
    const pc_backend_t *be; void *h; pc_reader_t r; rdw_t *w;
    aud_release_fn release; void *release_arg;
    uint32_t gen, after_gen, rate;   /* rate: the source's raw rate (src_rate); the pipeline is planned on the audio task */
    uint32_t burst;                  /* the backend's burst() (pc_pipe_plan), 0 = none */
    aud_opts_t o;
    /* a play-open (backend_gme): the new current source, not a gapless next one */
    bool play, r_self;
    int track;
    uint32_t start_off;
} next_t;
typedef struct {
    cmd_type_t type;
    const pc_backend_t *be;
    pc_reader_t r;
    bool r_self;               /* memory reader: u must point at our own copy */
    uint32_t start_off, rate;
    int track;
    bool fade;
    uint32_t gen;
    int (*mix)(float *, int, void *);
    void *mix_u;
    aud_release_fn release;
    void *release_arg;
    int64_t t_us;
    aud_opts_t o;
    next_t *next;              /* CMD_QUEUE, CMD_OPENED */
} cmd_t;

static QueueHandle_t s_cmd, s_evt;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static aud_status_t s_status;
static volatile float s_target_gain;
static volatile uint32_t s_gen;
/* The newest request that may still start: a play or mixer request sets it to its gen, a stop clears it (gens start at 1). A source
 * opened off the audio task (CMD_OPENED) is heard only if its gen is still this one; anything older is closed where it arrives. */
static volatile uint32_t s_live_gen;
static uint32_t s_live_epoch;      /* bumped by every store of s_live_gen (live_store), under s_mux: lets a failed stop tell whether anything was stored after its own store */
static volatile bool s_paused_cmd;                /* audio_svc_paused */
static aud_process_fn s_post;
static void *s_post_u;
static float *s_spec_ring;       /* PSRAM, mono */
static volatile uint32_t s_spec_w;
/* stereo tap (the phase scope): interleaved int16 L/R, ST_N frames, PSRAM. Allocated once, on the UI task, by
 * audio_svc_want_stereo(true) and never freed: the audio task can therefore never touch freed memory. s_st_ring is published
 * before s_st_on is set; the audio task only tests s_st_on (one flag per block) and then copies. */
#define ST_N 1024                       /* a power of two: the index is masked, not divided */
static int16_t *s_st_ring;
static volatile bool s_st_on;
static volatile uint32_t s_st_w;
static uint32_t s_st_base;              /* s_st_w when the tap was switched on: the reader gives only frames written since */
static uint32_t s_rate = 44100;      /* the pipeline rate = the DAC's (or the sink's input) rate; the source's own rate is P.src */
static bool s_ramp_in;              /* a new source starts from silence (no click when the rate does not change) */
static volatile aud_sink_fn s_sink;  /* NULL: I2S (3.5 mm). Written by the LVGL task (audio_svc_set_sink), read by the audio task: one pointer-sized word, each reader takes one snapshot */
static volatile bool s_sink_muted;   /* set by audio_svc_set_sink(fn != NULL) after the mute, cleared only by audio_unmute() on the audio task: "the DAC was muted for a sink and nobody has unmuted it yet" */
static bool s_released;              /* audio task only: the I2S channel was deleted by this task (a sink carries the sound); cleared by a successful reinit */
/* Bluetooth headphones with AVRCP absolute volume (bt_remote.c): set, the headset does the volume. Written by the LVGL task (or NULL by the
 * mode task), read by the audio task and by audio_svc_set_volume: one pointer-sized word. s_target_gain stays the device's volume all along;
 * unity applies only to a block that goes to a sink (one snapshot of s_sink per block), never to an I2S block. */
static aud_remote_vol_fn s_remote;   /* only through __atomic_load_n / __atomic_store_n, seq_cst */
static volatile int s_vol_last;      /* the last audio_svc_set_volume value (remote volume or not): what the remote path starts from */
/* BTVOL (volumes per output, vol_out.c): while the output changes, a ceiling on every value audio_svc_set_volume takes (pc_volhold: the newest
 * request is kept and applied by audio_svc_volume_release). Under s_mux; PSRAM, 0 B internal. It only decides WHICH value becomes the device volume
 * and when: the block loop, its sink snapshot and its gain are as they were. */
static EXT_RAM_BSS_ATTR pc_volhold_t s_hold;

/* current source */
static struct {
    const pc_backend_t *be; void *h; pc_reader_t r;
    int (*mix)(float *, int, void *); void *mix_u;
    aud_release_fn release; void *release_arg;
    uint32_t gen;
    bool fading; float fade_gain, fade_step;
    bool paused;               /* spec 5.8: the source is not pulled; one block fades out / in */
    aud_opts_t o;              /* ReplayGain, gapless pre-open notice */
    rdw_t *w;                  /* NULL unless o.near_ms */
    bool near_sent;
    uint64_t frames;           /* played from this source */
} S;
static next_t *s_next;         /* audio task only */
/* hires T5: the pipeline plan. A source runs at P.src; the halving stage (P.halve, pc_pipe_plan) brings it to the pipeline rate s_rate for
 * the output it goes to: the wired DAC up to min(cap, P.hw_cap), a sink 48k. */
#define SBUF_FRAMES (PC_SRC_MAX_RATE / 100)   /* one 10 ms source block at the highest source rate */
/* audio task (set up in audio_svc_start, before the task runs). One struct > 8 B: libmain .bss in PSRAM, nothing internal. */
static struct {
    pc_halve_t *halve;         /* PSRAM: the halving stage */
    pc_rsq_t *up;              /* PSRAM, one table block (pc_rsq_size, 70.5 KB): the LOWRATE resampling stage (a wired source the DAC's rate set does not take, either way) */
    uint32_t up_in, up_out;    /* the stage is on (up_in != 0): the source's rate and the pipeline (DAC) rate it makes; up_in 0 = off (the source plays as it is) */
    uint32_t up_pair_in, up_pair_out;   /* the pair the table was built for ({0, 0}: none) */
    int up_carry;              /* source frames the resampler has not taken yet, at the front of sbuf (a few) */
    float *sbuf;               /* PSRAM, 2 x PC_UP_SBUF_FRAMES: the source block before the halving / rate stage */
    uint32_t src, burst;       /* the current source's raw rate (0: none) and burst */
    pc_pipe_t plan;            /* its plan (plan.pipe = s_rate once switched) */
    bool sink;                 /* the plan is for a sink: the audio task's last s_sink snapshot (the one per block) */
    uint32_t cap;              /* the user's ceiling this source was planned with (cap_req() at its open) */
    volatile uint32_t cap_req; /* the ceiling asked for (dac_max_rate, normalised): any task writes (audio_svc_set_cap), the audio task reads; PSRAM, 0 B internal */
    uint32_t hw_cap;           /* the DAC clock's ceiling this boot: one tier down per refused / wrong clock */
    uint32_t src_logged;       /* the plan the last APP pipe line showed */
    pc_pipe_t logged;
    uint32_t wr_err, under0, su0;   /* failed I2S writes since boot; I2S underruns when the source started; the start-gap count then (night3 C) */
    volatile uint32_t tap_rate;   /* the rate of the spectrum / stereo taps (<= 48k: pc_tap_decim averages); read by the UI task */
    int64_t t_reinit;          /* time of the last I2S reinit try; 0: none since the release (try at once) */
    const void *fmt_h; uint32_t fmt_gen;   /* the source the format fields below were read for (handle and generation) */
    const char *fmt; uint8_t src_bits; uint32_t kbps;   /* audio_svc_info: format name, depth, bitrate of the current source (read once per source, off the block loop) */
} P;
static aud_info_t s_info;      /* audio_svc_info, under s_mux */
/* hires T6: the wired ceiling the user asked for (dac_max_rate, normalised by pc_dacmax_norm). Written by any task (audio_svc_set_cap), read by the
 * audio task: one aligned 32-bit word, each reader takes one snapshot (__atomic relaxed, no lock: nothing else depends on its order). The audio task
 * applies it (P.cap) only when a source opens (plan_set fresh): a playing source keeps its plan, a re-plan (the sink came / went) uses P.cap. */
static uint32_t cap_req(void) { return __atomic_load_n(&P.cap_req, __ATOMIC_RELAXED); }
static bool s_rate_join;       /* the next source is waiting for a rate change (the block before it is padded) */
static int s_rate_join_pad;
static float s_tail[2];        /* last two left samples of the previous block (join log) */
/* Track B: speed playback with the pitch kept (pc_speed.h) and the crossfade between tracks. One struct, PSRAM: 0 B internal.
 * The two requests are written by any task (audio_svc_set_speed / set_xfade, one word each, __atomic relaxed) and read once per block. The speed
 * stage sits on the source side of the pipeline at the pipeline rate (after the LOWRATE rate stage when there is one, so 22.05 kHz podcasts get their speed): only without a halving stage and up to 48 kHz; the crossfade only for a next
 * track that joins gapless (same raw rate and plan) and a source without a halving stage. Neither runs for a mixer or a chip source (no next, no length). */
EXT_RAM_BSS_ATTR static struct {
    volatile uint32_t speed_pct;   /* requested speed in percent (100 = off) */
    volatile uint32_t xfade_ms;    /* requested crossfade length (0 = off) */
    bool sp_on;                    /* the stage has been initialised for sp_pct */
    uint32_t sp_pct;
    int rc;                        /* feed: the source's last read result (1 going, 0 ended, -1 error) */
    int fed;                       /* feed: source frames pulled in this block */
    bool xf_on;                    /* this block is part of a crossfade (a new one logs once) */
    int64_t xf_total;              /* its length in frames: what was left when it started (at most the request) */
    const void *xf_h;              /* the next source the frame counter below belongs to */
    uint64_t xf_adv;               /* frames the next source has already played inside the crossfade */
    float last[2];                 /* the last frame of the previous block (before the gain): a speed change blends from it, no step */
    pc_speed_t sp;
    float xb[2 * MAX_BLOCK];       /* the next source's block */
} X;
static int sp_feed(void *u, float *dst, int n)   /* pc_speed_run asks for frames at the pipeline rate: no join, the track end is told to the block */
{
    pc_play_src_t *cur = u;
    if (P.up_in && P.up_out == s_rate) {              /* FIX3 I3: speed runs AFTER the LOWRATE stage (decode -> rate stage -> speed): the source frames go through it, n frames at the DAC rate come out */
        int got = 0;
        while (got < n) {                              /* chunks of at most 800 output frames: the source buffer holds 4x that (the ratio limit), so a short chunk is always the track end */
            int want = n - got < 800 ? n - got : 800, nc;
            int sf = pc_up_want(P.up_carry, want, P.up_in, s_rate);
            pc_fill_t f = pc_gapless_fill(cur, NULL, P.sbuf + 2 * P.up_carry, sf);
            int g = pc_up_run(P.up, P.sbuf, P.up_carry, f.frames, dst + 2 * got, want, &nc);
            int used = f.frames + P.up_carry - nc;
            P.up_carry = nc;
            X.rc = f.cur_rc;
            X.fed += used > 0 ? used : 0;
            got += g;
            if (g < want) break;
        }
        return got;
    }
    pc_fill_t f = pc_gapless_fill(cur, NULL, dst, n);
    X.rc = f.cur_rc;
    X.fed += f.frames;
    return f.frames;
}

static void free_next(next_t *n)
{
    if (!n) return;
    n->be->close(n->h);
    if (n->release) n->release(n->release_arg);
    free(n->w);
    free(n);
}
static void release_next(void)
{
    free_next(s_next);
    s_next = NULL;
    s_rate_join = false;
}

/* M1: a crossfade that is given up midway (seek back, crossfade set to 0, a sleep fade, speed turned on) leaves the next source already advanced and
 * only heard faded under the old track: it is dropped, so the player starts it from its beginning at the old track's end */
static void xf_abandon(void)
{
    if (X.xf_adv > 0 && s_next && X.xf_h == s_next->h) release_next();
    X.xf_adv = 0; X.xf_h = NULL; X.xf_on = false;
}

static void release_current(void)
{
    if (S.be && S.h) S.be->close(S.h);
    if (S.release) S.release(S.release_arg);
    free(S.w);
    memset(&S, 0, sizeof S);
    out_status_wired_clear();              /* BTAUDIO: nothing plays (the next plan_set records its own) */
    X.sp_on = false;                       /* the speed stage holds the old track's audio: the next one starts it afresh */
}

/* the pre-opened next source becomes the current one */
static void take_next(void)
{
    next_t *n = s_next;
    s_next = NULL;
    s_rate_join = false;
    release_current();
    S.be = n->be; S.h = n->h; S.r = n->r; S.w = n->w;
    S.release = n->release; S.release_arg = n->release_arg;
    S.gen = n->gen; S.o = n->o;
    P.burst = n->burst;
    P.under0 = bsp_audio_underruns();
    P.su0 = bsp_audio_start_underruns();
    bsp_audio_arm_start();                                 /* a new source: underruns until its first data are the start gap */
    free(n);
}

static aud_acc_t s_acc;

void audio_svc_take_acc(aud_acc_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_acc;
    memset(&s_acc, 0, sizeof s_acc);
    portEXIT_CRITICAL(&s_mux);
}

static void set_status(aud_state_t st)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = st;
    s_status.gen = S.gen;
    s_status.rate = s_rate;
    if (S.be && S.h) {
        s_status.pos_ms = S.be->position_ms ? S.be->position_ms(S.h) : 0;
        s_status.pos_byte = S.be->byte_pos ? S.be->byte_pos(S.h) : 0;
        s_status.len_ms = S.be->length_ms ? S.be->length_ms(S.h) : -1;
        s_status.tracks = S.be->track_count ? S.be->track_count(S.h) : 1;
    } else {                                   /* mixer (white noise) or idle: no position */
        s_status.pos_ms = 0;
        s_status.pos_byte = 0;
        s_status.len_ms = -1;
    }
    portEXIT_CRITICAL(&s_mux);
}

static void emit(aud_state_t type)
{
    aud_event_t e = { .type = type, .gen = S.gen };
    xQueueSend(s_evt, &e, 0);
}

/* A pending chip open that a stop killed (s_live_gen == 0) ends without a swap: its owner never sent that stop, so it is told as the fade/stop
 * would have told it (AUD_FADED with the job's gen; owners drop events of other gens). A job replaced by a newer play/mixer says nothing. */
static void post_faded_if_stopped(uint32_t gen)
{
    if (s_live_gen != 0) return;
    aud_event_t e = { .type = AUD_FADED, .gen = gen };
    xQueueSend(s_evt, &e, 0);
}

static void announce_next(uint32_t old_gen, uint64_t old_frames, int inserted, const char *how)
{
    portENTER_CRITICAL(&s_mux);
    s_status.track = 0;
    portEXIT_CRITICAL(&s_mux);
    set_status(AUD_PLAYING);
    emit(AUD_NEXT);
    printf("APP gapless %s gen=%lu->%lu prev_frames=%llu inserted=%d rate=%lu gain=%.3f\n", how, (unsigned long)old_gen, (unsigned long)S.gen,
           (unsigned long long)old_frames, inserted, (unsigned long)s_rate, S.o.gain);
}

/* Every source's rate enters the audio task through src_rate() (backends) or the mixer check in CMD_MIXER; 0 = refused, handled as a failed
 * open. test.sh checks that audio_svc.c asks a backend for its rate in this one place only. */
static uint32_t src_rate(const pc_backend_t *be, void *h)
{
    uint32_t raw, r = pc_src_rate(be, h, &raw);            /* the backend is asked once: the value logged is the value refused */
    if (!r) printf("APP play refused rate=%lu max=%d\n", (unsigned long)raw, PC_SRC_MAX_RATE);
    return r;
}

/* The only way to unmute the DAC on the audio task: never without a live channel, never while a sink carries the sound.
 * s_sink is read again after the unmute: set_sink() stores s_sink before it mutes, so either its mute comes after ours or we see the sink and mute again. */
static void audio_unmute(void)
{
    if (!bsp_audio_is_init() || s_sink) return;
    s_sink_muted = false;
    bsp_audio_mute(false);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);      /* the mute store must be ordered before the s_sink load (set_sink stores s_sink, then mutes: two cores) */
    if (s_sink) { s_sink_muted = true; bsp_audio_mute(true); }
}

/* Reservoir mode (bsp_audio_burst_active: native rate above 96 kHz on the wired output, 250 ms queued ahead of the DAC): a stop / pause / resume must not play the queue out.
 * The mute drops it (the BSP flushes on this task); unmute_after: the DAC is unmuted again after the next block is queued (the way a sink's mute is given back, audio_unmute),
 * a pause stays muted until its resume. Nothing here runs in any other mode. */
static void burst_cut(bool unmute_after)
{
    if (!bsp_audio_burst_active()) return;
    bsp_audio_mute(true);
    if (unmute_after) s_sink_muted = true;
}

static void info_fmt(void)      /* the current source's format fields, once per source (a gapless next one has another handle or generation) */
{
    if (!S.h && !S.mix) { P.fmt = NULL; P.fmt_h = NULL; P.src_bits = 0; P.kbps = 0; return; }
    if (S.h == P.fmt_h && S.gen == P.fmt_gen && P.fmt) return;
    P.fmt_h = S.h; P.fmt_gen = S.gen;
    P.fmt = S.be && pc_backend_resolve(S.be, S.h)->name ? pc_backend_resolve(S.be, S.h)->name : S.mix ? "Synth" : "?";
    P.src_bits = S.be && S.be->bits && S.h ? S.be->bits(S.h) : 0;
    int32_t ms = S.be && S.be->length_ms && S.h ? S.be->length_ms(S.h) : -1;
    P.kbps = ms > 0 && S.r.size ? (uint32_t)((uint64_t)S.r.size * 8 / (uint64_t)ms) : 0;
}

static void info_update(void)   /* audio task; once in audio_svc_start before the task exists */
{
    info_fmt();
    aud_info_t i = { .src_rate = S.h || S.mix ? P.src : 0, .pipe_rate = s_rate,   /* no source loaded: src_rate 0 (P keeps the last plan for the next join); pipe_rate stays what the DAC runs at */
                     .out_rate = bsp_audio_rate(), .bclk = bsp_audio_bclk(), .hw_cap = P.hw_cap,
                     .cap = P.cap, .pipe_why = S.h || S.mix ? P.plan.why : PC_PIPE_NATIVE, .halvings = S.h || S.mix ? P.plan.halvings : 0,   /* idle: no plan to show (T8b; P.plan stays for the next join) */
                     .out_bits = bsp_audio_bits(),
                     .sink = P.sink, .underruns_src = bsp_audio_underruns() - P.under0, .start_underruns_src = bsp_audio_start_underruns() - P.su0, .wr_timeouts = P.wr_err + bsp_audio_burst_wr_err(),
                     .fmt = P.fmt, .src_bits = P.src_bits, .kbps = P.kbps, .released = s_released, .next_rate = s_next ? s_next->rate : 0 };   /* next_rate: T10b fix 1 */
    portENTER_CRITICAL(&s_mux);
    s_info = i;
    portEXIT_CRITICAL(&s_mux);
}

void audio_svc_info(aud_info_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_info;
    portEXIT_CRITICAL(&s_mux);
    if (!out->src_rate) out->cap = cap_req();     /* no source: the ceiling asked for (what the next one opens with), not the last source's */
}

static void plan_log(void)      /* APP pipe once per plan change, never per block */
{
    if (P.src == P.src_logged && P.plan.pipe == P.logged.pipe && P.plan.halvings == P.logged.halvings && P.plan.why == P.logged.why) return;
    P.src_logged = P.src;
    P.logged = P.plan;
    printf("APP pipe src=%lu pipe=%lu halvings=%u why=%s\n", (unsigned long)P.src, (unsigned long)P.plan.pipe, (unsigned)P.plan.halvings,
           pc_pipe_why_name(P.plan.why));
}

/* out_max and its reason for the output the plan is for: pc_plan_args (pc_pipe_rules.h: the sink, cap = the user's ceiling, this boot's
 * hw_cap, the no-clock-row rule), the same function the simulator's fake plans with. plan_of is what a source opening now would get (cap_req(): the
 * next source's plan, so a changed ceiling is a rate change at the join, never a gapless carry-on at the old one). */
/* LOWRATE: the board's wired DAC rates (pc_dac_caps_t, one rule for both boards: pc/pc_lowrate.h). A plan the DAC does not take is resampled by the stage (P.up)
 * to the best rate it does; with no table block, or when nothing fits, the plan stays as it is and plan_set refuses it (with its reason when it is a nonzero rate). */
#ifdef BSP_DAC_CS43131
#define DAC_CAPS (&pc_dac_caps_fakepod)
#else
#define DAC_CAPS (&pc_dac_caps_round)
#endif
static pc_pipe_t fix_plan(uint32_t raw, uint32_t out_max, pc_pipe_t p, uint32_t *from)
{
    if (from) *from = 0;
    return P.up ? pc_dac_fix(DAC_CAPS, raw, out_max, P.sink, p, from) : p;
}

static pc_pipe_t plan_of_from(uint32_t raw, uint32_t burst, uint32_t *from)   /* what plan_set would give, without touching the stage (*from: the stage's source rate, 0 = no stage) */
{
    pc_plan_args_t a = pc_plan_args(raw, burst, P.sink, cap_req(), P.hw_cap);
    pc_dac_clamp_args(DAC_CAPS, &a);
    pc_pipe_t p = pc_pipe_plan(raw, a.out_max, burst, a.limit_why);
    if (p.halvings && !P.halve) p.pipe = 0;               /* no stage memory (allocation failed at start): only native plans */
    return fix_plan(raw, pc_dac_fix_max(DAC_CAPS, P.sink, cap_req(), P.hw_cap), p, from);   /* the pick's ceiling is the user's, not the plan's 48k no-clock-row one */
}

static pc_pipe_t plan_of(uint32_t raw, uint32_t burst) { return plan_of_from(raw, burst, NULL); }
/* plan_set for this source would build the stage's table (another pair than the one in P.up): ~26 ms, so never with the output unmuted (M1) */
static bool plan_builds_table(uint32_t raw, uint32_t burst)
{
    uint32_t from = 0;
    pc_pipe_t q = plan_of_from(raw, burst, &from);
    return from && q.pipe && (P.up_pair_in != from || P.up_pair_out != q.pipe);
}

/* The stage's table for in -> out, built here (plan_set: a source opening, or a re-plan, never inside a block; the output is muted at an open and the DAC's
 * DMA holds ~60 ms; the table takes ~26 ms on the board). The same pair as the last source keeps its table; a fresh source (or the stage coming back on
 * after a Bluetooth sink) starts from a silent history and no carry. false: no block or the build failed (the caller refuses the source as before). */
static bool up_prepare(uint32_t in, uint32_t out, bool fresh)
{
    if (!P.up) return false;
    if (P.up_pair_in != in || P.up_pair_out != out) {
        int64_t t0 = esp_timer_get_time();
        P.up_pair_in = P.up_pair_out = 0;
        if (pc_rsq_init(P.up, in, out) != 0) { printf("APP lowrate fail in=%lu out=%lu\n", (unsigned long)in, (unsigned long)out); return false; }
        P.up_pair_in = in;
        P.up_pair_out = out;
        printf("APP lowrate init in=%lu out=%lu us=%lu\n", (unsigned long)in, (unsigned long)out, (unsigned long)(esp_timer_get_time() - t0));
        fresh = true;
    }
    if (fresh || !P.up_in) { pc_rsq_reset(P.up); P.up_carry = 0; }
    return true;
}

/* Plans the current source (fresh: a new source or another raw rate, the stage history is cleared; else stages that stay on keep theirs).
 * Returns the pipeline rate, 0 = refused (logged as the board's refusal line; the caller ends the source as a failed open). */
static EXT_RAM_BSS_ATTR volatile uint32_t s_low_rate;   /* PSRAM: the last refusal for a rate below the wired DAC's minimum (0 = none) */
static EXT_RAM_BSS_ATTR bool s_low_hard;
uint32_t audio_svc_take_low_rate(void) { uint32_t r = s_low_rate; s_low_rate = 0; return r; }
bool audio_svc_low_is_hard(void) { return s_low_hard; }
static EXT_RAM_BSS_ATTR volatile uint8_t s_dac_note;   /* audio_svc_take_dac_note */
static EXT_RAM_BSS_ATTR volatile uint32_t s_dac_note_arg;
static EXT_RAM_BSS_ATTR volatile bool s_dac_silenced;   /* the wired DAC did not answer: the digital gain is 0 until a volume write is read back again (I1) */
uint8_t audio_svc_take_dac_note(uint32_t *arg)
{
    uint8_t n = bsp_audio_dac_note();                 /* the BSP's own (PLL fallback, DAC fault) */
    if (n) { s_dac_note_arg = 0; s_dac_note = n; }
    n = s_dac_note;
    if (arg) *arg = s_dac_note_arg;
    s_dac_note = 0;
    return n;
}
/* DACFS: the FakePod's hard limit (dac_hard_rate, default 384 kHz); the round board and a sink have none */
static uint32_t hard_limit(void)
{
#ifdef BSP_DAC_CS43131
    return pc_dacmax_norm(cfg_int("dac_hard_rate", 0), 384000);
#else
    return 0;
#endif
}
/* BTAUDIO: what was decided for the output chain (the wired stage's reason: pc_os_wired_plan prints it); the 24-bit slot and the word come from the settings when it is read */
static void os_record(uint32_t raw, pc_pipe_t p, uint32_t up_from, bool hard)
{
    pc_os_wired_in_t d;
    memset(&d, 0, sizeof d);
    d.src_rate = raw; d.caps = DAC_CAPS; d.cap = P.cap; d.hard = hard_limit(); d.hw_cap = P.hw_cap; d.plan = p; d.up_from = up_from; d.refused_hard = hard;
    out_status_wired_set(&d);
}
static uint32_t plan_set(uint32_t raw, uint32_t burst, bool fresh)
{
    if (fresh && pc_dac_over_hard(DAC_CAPS, P.sink, raw, hard_limit())) {   /* refused before anything is opened or read (FIX4 I5: a new source only: a changed hard limit never ends the one that plays) */
        printf("APP dac rate: source %lu Hz is above the hard limit %lu Hz (dac_hard_rate): source refused\n", (unsigned long)raw, (unsigned long)hard_limit());
        s_low_rate = raw; s_low_hard = true;
        os_record(raw, (pc_pipe_t){ 0, 0, PC_PIPE_NATIVE }, 0, true);
        P.up_in = P.up_out = 0;
        P.src = 0;
        return 0;
    }
    if (fresh) {                                          /* a new source: this is where a changed dac_max_rate takes effect (one snapshot for both uses below) */
        uint32_t nc = cap_req();
        P.hw_cap = pc_hw_cap_after_cap_change(P.cap, nc, P.hw_cap);   /* raising the setting tries the DAC at the new rate once more */
        P.cap = nc;
    }
    pc_plan_args_t a = pc_plan_args(raw, burst, P.sink, P.cap, P.hw_cap);
    pc_dac_clamp_args(DAC_CAPS, &a);
    pc_pipe_t p = pc_pipe_plan(raw, a.out_max, burst, a.limit_why);
    if (p.halvings && !P.halve) p.pipe = 0;
    if (P.halve) {
        if (fresh) pc_halve_init(P.halve);
        p = pc_halve_set(P.halve, raw, a.out_max, burst, a.limit_why);
    }
    if (fresh) { P.under0 = bsp_audio_underruns(); P.su0 = bsp_audio_start_underruns(); bsp_audio_arm_start(); }
    uint32_t up_src = 0;                                  /* LOWRATE: the source's rate when the resampling stage makes the pipeline rate */
    {
        uint32_t from;
        pc_pipe_t q = fix_plan(raw, pc_dac_fix_max(DAC_CAPS, P.sink, P.cap, P.hw_cap), p, &from);
        if (from) {
            if (up_prepare(from, q.pipe, fresh)) { p = q; up_src = from; }
            else printf("APP lowrate: no stage for %lu Hz\n", (unsigned long)from);
        }
    }
    if (pc_dac_unsupported(DAC_CAPS, p.pipe, P.sink)) {   /* a rate the DAC cannot take and nothing fits (or no table block): refused as a failed open (the fail streak and the player's error path run) instead of a channel that retries every second in silence */
        printf("APP dac rate: source %lu Hz (pipeline %lu Hz) is not a rate the DAC takes (no resampling for it): source refused\n", (unsigned long)raw, (unsigned long)p.pipe);
        s_low_rate = raw; s_low_hard = false;             /* the source's rate (M4): the user knows the file, not the halved pipeline rate */
        p.pipe = 0;
    }
    if (!p.pipe) {
        os_record(raw, p, 0, false);
        if (P.halve) pc_halve_init(P.halve);
        P.up_in = P.up_out = 0;
        P.src = 0;
        printf("APP play refused rate=%lu max=%d\n", (unsigned long)raw, PC_SRC_MAX_RATE);
        return 0;
    }
    P.up_in = up_src;
    P.up_out = up_src ? p.pipe : 0;
    P.src = raw;
    P.burst = burst;
    P.plan = p;
    os_record(raw, p, up_src, false);
    plan_log();
    info_update();
    return p.pipe;
}

static void plan_mixer(uint32_t rate)   /* a mixer renders at the pipeline rate itself: never halved (CMD_MIXER takes only pc_mixer_rate_ok) */
{
    if (P.halve) pc_halve_init(P.halve);
    P.up_in = P.up_out = 0;
    P.under0 = bsp_audio_underruns();
    P.su0 = bsp_audio_start_underruns();
    bsp_audio_arm_start();
    P.src = rate;
    P.burst = 0;
    P.plan = (pc_pipe_t){ rate, 0, PC_PIPE_NATIVE };
    os_record(rate, P.plan, 0, false);
    plan_log();
    info_update();
}

/* The output changed (the sink came or went, the clock ceiling dropped): the current source again, stages that stay on keep their history
 * (a block boundary: no half- or double-speed block). Returns the new pipeline rate (a mixer: its own). 0 cannot happen for
 * a source that planned once (refused rates are odd ones above 48k, refused at their first plan); if it did, the source ends as an error. */
static uint32_t replan(void)
{
    if (S.mix) return P.src;                              /* its own rate, never halved */
    if (!S.h || !P.src) return s_rate > P.hw_cap ? P.hw_cap : s_rate;   /* idle: the next source plans; the channel comes back at a rate the clock takes */
    uint32_t p = plan_set(P.src, P.burst, false);
    if (!p) {
        set_status(AUD_ERROR);
        emit(AUD_ERROR);
        release_next();
        release_current();
    }
    return p;
}

/* The failure codes pc_hires_failure_lowers_cap takes are ESP-IDF's own (pc_pipe_rules.h has no IDF header). */
_Static_assert(PC_ERR_NO_MEM == ESP_ERR_NO_MEM && PC_ERR_INVALID_ARG == ESP_ERR_INVALID_ARG && PC_ERR_NOT_SUPPORTED == ESP_ERR_NOT_SUPPORTED &&
               PC_ERR_INVALID_RESPONSE == ESP_ERR_INVALID_RESPONSE, "pc_pipe_rules.h error codes must be esp_err.h's");

/* What a rate switch did: SW_GONE = the re-plan after a clock failure refused the source (released as an error: the caller must not report it
 * as playing); SW_OK = the channel runs at the rate (or there is no channel and s_rate is set for the reinit); SW_RETRY = the switch failed
 * without lowering the ceiling, the channel was released and comes back at this rate through the reinit (no APP switch line). */
typedef enum { SW_GONE = 0, SW_OK, SW_RETRY } sw_t;
static sw_t switch_rate(uint32_t rate);
static bool s_rate_stale;   /* a switch failed and neither a later switch nor release_retry has run yet (the channel may be at another clock): the next switch_rate() calls the BSP even for s_rate (s_rate itself stays valid: block size, sinks, spectrum divide by it) */

/* A failed switch that does not lower the ceiling, at any rate (spec 4.5; T5 re-review m1): the channel is released and comes back at this rate
 * through the reinit (once per second, Task 3's rule; the blocks in between are dropped by pc_block_route), so no block is ever written at
 * the clock the channel had before. The ceiling stays. */
static void release_retry(uint32_t rate)
{
    printf("APP audio rate fail rate=%lu retry\n", (unsigned long)rate);
    bsp_audio_deinit();                                   /* mutes; nothing when there is no channel */
    if (!s_released) P.t_reinit = 0;                      /* a live channel was just released: try at once; already released (a failed reinit): keep the 1 s pace */
    s_released = true;
    s_rate = rate;
    s_rate_stale = false;
    info_update();
}

/* Spec 4.5, never play at the wrong rate: a clock above 48k that the driver refuses or that reads back wrong (pc_hires_failure_lowers_cap) lowers
 * this boot's ceiling to the tier below the refused rate (pc_hires_tier_down), the source is re-planned (halved) and the switch is made again,
 * at a strictly lower rate each time (48k at the end). */
static sw_t hires_off(const char *why, uint32_t rate)
{
    uint32_t t = pc_hires_tier_down(rate);
    if (t < P.hw_cap) P.hw_cap = t;
    printf("APP hires off: %s rate=%lu cap=%lu\n", why, (unsigned long)rate, (unsigned long)P.hw_cap);
    uint32_t p = replan();
    info_update();
    if (!p) return SW_GONE;
    if (p < rate) return switch_rate(p);
    release_retry(rate);                                  /* a mixer cannot be halved: as any failed switch that keeps the ceiling */
    return SW_RETRY;
}

static bool joins(const next_t *n)   /* the next source carries on in the same block: same raw rate and the same plan (pc_join_kind) */
{
    return pc_join_kind(P.src, P.plan, n->rate, plan_of(n->rate, n->burst)) == PC_JOIN_GAPLESS;
}

static pc_pipe_t clock_plan(void)   /* the current plan at the rate the channel runs (s_rate): what a join is compared with */
{
    pc_pipe_t p = P.plan;
    p.pipe = s_rate;
    return p;
}

static EXT_RAM_BSS_ATTR int64_t s_dac_chk_us;          /* DACFS: when the DAC's serial-port error flags are polled next (0: off) */
static void dac_recover_check(void)   /* the DAC answered again (a start worked): the digital gain goes back to unity through a normal volume write (I1) */
{
#ifdef BSP_DAC_CS43131
    if (s_dac_silenced) audio_svc_set_volume(audio_svc_volume_req());   /* the value asked for (the same applied value: a hold in force caps it again) */
#endif
}

static sw_t switch_rate(uint32_t rate)
{
    if ((rate == s_rate && !s_rate_stale) || !rate) return SW_OK;
    if (s_released || !bsp_audio_is_init()) { s_rate = rate; s_rate_stale = false; info_update(); return SW_OK; }   /* no channel: the reinit at the first block without a sink uses s_rate (a sink takes any rate) */
    esp_err_t e = bsp_audio_set_rate(rate);   /* mute -> reconfigure (or reshape: another DMA shape) -> read back -> settle -> unmute */
    if (e != ESP_OK) {                        /* the channel may run at another clock now: never written to before a switch or a reinit works */
        s_rate_stale = true;
        if (!bsp_audio_is_init() && !s_released) { s_released = true; P.t_reinit = 0; }   /* a failed reshape deleted the channel: as released, the reinit brings it back at the rate this switch settles on */
        if (rate > 48000 && pc_hires_failure_lowers_cap(e, e == ESP_ERR_INVALID_RESPONSE)) return hires_off(e == ESP_ERR_INVALID_RESPONSE ? "readback" : "i2s", rate);
        release_retry(rate);
        return SW_RETRY;
    }
    s_rate = rate;
    s_rate_stale = false;
    s_dac_chk_us = esp_timer_get_time() + 100000;      /* I7: the serial port's error flags are looked at 100 ms after a start */
    dac_recover_check();
    info_update();
    return SW_OK;
}

/* BTAUDIO: a wired setting (dac_max_rate, dac_hard_rate, dac_slot24_from) changed: the audio task takes the new ceiling now, re-plans the source it plays and re-opens the I2S at the
 * rate and slot width the new plan gives (set_rate mutes, reconfigures, settles, unmutes: a brief mute, never a reboot). A source the new hard limit refuses ends as an error, as
 * any refused source. Idle: the next source plans with the new values. */
static EXT_RAM_BSS_ATTR volatile bool s_wired_dirty;
static void wired_reopen(void)
{
    const uint32_t nc = cap_req();
    P.hw_cap = pc_hw_cap_after_cap_change(P.cap, nc, P.hw_cap);
    P.cap = nc;
    bsp_audio_set_slot_mode(cfg_int("dac_slot24_from", 0));
    bsp_audio_dac_config(cfg_int("dac_hpf", 1), cfg_int("dac_fs", 0), cfg_int("dac_amute", 1));   /* a changed option makes the next DAC start reset and boot the chip again */
    const esp_err_t ae = bsp_audio_apply_options();   /* FIX4 I1/I2: applied now, playing or idle (muted around it, volume read back before the unmute) */
    if (ae != ESP_OK) { s_rate_stale = true; printf("APP audio wired options: %s\n", esp_err_to_name(ae)); }
    if (!S.h && !S.mix) return;                           /* nothing plays: the next source plans with the new values */
    s_rate_stale = true;                                   /* the width may differ at the same rate: the BSP decides, the mute is brief */
    const uint32_t p = replan();
    if (p) switch_rate(p);
    s_ramp_in = true;                                      /* FIX4 M9: the gain climbs from 0 across the first block after the re-open (as every other rate change; never above the user gain) */
    info_update();
    printf("APP audio wired reopen cap=%lu\n", (unsigned long)P.cap);
}
void audio_svc_wired_changed(void) { s_wired_dirty = true; }   /* plain byte in PSRAM: the task clears it BEFORE it reads the settings, so a later change is never lost */

static void handle(cmd_t *c)
{
    switch (c->type) {
    case CMD_QUEUE: {
        next_t *n = c->next;
        if (!S.h || S.gen != n->after_gen || S.fading) {       /* the source it was to follow is gone: drop it */
            printf("APP gapless drop gen=%lu (now gen=%lu)\n", (unsigned long)n->gen, (unsigned long)S.gen);
            free_next(n);
            break;
        }
        release_next();
        s_next = n;
        info_update();                                     /* hires T10b fix 1: next_rate now, the Bluetooth resampler's table is built before the join */
        printf("APP gapless ready gen=%lu rate=%lu (%s)\n", (unsigned long)n->gen, (unsigned long)n->rate,
               pc_join_name(pc_join_kind(P.src, P.plan, n->rate, plan_of(n->rate, n->burst))));   /* the plan joins() uses */
        break;
    }
    case CMD_OPENED: {                                     /* chip source opened on aud_pre: swap only, no open here */
        next_t *n = c->next;
        if (n->gen != s_live_gen) {                        /* a later play / mixer / stop happened while it opened: never heard */
            printf("APP preopen drop gen=%lu (live=%lu)\n", (unsigned long)n->gen, (unsigned long)s_live_gen);
            post_faded_if_stopped(n->gen);
            free_next(n);
            break;
        }
        int64_t t0 = esp_timer_get_time();
        s_ramp_in = true;
        bsp_audio_mute(true);
        release_next();
        release_current();
        S.gen = n->gen;
        S.o = n->o;
        S.release = n->release;
        S.release_arg = n->release_arg;
        S.r = n->r;           /* a copy: the backend must not keep the pointer it was opened with (true for backend_gme: open reads r->mem.p once) */
        if (n->r_self) S.r.u = &S.r;
        S.w = n->w;
        S.be = n->be;
        S.h = n->h;
        uint32_t raw = n->rate, burst = n->burst;
        portENTER_CRITICAL(&s_mux);
        s_status.track = n->track;
        portEXIT_CRITICAL(&s_mux);
        free(n);
        S.paused = s_paused_cmd;                           /* a pause requested after the play request (the flag was cleared at the request) is honoured: flag and state agree */
        uint32_t rate = plan_set(raw, burst, true);        /* the pipeline rate */
        if (!rate) {                                       /* refused: as a failed open */
            set_status(AUD_ERROR);
            emit(AUD_ERROR);
            release_current();
            break;
        }
        sw_t sw = switch_rate(rate);
        if (!sw) break;                                    /* released by a clock failure's re-plan: AUD_ERROR stands */
        set_status(S.paused ? AUD_PAUSED : AUD_PLAYING);
        audio_unmute();                                    /* nothing while the channel is released (SW_RETRY): the reinit unmutes */
        if (sw == SW_OK) printf("APP switch ms=%lld rate=%lu\n", (esp_timer_get_time() - t0) / 1000, (unsigned long)s_rate);
        break;
    }
    case CMD_OPEN_FAILED:                                  /* as a failed open on this task: the old source goes, the owner hears AUD_ERROR */
        if (c->gen != s_live_gen) { post_faded_if_stopped(c->gen); break; }   /* nobody wants it any more */
        s_ramp_in = true;
        bsp_audio_mute(true);
        release_next();
        release_current();
        S.gen = c->gen;
        printf("APP audio open failed\n");
        set_status(AUD_ERROR);
        emit(AUD_ERROR);
        release_current();
        break;
    case CMD_GAIN:                                         /* ReplayGain setting changed: the track playing follows */
        if (S.h && S.gen == c->gen) S.o.gain = c->o.gain;
        if (s_next && s_next->gen == c->gen) s_next->o.gain = c->o.gain;
        break;
    case CMD_STOP:
        release_next();
        if (c->fade && (S.h || S.mix)) {
            S.fading = true;
            S.fade_gain = 1.0f;
            S.fade_step = 1.0f / ((float)FADE_MS / 10.0f);   /* per 10 ms block */
        } else {
            release_current();
            set_status(AUD_IDLE);
            burst_cut(true);                               /* reservoir mode: the queued rest of the track does not play on */
        }
        break;
    case CMD_PAUSE:
        if (S.h || S.mix) {
            S.paused = c->fade; set_status(S.paused ? AUD_PAUSED : AUD_PLAYING);   /* fade field = pause */
            burst_cut(!S.paused);                          /* reservoir mode: pause / resume take effect now, not after the queue (what was queued is dropped); a pause stays muted */
        }
        break;
    case CMD_TRACK:
        if (S.be && S.h && S.be->start_track && S.gen == c->gen) {
            int rc = S.be->start_track(S.h, c->track);
            portENTER_CRITICAL(&s_mux);
            s_status.track = c->track;
            portEXIT_CRITICAL(&s_mux);
            set_status(rc == 0 ? AUD_PLAYING : AUD_ERROR);
            if (rc) emit(AUD_ERROR);
        }
        break;
    case CMD_PLAY:
    case CMD_MIXER: {
        s_ramp_in = true;
        bsp_audio_mute(true);
        release_next();
        release_current();
        S.gen = c->gen;
        S.o = c->o;
        S.release = c->release;
        S.release_arg = c->release_arg;
        uint32_t rate;
        if (c->type == CMD_MIXER) {
            S.mix = c->mix;
            S.mix_u = c->mix_u;
            rate = c->rate;
            if (!pc_mixer_rate_ok(rate)) {                 /* never halved: its own rate needs a clock row */
                printf("APP play refused rate=%lu max=%d\n", (unsigned long)rate, PC_AUDIO_MAX_RATE);
                set_status(AUD_ERROR);
                emit(AUD_ERROR);
                release_current();
                break;
            }
            plan_mixer(rate);
        } else {
            S.r = c->r;
            if (c->r_self) S.r.u = &S.r;
            if (S.o.near_ms && (S.w = malloc(sizeof *S.w))) {   /* watch the reads (time left for an unknown length) */
                S.w->in = S.r;
                if (c->r_self) S.w->in.u = &S.w->in;
                S.w->end = S.w->bytes = 0;
                S.r = (pc_reader_t){ .read = rdw_read, .size = S.w->in.size, .u = S.w };
            }
            S.be = c->be;
            if (S.be->open(&S.h, &S.r, c->start_off) != 0) {
                S.h = NULL;
                printf("APP audio open failed\n");
                set_status(AUD_ERROR);
                emit(AUD_ERROR);
                release_current();
                break;
            }
            if (c->track > 0 && S.be->start_track) S.be->start_track(S.h, c->track);
            rate = src_rate(S.be, S.h);
            if (rate) rate = plan_set(rate, S.be->burst ? S.be->burst(S.h) : 0, true);   /* raw -> the pipeline rate */
            if (!rate) {                                   /* refused: as a failed open */
                set_status(AUD_ERROR);
                emit(AUD_ERROR);
                release_current();
                break;
            }
        }
        portENTER_CRITICAL(&s_mux);
        s_status.track = c->track;
        portEXIT_CRITICAL(&s_mux);
        sw_t sw = switch_rate(rate);
        if (!sw) break;                                    /* released by a clock failure's re-plan: AUD_ERROR stands */
        set_status(AUD_PLAYING);
        audio_unmute();                                    /* nothing while the channel is released (SW_RETRY): the reinit unmutes */
        if (sw == SW_OK) printf("APP switch ms=%lld rate=%lu\n", (esp_timer_get_time() - c->t_us) / 1000, (unsigned long)s_rate);
        break;
    }
    }
}

static void task(void *arg)
{
    (void)arg;
    float *buf = heap_caps_malloc(2 * MAX_BLOCK * sizeof(float), MALLOC_CAP_SPIRAM);
    int32_t *pcm = heap_caps_malloc(2 * MAX_BLOCK * sizeof(int32_t), MALLOC_CAP_SPIRAM);   /* 16-bit channel: the first half holds int16 */
    float gain = 0.0f;
    uint32_t blocks = 0, blk_max = 0, under0 = bsp_audio_underruns(), su0 = bsp_audio_start_underruns();
    bool sink_used = false, rate_clamp_logged = false;
    bool unity_prev = false;                            /* the last block went to a sink at unity (remote volume) */
    int64_t t_stat = esp_timer_get_time();
    cpu_snap_t snap0;
    cpu_snap(&snap0);
    for (;;) {
        cmd_t c;
        while (xQueueReceive(s_cmd, &c, 0) == pdTRUE) handle(&c);
        if (s_rate_join && s_next && S.h && !S.fading) {  /* the last block of the old rate went out padded */
            uint32_t old = S.gen;
            uint64_t of = S.frames;
            int pad = s_rate_join_pad;
            uint32_t r = s_next->rate, bu = s_next->burst;
            uint32_t was_raw = P.src;
            pc_pipe_t was = clock_plan();                  /* the old source's plan at the clock the channel runs */
            const bool build = plan_builds_table(r, bu);   /* M1: the table is built below on this task, so even a same-rate (seamless) join is muted around it */
            if (build) bsp_audio_mute(true);
            take_next();
            uint32_t pipe = plan_set(r, bu, true);         /* another raw rate or plan: fresh stages */
            if (!pipe) {                                   /* checked before the join: only if the output changed in between */
                set_status(AUD_ERROR);
                emit(AUD_ERROR);
                release_current();
            } else if (pc_join_kind(was_raw, was, r, P.plan) == PC_JOIN_RATE_CHANGE || s_rate_stale || build) {   /* the clock changes: mute, switch, ramp in */
                bsp_audio_mute(true);                          /* as handle(): the BSP's own restore must never unmute by itself */
                if (switch_rate(pipe)) {                   /* reconfigure -> settle */
                    audio_unmute();
                    s_ramp_in = true;
                    announce_next(old, of, pad, "rate-change");
                }
            } else announce_next(old, of, pad, "rate-change");   /* seamless, the same pipeline rate (a sink; 192k -> 96k wired): no mute, no ramp */
        }
        int frames = pc_block_frames(s_rate);              /* never more than the buffers hold, whatever s_rate says */
        const uint32_t brate = s_rate;                      /* the rate this block is made at (a re-plan below takes effect from the next one) */
        if (s_rate / 100 > (uint32_t)MAX_BLOCK && !rate_clamp_logged) {
            rate_clamp_logged = true;
            printf("APP audio rate clamp rate=%lu frames=%d\n", (unsigned long)s_rate, frames);
        }
        int64_t t0 = esp_timer_get_time();
        int n = 0;
        bool held = S.paused && gain <= 0.0f;            /* paused and already faded out: do not pull */
        const pc_backend_t *rbe = pc_backend_resolve(S.be, S.h);   /* the chain backend (OPL / tracker suffixes) is the backend that opened */
        int kind = S.mix ? PC_SRC_NOISE : (rbe == &pc_backend_mp3 || rbe == &pc_backend_flac || rbe == &backend_ogg || rbe == &backend_m4a || rbe == &pc_backend_wav || rbe == &pc_backend_aiff || rbe == &pc_backend_mpc || rbe == &pc_backend_tta || rbe == &pc_backend_hvl || rbe == &pc_backend_sid || rbe == &pc_backend_opl || rbe == &pc_backend_p8 || rbe == &pc_backend_ym || rbe == &pc_backend_adx || rbe == &pc_backend_vag || rbe == &pc_backend_brstm || rbe == &pc_backend_xmp) ? PC_SRC_MP3 : PC_SRC_CHIP;
        bool src_end = false, src_err = false;
        if (held) { memset(buf, 0, (size_t)frames * 2 * sizeof(float)); n = 0; }
        else if (S.mix) { n = S.mix(buf, frames, S.mix_u); src_end = n <= 0; src_err = n < 0; if (n < 0) n = 0; }
        else if (S.h) {
            uint32_t spd = __atomic_load_n(&X.speed_pct, __ATOMIC_RELAXED);
            if (!spd) spd = 100;                           /* never set: normal speed */
            const bool up = P.up_in && P.up_out == s_rate;   /* LOWRATE: this block's source is up-sampled to the DAC rate (plan_set always makes up_out the rate switch_rate settles on, so a pair that is not this block's rate cannot happen) */
            const bool spx = S.o.speed && spd != 100 && !S.paused && s_rate <= PC_SPEED_MAX_RATE && (!P.halve || pc_halve_in_frames(P.halve, frames) == frames);
            bool join = s_next && !S.fading && joins(s_next) && !spx;   /* speed: no join inside a block, the end goes the padded way below */
            pc_play_src_t cur = { S.be, S.h, S.o.gain > 0 ? S.o.gain : 1.0f };
            pc_play_src_t nx = { join ? s_next->be : NULL, join ? s_next->h : NULL, join && s_next->o.gain > 0 ? s_next->o.gain : 1.0f };
            int sf = up ? pc_up_want(P.up_carry, frames, P.up_in, s_rate) : P.halve ? pc_halve_in_frames(P.halve, frames) : frames;   /* source frames for one pipeline block (the stage: what tops its buffer up, never past the buffer: I1) */
            if (!up && sf > SBUF_FRAMES) sf = SBUF_FRAMES;
            float *sb = up ? P.sbuf + 2 * P.up_carry : sf > frames ? P.sbuf : buf;        /* halved: the source block goes through the stage into buf */
            pc_fill_t f;
            int adv = 0;                                   /* source frames this block used (S.frames) */
            bool sp_edge = false, xf_end = false;
            if (spx) {                                     /* speed: the stage pulls what it needs (cur only) */
                bool sp_ended = false;
                xf_abandon();
                if (!X.sp_on || X.sp_pct != spd) {
                    pc_speed_init(&X.sp, s_rate, (float)spd / 100.0f);
                    X.sp_on = true; X.sp_pct = spd; sp_edge = true;
                    printf("APP audio speed=%lu%%\n", (unsigned long)spd);
                }
                X.rc = 1; X.fed = 0;
                int no = pc_speed_run(&X.sp, buf, frames, sp_feed, &cur, &sp_ended);
                adv = X.fed;
                f = (pc_fill_t){ no, sp_ended ? (X.rc < 0 ? -1 : 0) : 1, -1 };
            } else {
                if (X.sp_on) { X.sp_on = false; sp_edge = true; printf("APP audio speed=100%%\n"); }
                const uint32_t xfm = __atomic_load_n(&X.xfade_ms, __ATOMIC_RELAXED);
                int64_t left = -1;
                /* the crossfade works on the source block (before the rate stage, FIX3 I3): xs frames at the source rate xr; without the stage that is the pipeline block itself */
                const int xs = sf;
                const uint32_t xr = up ? P.up_in : s_rate;
                if (join && xfm && (up || sf == frames) && xs <= MAX_BLOCK && S.o.speed) {         /* the crossfade: the last xfm ms of this track under the start of the next */
                    int32_t l = pc_gapless_remaining_ms(S.be->length_ms ? S.be->length_ms(S.h) : -1, S.be->position_ms ? S.be->position_ms(S.h) : 0,
                                                        S.w ? S.w->in.size : 0, S.w ? S.w->end : 0, S.w ? S.w->bytes : 0);
                    if (l >= 0 && (uint32_t)l <= xfm) left = (int64_t)l * (int64_t)xr / 1000;
                }
                if (!s_next || X.xf_h != s_next->h) { X.xf_h = s_next ? s_next->h : NULL; X.xf_adv = 0; X.xf_on = false; }   /* another next track: its own count */
                if (left >= 0) {
                    if (!X.xf_on) {
                        X.xf_on = true; X.xf_total = left; printf("APP audio xfade start ms=%lu left=%lld\n", (unsigned long)xfm, (long long)left);
                        if (s_next) { aud_event_t xe = { .type = AUD_XFADE, .gen = s_next->gen }; xQueueSend(s_evt, &xe, 0); }   /* the page shows the new track from its first audible moment, not when the old one has ended */
                    }
                    f = pc_gapless_fill(&cur, NULL, sb, sf);
                    int nn = f.frames;
                    float p0 = pc_xfade_progress(left, X.xf_total), p1 = pc_xfade_progress(left - xs, X.xf_total);
                    memset(X.xb, 0, (size_t)xs * 2 * sizeof(float));
                    int got = pc_gapless_fill(&nx, NULL, X.xb, nn).frames;   /* the new track alongside, as many frames as the old one gave */
                    if (nn > 0) pc_xfade_mix(sb, X.xb, nn, p0, p1);
                    X.xf_adv += (uint64_t)got;
                    if (f.cur_rc == 0 && nn < xs) {   /* the old track ended in this block: the new one carries on alone, its gain rising to 1 */
                        pc_fill_t g = pc_gapless_fill(&nx, NULL, sb + 2 * nn, xs - nn);
                        pc_xfade_finish(sb + 2 * nn, g.frames, p1);
                        f.next_frames = g.frames;
                        f.frames = nn + g.frames;
                        X.xf_adv += (uint64_t)g.frames;
                        xf_end = true;
                    }
                } else {
                    if (X.xf_on && s_next && X.xf_h == s_next->h && X.xf_adv > 0) { xf_abandon(); join = false; nx.be = NULL; nx.h = NULL; nx.gain = 1.0f; }
                    X.xf_on = false;
                    f = pc_gapless_fill(&cur, join ? &nx : NULL, sb, sf);
                }
            }
            int ns = f.frames;                             /* source frames (speed: the frames out) */
            if (!spx) adv = ns;
            float e[4] = { 0 };
            int a = f.next_frames >= 0 ? ns - f.next_frames : 0;   /* the old source's frames in a join block */
            if (f.next_frames >= 0) {                      /* the join edge (source samples) before the stage overwrites them */
                e[0] = a >= 2 ? sb[2 * (a - 2)] : a == 1 ? s_tail[1] : s_tail[0];
                e[1] = a >= 1 ? sb[2 * (a - 1)] : s_tail[1];
                e[2] = f.next_frames >= 1 ? sb[2 * a] : 0;
                e[3] = f.next_frames >= 2 ? sb[2 * (a + 1)] : 0;
            }
            n = ns;
            if (up && !spx) {                              /* LOWRATE (speed: sp_feed already ran the stage): source rate -> DAC rate (up or down), the whole block, before the post hook (the gain stage and everything after see an ordinary block) */
                int nc;
                if (f.cur_rc == 0 && f.next_frames < 0) {  /* the track ends in this block: silence behind it pushes the filter's last PC_RSQ_TAPS / 2 frames out (M2), never past the buffer */
                    int room = PC_UP_SBUF_FRAMES - P.up_carry - ns, z = PC_RSQ_TAPS / 2 < room ? PC_RSQ_TAPS / 2 : room;
                    if (z > 0) { memset(P.sbuf + 2 * (P.up_carry + ns), 0, (size_t)z * 2 * sizeof(float)); ns += z; }
                }
                n = pc_up_run(P.up, P.sbuf, P.up_carry, ns, buf, frames, &nc);
                adv = ns + P.up_carry - nc;
                if (adv < 0) adv = 0;
                P.up_carry = nc;
            } else if (!spx && sb != buf) {                       /* source rate -> pipeline rate, in place, then into the pipeline buffer */
                n = pc_halve_run(P.halve, P.sbuf, ns);
                memcpy(buf, P.sbuf, (size_t)n * 2 * sizeof(float));
            }
            if (sp_edge && n > 0) {                        /* the stage starts or stops (what was buffered in it is dropped): the first 2 ms blend from the last sample, no step */
                int m = n < 96 ? n : 96;
                for (int i = 0; i < m; i++) {
                    float k = (float)(i + 1) / (float)(m + 1);
                    buf[2 * i] = buf[2 * i] * k + X.last[0] * (1.0f - k);
                    buf[2 * i + 1] = buf[2 * i + 1] * k + X.last[1] * (1.0f - k);
                }
            }
            if (f.next_frames >= 0) {                      /* gapless: the next track carries on in this block */
                uint32_t old = S.gen;
                uint64_t of = S.frames + (uint64_t)a;
                take_next();
                S.frames = xf_end ? X.xf_adv : (uint64_t)f.next_frames;   /* a crossfade: the new track has played since it began under the old one */
                if (xf_end) printf("APP audio xfade end\n");
                X.xf_adv = 0; X.xf_h = NULL; X.xf_on = false;
                announce_next(old, of, frames - n, "join");
                printf("APP gapless edge a=%.5f,%.5f b=%.5f,%.5f\n", e[0], e[1], e[2], e[3]);
            } else {
                S.frames += (uint64_t)adv;
                src_end = f.cur_rc <= 0;
                src_err = f.cur_rc < 0;
                if (src_end && f.cur_rc == 0 && s_next && !S.fading && !plan_of(s_next->rate, s_next->burst).pipe) release_next();   /* the output cannot take it: played (and refused) at its turn */
                if (src_end && f.cur_rc == 0 && s_next && !S.fading) {   /* other rate or plan: pad this block, switch before the next */
                    s_rate_join = true;
                    s_rate_join_pad = frames - n;
                    src_end = false;
                    printf("APP gapless end gen=%lu frames=%llu, next needs %lu Hz\n", (unsigned long)S.gen, (unsigned long long)S.frames,
                           (unsigned long)s_next->rate);
                }
            }
            if (S.o.near_ms && !S.near_sent && S.h && !src_end && !s_rate_join) {   /* time to pre-open the next one? */
                int32_t left = pc_gapless_remaining_ms(S.be->length_ms ? S.be->length_ms(S.h) : -1, S.be->position_ms ? S.be->position_ms(S.h) : 0,
                                                       S.w ? S.w->in.size : 0, S.w ? S.w->end : 0, S.w ? S.w->bytes : 0);
                if (left >= 0 && left <= S.o.near_ms) { S.near_sent = true; emit(AUD_NEAR); }
            }
        }
        if (n >= 2) { s_tail[0] = buf[2 * (n - 2)]; s_tail[1] = buf[2 * (n - 1)]; }
        if (n >= 1) { X.last[0] = buf[2 * (n - 1)]; X.last[1] = buf[2 * (n - 1) + 1]; } else if (held) { X.last[0] = X.last[1] = 0; }
        if ((S.h || S.mix) && !held && src_end) {
            /* during a sleep fade an ending source finishes the fade: the owner must not start the next one */
            aud_state_t ev = S.fading ? AUD_FADED : src_err ? AUD_ERROR : AUD_ENDED;
            printf("APP audio %s\n", ev == AUD_FADED ? "faded (source ended)" : ev == AUD_ERROR ? "error" : "ended");
            if (S.h) printf("APP audio end gen=%lu frames=%llu tail=%d\n", (unsigned long)S.gen, (unsigned long long)S.frames, n);
            set_status(ev == AUD_ERROR ? AUD_ERROR : AUD_ENDED);
            emit(ev);
            release_next();
            release_current();                             /* the n frames it gave in this block still play */
        }
        if (n < frames) memset(buf + 2 * n, 0, (size_t)(frames - n) * 2 * sizeof(float));
        aud_process_fn post = __atomic_load_n(&s_post, __ATOMIC_ACQUIRE);
        if (post) {
            post(buf, frames, brate, n ? kind : -1, s_post_u);   /* every block: the fx chain also settles its fades and hands blocks back while nothing plays (src -1, buf is silent) */
            if (!n) memset(buf, 0, (size_t)frames * 2 * sizeof(float));   /* what the hook made of a block with no source is never played (taps, gain stage and sinks below see silence) */
        }
        /* spectrum tap (pre-volume, mono). Above 48k each tap sample is the average of td frames, so the taps stay <= 48k (the FFT size is unchanged) */
        int td = pc_tap_decim(brate), tn = frames / td;
        P.tap_rate = brate / (uint32_t)td;
        uint32_t w = s_spec_w;
        for (int i = 0; i < tn; i++) {
            float m = 0.0f;
            for (int k = i * td; k < (i + 1) * td; k++) m += buf[2 * k] + buf[2 * k + 1];
            s_spec_ring[(w + (uint32_t)i) % SPEC_N] = 0.5f * m / (float)td;
        }
        s_spec_w = w + (uint32_t)tn;
        /* stereo tap: same point as the mono ring (after the post hook, before the volume), only while the spectrum page shows a stereo picture */
        int16_t *st = s_st_ring;                           /* both tested: the flag alone does not order the pointer load (the ring is never freed, so non-NULL stays valid) */
        if (s_st_on && st) {
            uint32_t sw = s_st_w;
            for (int i = 0; i < tn; i++) {
                float l = 0.0f, r = 0.0f;
                for (int k = i * td; k < (i + 1) * td; k++) { l += buf[2 * k]; r += buf[2 * k + 1]; }
                l = l / (float)td * 32767.0f;
                r = r / (float)td * 32767.0f;
                l = l > 32767.0f ? 32767.0f : l < -32768.0f ? -32768.0f : l;
                r = r > 32767.0f ? 32767.0f : r < -32768.0f ? -32768.0f : r;
                uint32_t k = (sw + (uint32_t)i) & (ST_N - 1);
                st[2 * k] = (int16_t)l;
                st[2 * k + 1] = (int16_t)r;
            }
            s_st_w = sw + (uint32_t)tn;
        }
        /* toy sound effects (sfx.h): one path per block, chosen from S.paused as pc_rv_block reads it below (nothing in between changes it).
         * Not paused: added here, after the post hook and the taps and BEFORE the gain below, so they get exactly the gain the music gets (the
         * device volume on I2S, never more; unity only where pc_rv_block gives the music unity: a remote-volume headset). Paused (the gain
         * target is 0): added after the gain loop instead, times the device-volume gain and the sleep fade (pc_sfx_route: s_target_gain and the
         * fade each clamped to 0..1, never bg.target, never unity on its own). Every block comes here, with or without a source, so they also sound
         * when nothing plays. The sum is held at full scale in pc_sfx_mix_gain (the toy sound yields: it cannot make the music clip). */
        const pc_sfx_route_t sfx_rt = pc_sfx_route(S.paused, s_target_gain, S.fading ? S.fade_gain : 1.0f);
        if (!sfx_rt.post) sfx_mix(buf, frames, brate, 1.0f);
        /* volume ramp across the block, plus the sleep fade */
        if (s_ramp_in) { gain = 0.0f; s_ramp_in = false; }   /* ramps up across this block */
        /* output: one snapshot of s_sink decides the block (taken before the gain: the gain target depends on where the block goes).
         * pc_rv_block: unity only with this sink snapshot and the remote flag; a block without a sink after a unity one ramps up from 0 */
        aud_sink_fn sink = s_sink;
        pc_rv_block_t bg = pc_rv_block(sink != NULL, __atomic_load_n(&s_remote, __ATOMIC_SEQ_CST) != NULL, S.paused, unity_prev, gain, s_target_gain);
        gain = bg.start;
        unity_prev = bg.unity;
        float target = bg.target;
        if (S.fading) {
            S.fade_gain -= S.fade_step;
            if (S.fade_gain <= 0.0f) {
                printf("APP audio faded\n");
                emit(AUD_FADED);
                release_next();
                release_current();
                set_status(AUD_IDLE);
                S.fade_gain = 0.0f;
            }
            target *= S.fade_gain > 0 ? S.fade_gain : 0.0f;
        }
        float step = (target - gain) / (float)frames;
        for (int i = 0; i < frames; i++) { gain += step; buf[2 * i] *= gain; buf[2 * i + 1] *= gain; }
        gain = target;
        if (sfx_rt.post) sfx_mix(buf, frames, brate, sfx_rt.gain);   /* paused: the toy sound after the gain, at the device volume times the sleep fade (above) */
        /* hires T5: the plan follows this block's sink snapshot (no second read). The sink cleared: back to the wired plan before the I2S returns;
         * this block, made at the sink's rate, is then not written (below: brate != s_rate). */
        if (!sink && P.sink) { P.sink = false; switch_rate(replan()); info_update(); }
        if (s_wired_dirty && !sink) { s_wired_dirty = false; wired_reopen(); }   /* BTAUDIO: a wired setting changed while playing: re-plan, one brief mute */
        /* output: the snapshot above. Conversion only where its result is used. blk_max_us ends just before the sink call / the I2S write
         * (the wired number still includes the float -> integer conversion; the time of a reinit try is taken out of it; a dropped block still counts its decode time). */
        size_t written = 0;
        bool out = false;                                   /* a sink call or an I2S write happened */
        if (sink) {
            uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
            if (us > blk_max) blk_max = us;
            for (int k = 0; k < frames; k += PC_SINK_MAX_BLOCK)   /* Bluetooth: blocks at the link's pace; never more than PC_SINK_MAX_BLOCK per call (a 96k block made before the re-plan comes in two) */
                sink(buf + 2 * k, frames - k < PC_SINK_MAX_BLOCK ? frames - k : PC_SINK_MAX_BLOCK, brate);
            sink_used = out = true;
            if (bsp_audio_is_init()) {                      /* the channel and its DMA are not needed while the link carries the sound */
                bsp_audio_deinit();                         /* mutes */
                s_released = true;
                s_sink_muted = true;
                P.t_reinit = 0;                             /* the first block without a sink tries at once */
                printf("APP audio i2s released\n");
            }
            if (!P.sink) { P.sink = true; switch_rate(replan()); info_update(); }   /* the sink came: its plan from the next block (the I2S is released: no clock change) */
        } else {
            bool back = false;
            int64_t back_ms = 0;
            if (s_released && brate == s_rate) {
                int64_t tn = esp_timer_get_time();
                if (!P.t_reinit || tn - P.t_reinit >= 1000000) {
                    P.t_reinit = tn;
                    esp_err_t e = bsp_audio_reinit(s_rate);
                    int64_t took = esp_timer_get_time() - tn;
                    int64_t ms = took / 1000;
                    t0 += took;                             /* not part of blk_max_us */
                    if (e == ESP_OK) {
                        s_released = false;
                        s_dac_chk_us = esp_timer_get_time() + 100000;
                        dac_recover_check();
                        back = true;
                        back_ms = ms;
                        under0 = bsp_audio_underruns();     /* not a jump in the 10 s line */
                        su0 = bsp_audio_start_underruns();
                        for (int i = 0; i < frames; i++) {  /* the clocks just restarted: fade this first block in (the gain ramp above already ran) */
                            float k = (float)i / (float)frames;
                            buf[2 * i] *= k; buf[2 * i + 1] *= k;
                        }
                    } else {
                        printf("APP audio i2s back failed: %s\n", esp_err_to_name(e));
                        if (s_rate > 48000 && pc_hires_failure_lowers_cap(e, e == ESP_ERR_INVALID_RESPONSE)) { if (hires_off(e == ESP_ERR_INVALID_RESPONSE ? "readback" : "i2s", s_rate) != SW_RETRY) P.t_reinit = 0; }   /* re-planned lower: try at once, this block is dropped; a mixer (not halved, SW_RETRY) and other errors: retried in 1 s, the ceiling stays */
                    }
                }
            }
            uint8_t bits = pc_block_route(sink != NULL, !s_released, brate, s_rate) == PC_BLOCK_WRITE ? bsp_audio_bits() : 0;   /* 0: no channel, or the block was made at a rate the output no longer runs: dropped (spec 4.5) */
            if (bits == 32) pc_f32_to_s32(buf, pcm, 2 * frames);
            else if (bits == 24) { pc_f32_to_s32(buf, pcm, 2 * frames); pc_pack_s24(pcm, (uint8_t *)pcm, (size_t)(2 * frames)); }   /* BTAUDIO: a 24-bit slot (FakePod dac_slot24_from): the top 24 bits, packed */
            else if (bits == 16) pc_f32_to_s16(buf, (int16_t *)pcm, 2 * frames);   /* dac_bits 16: today's undithered path, the A/B reference */
            uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
            if (us > blk_max) blk_max = us;
            esp_err_t we = ESP_OK;
            if (bits == 32) we = bsp_audio_write32(pcm, (size_t)frames, &written, 100);
            else if (bits == 24) we = bsp_audio_write24((const uint8_t *)pcm, (size_t)frames, &written, 100);
            else if (bits == 16) we = bsp_audio_write((const int16_t *)pcm, (size_t)frames, &written, 100);
            if (we != ESP_OK) P.wr_err++;
            out = written > 0;
            if (out && (back || s_sink_muted)) audio_unmute();   /* after the first block is in the DMA: nothing but silence leaves the DAC before */
            if (back) printf("APP audio i2s back rate=%lu ms=%lld\n", (unsigned long)s_rate, (long long)back_ms);
            /* hires T12: the real read-back, the first 500 ms after every clock change above 48k (a few loads per block; the verdict prints once). A mismatch
             * (a divider or PLL that did not give the rate) mutes, lowers this boot's ceiling and re-plans exactly as a refused switch; the blocks already
             * written at the planned rate in that window are the spec's "<= 500 ms" (the settle's own 50 ms measurement catches a wrong APLL divider muted) */
            if (!s_released && s_rate > 48000 && bsp_audio_clock_poll() == ESP_ERR_INVALID_RESPONSE) {
                s_rate_stale = true;
                if (hires_off("readback", s_rate) == SW_OK) { audio_unmute(); s_ramp_in = true; }
            }
#ifdef BSP_DAC_CS43131   /* I7: the CS43131's serial-port flags (NOLRCK / EARLY / LATE / ERROR) 100 ms after a start and then every 2 s: at 352.8 / 384 kHz (BCK 22.6 / 24.6 MHz, at the slave limit
                      * of 24.58 MHz) any flag sets this boot's ceiling one tier down, with the reason shown (the same path as a clock read-back mismatch) */
            if (!s_released && !sink && s_dac_chk_us && esp_timer_get_time() >= s_dac_chk_us) {
                s_dac_chk_us = esp_timer_get_time() + 2000000;
                const int de = bsp_audio_dac_errors();
                if (de && s_rate >= 352800) {
                    printf("APP dac serial port errors 0x%02x at %lu Hz\n", de, (unsigned long)s_rate);
                    s_dac_note = 3; s_dac_note_arg = s_rate;
                    s_rate_stale = true;
                    if (hires_off("serial", s_rate) == SW_OK) { audio_unmute(); s_ramp_in = true; }
                }
            }
#endif
        }
        blocks++;
        if (!out) vTaskDelay(pdMS_TO_TICKS(10));            /* dropped block (no channel, or a failed write): keep real time, never spin */
        if (S.h && (blocks & 15) == 0) set_status(S.paused ? AUD_PAUSED : AUD_PLAYING);
        if ((blocks & 15) == 0) info_update();             /* the counters */
        int64_t now = esp_timer_get_time();
        if (now - t_stat >= 10000000) {
            cpu_snap_t snap1;
            cpu_snap(&snap1);
            float pct[2];
            cpu_load(&snap0, &snap1, pct);
            uint32_t under = bsp_audio_underruns();
            const uint32_t sstart = bsp_audio_start_underruns();
            if (sink_used) { under0 = under; su0 = sstart; }   /* I2S idles while Bluetooth carries the sound: not dropouts */
            printf("APP audio blocks=%lu blk_max_us=%lu underruns=%lu cpu0=%.0f cpu1=%.0f state=%d sink=%d start_underruns=%lu\n", (unsigned long)blocks,
                   (unsigned long)blk_max, (unsigned long)(under - under0), pct[0], pct[1], s_status.state, sink_used, (unsigned long)(sstart - su0));
            { uint32_t fmin; bool fv;   /* reservoir mode only: the lowest FIFO fill in this window (the margin against a decode burst); blk_max_us above is then the decode + convert time of a block, the burst length, before the queue */
              if (bsp_audio_burst_window(&fmin, &fv)) printf("APP audio fifo min_ms=%s%lu depth_ms=%d wr_err=%lu\n", fv ? "" : "na:", (unsigned long)fmin, PC_BURST_MS, (unsigned long)bsp_audio_burst_wr_err()); }
            portENTER_CRITICAL(&s_mux);                     /* for the soak log (soak_log.c) */
            s_acc.underruns += under - under0;
            if (blk_max > s_acc.blk_max_us) s_acc.blk_max_us = blk_max;
            s_acc.cpu1 = pct[1];
            s_acc.windows++;
            portEXIT_CRITICAL(&s_mux);
            blocks = blk_max = 0;
            under0 = under;
            su0 = sstart;
            sink_used = false;
            snap0 = snap1;
            t_stat = now;
        }
    }
}

void audio_svc_start(void)
{
    s_cmd = xQueueCreate(8, sizeof(cmd_t));
    s_evt = xQueueCreate(8, sizeof(aud_event_t));
    P.plan = P.logged = (pc_pipe_t){ 44100, 0, PC_PIPE_NATIVE };   /* boot: the channel's first rate, native (not logged as a change) */
    P.src_logged = 44100;
    P.hw_cap = PC_AUDIO_MAX_RATE;
    P.cap = P.cap_req = pc_dacmax_norm(cfg_int("dac_max_rate", 0), BSP_DAC_RATE_DEFAULT);   /* 0 / missing: the board's default (H26) */
    P.tap_rate = 44100;
    s_spec_ring = heap_caps_calloc(SPEC_N, sizeof(float), MALLOC_CAP_SPIRAM);
    P.halve = heap_caps_malloc(pc_halve_size(), MALLOC_CAP_SPIRAM);   /* the halving stages (~4.9 KB at three halvings): PSRAM because it is asked for */
    P.sbuf = heap_caps_malloc(2 * PC_UP_SBUF_FRAMES * sizeof(float), MALLOC_CAP_SPIRAM);   /* + 8 frames: the stage's carry and ceil (pc_up_want never reads past it) */
    if (!P.halve || !P.sbuf) {                                         /* no halving: sources above the output's ceiling are refused */
        printf("APP audio halve alloc failed\n");
        free(P.halve);
        P.halve = NULL;
    }
    if (P.halve) pc_halve_init(P.halve);
    P.up = heap_caps_malloc(pc_rsq_size(), MALLOC_CAP_SPIRAM);   /* LOWRATE: 70.5 KB PSRAM (no internal RAM), the table is built when a source the DAC cannot take opens */
    if (!P.up) printf("APP lowrate alloc failed bytes=%u\n", (unsigned)pc_rsq_size());   /* such sources are refused (with their reason) */
    bsp_audio_set_slot_mode(cfg_int("dac_slot24_from", 0));   /* BTAUDIO: FakePod 24-bit slots from the stored rate (default off) */
    int want = cfg_int("dac_bits", 32) == 16 ? 16 : 32;   /* anything but 16 means 32; read at boot */
    bsp_audio_dac_config(cfg_int("dac_hpf", 1), cfg_int("dac_fs", 0), cfg_int("dac_amute", 1));   /* DACFS: the FakePod DAC's boot-time options (no-op on the round board) */
    esp_err_t e = bsp_audio_init_w(s_rate, 6, 480, (uint8_t)want);
    if (e != ESP_OK && want == 32) {
        printf("APP audio dac_bits=32 failed: %s\n", esp_err_to_name(e));
        e = bsp_audio_init_w(s_rate, 6, 480, 16);           /* this boot only */
    }
    if (e == ESP_OK) bsp_audio_apply_options();            /* FIX4 I2: the stored slot width at boot (set_rate makes the channel again; muted, as it is until the unmute below) */
    audio_unmute();
    printf("APP audio init=%s bits=%u\n", esp_err_to_name(e), (unsigned)bsp_audio_bits());
    sfx_init();                                           /* the toy sound bank, parsed into PSRAM before the task can mix it */
    info_update();                                        /* audio info is never all zeros (the task is not running yet: no race) */
    xTaskCreatePinnedToCoreWithCaps(task, "audio", 32 * 1024, NULL, configMAX_PRIORITIES - 2, NULL, 1, MALLOC_CAP_SPIRAM);
}

/* Every store of s_live_gen goes through here (send() and play_open()): the value and the epoch change together under s_mux; readers stay lock-free.
 * Returns the epoch this store made; *was (when given) is the value it replaced, read under the same lock. */
static uint32_t live_store(uint32_t v, uint32_t *was)
{
    portENTER_CRITICAL(&s_mux);
    if (was) *was = s_live_gen;
    s_live_gen = v;
    s_live_epoch++;
    uint32_t e = s_live_epoch;
    portEXIT_CRITICAL(&s_mux);
    return e;
}

static uint32_t send(cmd_t *c)
{
    c->t_us = esp_timer_get_time();
    uint32_t live_was = s_live_gen, my_epoch = 0;   /* PAUSE etc. never restore it; PLAY / MIXER / STOP overwrite it from the value read under the lock */
    if (c->type == CMD_PLAY || c->type == CMD_MIXER) { c->gen = __atomic_add_fetch(&s_gen, 1, __ATOMIC_RELAXED); live_store(c->gen, &live_was); }
    bool paused_was = s_paused_cmd;
    if (c->type == CMD_PAUSE) s_paused_cmd = c->fade;  /* before the enqueue too: a swap that runs right after the PAUSE is queued must see it */
    if (c->type == CMD_STOP) my_epoch = live_store(0, &live_was); /* before the enqueue: the audio task can never see the stop while a chip open still counts as live */
    if (xQueueSend(s_cmd, c, pdMS_TO_TICKS(200)) != pdTRUE) {
        if (c->type == CMD_PAUSE) s_paused_cmd = paused_was;
        /* The command did not happen: a pending open stays wanted, but only if nothing was stored since this call's own store (a check-then-write
         * could undo a store made by play_open() or another send() between the check and the write).
         * live_was is the value this call's own store replaced, read under the same lock, so what is restored is exactly what it overwrote.
         * PLAY / MIXER: the expected value is this call's own gen, unique (s_gen is an atomic add), so the compare-and-swap succeeds only when no
         * other store happened since (no ABA).
         * STOP: its expected value 0 recurs (a second stop stores 0 again), so a value comparison is not enough; the epoch is bumped by every
         * store under the same lock, so the restore runs only when nothing was stored after this stop's own store.
         * A PLAY / MIXER restore that succeeds needs no epoch bump: it can only succeed when no store (and so no stop window) began after its own store. */
        if (c->type == CMD_PLAY || c->type == CMD_MIXER) {
            uint32_t exp = c->gen;
            __atomic_compare_exchange_n(&s_live_gen, &exp, live_was, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        } else if (c->type == CMD_STOP) {
            portENTER_CRITICAL(&s_mux);
            if (s_live_epoch == my_epoch) s_live_gen = live_was;
            portEXIT_CRITICAL(&s_mux);
        }
        if (c->release) c->release(c->release_arg);
        return 0;
    }
    if (c->type == CMD_PLAY || c->type == CMD_MIXER || c->type == CMD_STOP) s_paused_cmd = false;   /* a new source starts un-paused; a stop ends it */
    return c->gen;
}
bool audio_svc_paused(void) { return s_paused_cmd; }

static uint32_t play_open(const pc_backend_t *be, const pc_reader_t *r, uint32_t start_off, int track, const aud_opts_t *o,
                          aud_release_fn release, void *arg);

uint32_t audio_svc_play_ex(const pc_backend_t *be, const pc_reader_t *r, uint32_t start_off, int track, const aud_opts_t *o,
                           aud_release_fn release, void *arg)
{
    /* slow opens (90-600 ms): off the audio task. 0 = no memory / no aud_pre task, nothing released: the plain path follows.
     * DOS OPL music (the chain backend; opl itself) belongs here too: its open parses the file and runs the silent length scan (0.3-0.7 s on the board), and on
     * the audio task that was a start gap of 20-56 underrun blocks at the start of every AdPlug track (board round 3). Both read the file only during open(). */
    if (be == &backend_gme || be == &pc_backend_chain || be == &pc_backend_opl) {
        uint32_t g = play_open(be, r, start_off, track, o, release, arg);
        if (g) return g;
    }
    cmd_t c = { .type = CMD_PLAY, .be = be, .r = *r, .start_off = start_off, .track = track, .release = release, .release_arg = arg };
    c.r_self = r->u == (const void *)r;
    c.o = o ? *o : (aud_opts_t){ .gain = 1.0f };
    return send(&c);
}

uint32_t audio_svc_play(const pc_backend_t *be, const pc_reader_t *r, uint32_t start_off, int track, aud_release_fn release, void *arg)
{
    return audio_svc_play_ex(be, r, start_off, track, NULL, release, arg);
}

/* Gapless pre-open: the next source is opened here, on a low-priority task with a deep stack (minimp3 keeps
 * ~16 KB of scratch on the stack; an open reads and parses headers for tens of ms), never on the audio task. */
static QueueHandle_t s_pre;
typedef struct { next_t *n; bool r_self; } pre_job_t;   /* n NULL: a wake-up for the play-open waiting in s_play */
/* Chip play-opens (board batch A F1): at most one waits; a newer request replaces it (it would be skipped by gen anyway) and the
 * replaced one is released by the caller. The job being opened finishes and is judged by gen as before. A request is never refused
 * for a full queue, so it never falls back to opening on the audio task. s_play is shared by the caller (LVGL task) and aud_pre. */
static pc_latest_t s_play;
static portMUX_TYPE s_play_mux = portMUX_INITIALIZER_UNLOCKED;

static void play_open_run(next_t *n)              /* aud_pre: open the chip source that becomes the current one */
{
    int64_t t0 = esp_timer_get_time();
    if (n->r_self) n->r.u = &n->r;
    if (n->gen != s_live_gen) {                    /* superseded (a newer play or mixer, or a stop) while it waited: do not spend the open */
        printf("APP preopen skip gen=%lu (live=%lu)\n", (unsigned long)n->gen, (unsigned long)s_live_gen);
        post_faded_if_stopped(n->gen);
        if (n->release) n->release(n->release_arg);
        free(n);
        return;
    }
    int ok = n->be->open(&n->h, &n->r, n->start_off) == 0;
    if (ok) {
        if (n->track > 0 && n->be->start_track) n->be->start_track(n->h, n->track);   /* the sub: still off the audio task */
        n->rate = src_rate(n->be, n->h);
        n->burst = n->be->burst ? n->be->burst(n->h) : 0;
        if (!n->rate) { n->be->close(n->h); ok = 0; }          /* refused: as a failed open */
    }
    printf("APP preopen ms=%lld gen=%lu ok=%d\n", (esp_timer_get_time() - t0) / 1000, (unsigned long)n->gen, ok);
    cmd_t pc = ok ? (cmd_t){ .type = CMD_OPENED, .next = n } : (cmd_t){ .type = CMD_OPEN_FAILED, .gen = n->gen };
    uint32_t gen = n->gen;
    if (!ok) {
        if (n->release) n->release(n->release_arg);
        free(n);
    }
    if (xQueueSend(s_cmd, &pc, pdMS_TO_TICKS(200)) != pdTRUE) {
        printf("APP preopen not delivered gen=%lu\n", (unsigned long)gen);
        if (ok) free_next(n);                      /* closes the emulator and releases the caller's copy */
    }
}

static void pre_gapless(next_t *n, bool r_self)   /* aud_pre: open the gapless next source */
{
    int64_t t0 = esp_timer_get_time();
    if (r_self) n->r.u = &n->r;
    if (n->o.near_ms && (n->w = malloc(sizeof *n->w))) {
        n->w->in = n->r;
        if (r_self) n->w->in.u = &n->w->in;
        n->w->end = n->w->bytes = 0;
        n->r = (pc_reader_t){ .read = rdw_read, .size = n->w->in.size, .u = n->w };
    }
    if (n->be->open(&n->h, &n->r, 0) != 0) {
        printf("APP gapless preopen failed gen=%lu\n", (unsigned long)n->gen);
        if (n->release) n->release(n->release_arg);
        free(n->w);
        free(n);
        return;
    }
    n->rate = src_rate(n->be, n->h);
    n->burst = n->be->burst ? n->be->burst(n->h) : 0;
    if (!n->rate) {                                          /* refused: as a failed preopen, the track is played (and refused) when its turn comes */
        n->be->close(n->h);
        printf("APP gapless preopen failed gen=%lu\n", (unsigned long)n->gen);
        if (n->release) n->release(n->release_arg);
        free(n->w);
        free(n);
        return;
    }
    printf("APP gapless preopen gen=%lu ms=%lld\n", (unsigned long)n->gen, (esp_timer_get_time() - t0) / 1000);
    cmd_t c = { .type = CMD_QUEUE, .next = n };
    if (xQueueSend(s_cmd, &c, pdMS_TO_TICKS(200)) != pdTRUE) free_next(n);
}

static void pre_task(void *arg)
{
    (void)arg;
    for (;;) {
        pre_job_t j;
        if (xQueueReceive(s_pre, &j, portMAX_DELAY) != pdTRUE) continue;
        if (j.n) pre_gapless(j.n, j.r_self);
        portENTER_CRITICAL(&s_play_mux);           /* after any job: a play-open whose wake-up could not be queued is found here too */
        next_t *p = pc_latest_take(&s_play);
        portEXIT_CRITICAL(&s_play_mux);
        if (p) play_open_run(p);
    }
}

static bool ensure_pre(void)
{
    if (s_pre) return true;
    QueueHandle_t q = xQueueCreate(4, sizeof(pre_job_t));   /* a play-open and a gapless next can wait together; 4 x 8 B */
    if (!q) return false;
    s_pre = q;                                               /* the task reads it */
    if (xTaskCreatePinnedToCoreWithCaps(pre_task, "aud_pre", 32 * 1024, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        s_pre = NULL;
        vQueueDelete(q);
        return false;
    }
    return true;
}

/* Returns the gen; the request replaces a play-open still waiting (never refused for a full queue, board batch A F1). 0 with nothing
 * taken (the caller keeps the release) only when the job or the aud_pre task cannot be allocated. */
static uint32_t play_open(const pc_backend_t *be, const pc_reader_t *r, uint32_t start_off, int track, const aud_opts_t *o,
                          aud_release_fn release, void *arg)
{
    next_t *n = calloc(1, sizeof *n);
    if (!n) return 0;
    if (!ensure_pre()) { free(n); return 0; }
    n->be = be;
    n->r = *r;
    n->r_self = r->u == (const void *)r;
    n->release = release;
    n->release_arg = arg;
    n->o = o ? *o : (aud_opts_t){ .gain = 1.0f };
    n->play = true;
    n->track = track;
    n->start_off = start_off;
    n->gen = __atomic_add_fetch(&s_gen, 1, __ATOMIC_RELAXED);
    live_store(n->gen, NULL);                                    /* before the job can run: the pre-open task compares against it */
    uint32_t gen = n->gen;                                   /* n belongs to aud_pre once it is in s_play */
    bool wake;
    portENTER_CRITICAL(&s_play_mux);
    next_t *old = pc_latest_put(&s_play, n, &wake);
    portEXIT_CRITICAL(&s_play_mux);
    if (old) {                                               /* still waiting, never opened: replaced (its gen is no longer live) */
        printf("APP preopen replaced gen=%lu by gen=%lu\n", (unsigned long)old->gen, (unsigned long)gen);
        if (old->release) old->release(old->release_arg);
        free(old);
    }
    if (wake) {
        pre_job_t j = { NULL, false };
        if (xQueueSend(s_pre, &j, 0) != pdTRUE) {            /* the queue is full of gapless jobs: aud_pre takes the play job after them */
            portENTER_CRITICAL(&s_play_mux);
            pc_latest_wake_lost(&s_play);
            portEXIT_CRITICAL(&s_play_mux);
            printf("APP preopen wake deferred gen=%lu\n", (unsigned long)gen);
        }
    }
    s_paused_cmd = false;                                    /* a new source starts un-paused */
    return gen;
}

uint32_t audio_svc_queue_next(uint32_t after_gen, const pc_backend_t *be, const pc_reader_t *r, const aud_opts_t *o, aud_release_fn release, void *arg)
{
    next_t *n = calloc(1, sizeof *n);
    if (!n) { if (release) release(arg); return 0; }
    if (!ensure_pre()) { if (release) release(arg); free(n); return 0; }
    n->be = be;
    n->r = *r;
    n->release = release;
    n->release_arg = arg;
    n->after_gen = after_gen;
    n->o = o ? *o : (aud_opts_t){ .gain = 1.0f };
    n->gen = __atomic_add_fetch(&s_gen, 1, __ATOMIC_RELAXED);
    pre_job_t j = { n, r->u == (const void *)r };
    if (xQueueSend(s_pre, &j, pdMS_TO_TICKS(200)) != pdTRUE) {
        if (release) release(arg);
        free(n);
        return 0;
    }
    return n->gen;
}

void audio_svc_set_gain(uint32_t gen, float gain)
{
    cmd_t c = { .type = CMD_GAIN, .gen = gen, .o = { .gain = gain } };
    send(&c);
}

uint32_t audio_svc_play_mixer(int (*render)(float *, int, void *), void *u, uint32_t rate, aud_release_fn release, void *arg)
{
    cmd_t c = { .type = CMD_MIXER, .mix = render, .mix_u = u, .rate = rate, .release = release, .release_arg = arg };
    return send(&c);
}

int audio_svc_next_track(int track)
{
    cmd_t c = { .type = CMD_TRACK, .track = track, .gen = s_gen };
    return send(&c) ? 0 : -1;
}

void audio_svc_set_sink(aud_sink_fn fn)
{
#ifdef BSP_DAC_CS43131   /* DACFS: the wired path is unity (the DAC's volume does it), a sink gets the device volume as before. Becoming a sink: the volume first, then the sink; leaving it: the sink first, then unity
                         * (a block in between is quieter, never louder); one critical section keeps it against audio_svc_set_volume on another task (M2) */
    portENTER_CRITICAL(&s_mux);
    if (fn) s_target_gain = pc_volume_gain(s_vol_last);
    s_sink = fn;
    if (!fn) s_target_gain = s_dac_silenced ? 0.0f : pc_cs_digital_gain(s_vol_last);
    portEXIT_CRITICAL(&s_mux);
#else
    s_sink = fn;
#endif
    if (fn) {
        s_sink_muted = true;                      /* before the mute: see audio_unmute() */
        bsp_audio_mute(true);                     /* the 3.5 mm out is silent while audio goes elsewhere */
    }                                             /* clearing: the audio task re-creates the channel if needed and unmutes (only it may: the channel can be half deleted here) */
}

void audio_svc_pause(bool pause)
{
    cmd_t c = { .type = CMD_PAUSE, .fade = pause };
    send(&c);
}

void audio_svc_stop(bool fade)
{
    cmd_t c = { .type = CMD_STOP, .fade = fade };
    send(&c);
}

void audio_svc_set_cap(uint32_t hz)
{
    __atomic_store_n(&P.cap_req, hz, __ATOMIC_RELAXED);
    aud_status_t st;
    audio_svc_status(&st);
    if (st.state == AUD_PLAYING || st.state == AUD_PAUSED) printf("APP audio cap=%lu (next track)\n", (unsigned long)hz);
    else printf("APP audio cap=%lu\n", (unsigned long)hz);
}

void audio_svc_set_speed(int pct) { __atomic_store_n(&X.speed_pct, (uint32_t)(pct < 50 ? 50 : pct > 200 ? 200 : pct), __ATOMIC_RELAXED); }
void audio_svc_set_xfade(int ms) { __atomic_store_n(&X.xfade_ms, (uint32_t)(ms < 0 ? 0 : ms), __ATOMIC_RELAXED); }

void audio_svc_set_volume(int vol)
{
    portENTER_CRITICAL(&s_mux);
    vol = pc_volhold_apply(&s_hold, vol);               /* BTVOL: never above the ceiling of an output change in progress (no hold: vol as it is) */
    portEXIT_CRITICAL(&s_mux);
#ifdef BSP_DAC_CS43131   /* DACFS (FakePod only): the CS43131's own digital volume register is the volume of the wired path, the digital gain passes unity (0 at volume 0); a sink keeps the device volume.
                         * If the DAC does not answer (I2C error, reset) the digital gain goes to 0 at once, which needs no bus, and the user is told once; unity comes back only after a volume write is read back (I1) */
    const esp_err_t de = bsp_audio_set_volume(vol);
    portENTER_CRITICAL(&s_mux);
    s_dac_silenced = de != ESP_OK;
    s_vol_last = vol;
    s_target_gain = s_sink ? pc_volume_gain(vol) : (s_dac_silenced ? 0.0f : pc_cs_digital_gain(vol));
    portEXIT_CRITICAL(&s_mux);
    if (de != ESP_OK) { s_dac_note = 2; s_dac_note_arg = 0; printf("APP dac not answering: digital output silenced (volume %d)\n", vol); }
#else
    s_target_gain = pc_volume_gain(vol);                /* always the device's volume: what a block to I2S gets */
    s_vol_last = vol;
#endif
    aud_remote_vol_fn f = __atomic_load_n(&s_remote, __ATOMIC_SEQ_CST);
    if (f) f(vol);                                      /* remote volume: the value goes to the headset too */
}

void audio_svc_set_remote_volume(aud_remote_vol_fn fn) { __atomic_store_n(&s_remote, fn, __ATOMIC_SEQ_CST); }
int audio_svc_volume_last(void) { return s_vol_last; }

void audio_svc_volume_hold(int cap)
{
    portENTER_CRITICAL(&s_mux);
    pc_volhold_begin(&s_hold, cap);
    portEXIT_CRITICAL(&s_mux);
    if (s_vol_last > cap) audio_svc_set_volume(audio_svc_volume_req());   /* what plays now comes down to the ceiling (the request is kept) */
}

void audio_svc_volume_release(void)
{
    portENTER_CRITICAL(&s_mux);
    const int w = pc_volhold_end(&s_hold);
    portEXIT_CRITICAL(&s_mux);
    if (w >= 0) audio_svc_set_volume(w);
}

bool audio_svc_volume_held(void) { return pc_volhold_on(&s_hold); }

int audio_svc_volume_req(void)
{
    portENTER_CRITICAL(&s_mux);
    const int r = pc_volhold_req(&s_hold, s_vol_last);
    portEXIT_CRITICAL(&s_mux);
    return r;
}

/* The spectrum tap's write index: the audio task advances it once in every block, BEFORE that block takes its sink snapshot. A value that differs
 * from one read after audio_svc_set_sink() therefore proves that a block began after the sink was stored (and that the block before it, the only
 * one that could still hold the old snapshot, has finished its gain). */
uint32_t audio_svc_block_mark(void) { return s_spec_w; }

void audio_svc_status(aud_status_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_status;
    portEXIT_CRITICAL(&s_mux);
}

uint32_t audio_svc_live_gen(void) { return s_live_gen; }

QueueHandle_t audio_svc_events(void) { return s_evt; }

void audio_svc_spectrum_db(float floor_db, float bars[64])
{
    static float *mono;
    if (!s_spec_ring) { memset(bars, 0, 64 * sizeof(float)); return; }   /* the tap ring is not allocated yet */
    if (!mono) mono = heap_caps_malloc(SPEC_N * sizeof(float), MALLOC_CAP_SPIRAM);
    if (!mono) { memset(bars, 0, 64 * sizeof(float)); return; }
    uint32_t w = s_spec_w;
    for (int i = 0; i < SPEC_N; i++) mono[i] = s_spec_ring[(w + (uint32_t)i) % SPEC_N];
    pc_spectrum_bars_db(mono, SPEC_N, P.tap_rate, floor_db, bars);
}

void audio_svc_spectrum(float bars[64]) { audio_svc_spectrum_db(-60.0f, bars); }

/* The ring holds 0.5 * (L + R) of float samples whose full scale is +-1.0 (the backends and the mixer deliver -1..1), so the
 * divisor is 1: the values are copied as they are. The writer may overtake the reader (a torn picture is fine, an index out of
 * the ring is not: every index is taken modulo SPEC_N). */
int audio_svc_wave(float *out, int n)
{
    if (!s_spec_ring || !out || n <= 0) return 0;
    if (n > SPEC_N) n = SPEC_N;
    uint32_t w = s_spec_w;
    for (int i = 0; i < n; i++) out[i] = s_spec_ring[(w + (uint32_t)(SPEC_N - n + i)) % SPEC_N];
    return n;
}

/* Each backend clears its snapshot before it frees its emulator or module, so whichever one still has voices is the one playing. */
int audio_svc_voices(pc_voices_t *out)
{
    if (!out) return 0;
    int n = backend_gme_voices(out);
    if (n <= 0) n = pc_backend_xmp_voices(out);
    if (n <= 0) n = pc_extvoice_read(out);
    if (n > 0 && audio_svc_paused()) memset(out->level, 0, sizeof out->level);   /* a paused source is not rendered: its last levels would freeze on screen */
    return n;
}

void audio_svc_set_post(aud_process_fn fn, void *u)
{
    s_post_u = u;
    __atomic_store_n(&s_post, fn, __ATOMIC_RELEASE);   /* the audio task reads s_post with acquire, then s_post_u */
}

/* UI task only. on: the first time allocates the ring (PSRAM, 4 KB), then lets the audio task write it. Returns true when the tap is on;
 * allocation failure: stays off and returns false. Frames from before the switch-on are never returned (s_st_base). */
bool audio_svc_want_stereo(bool on)
{
    if (!on) { s_st_on = false; return false; }
    if (s_st_on) return true;
    if (!s_st_ring) {
        int16_t *p = heap_caps_calloc(2 * ST_N, sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!p) return false;
        s_st_ring = p;
    }
    s_st_base = s_st_w;                                 /* the writer is idle while the flag is off, so this is where it carries on */
    __sync_synchronize();                               /* pointer and base are visible before the flag */
    s_st_on = true;
    return true;
}

/* UI task: the newest frames (<= 1024) interleaved L/R, oldest first, but only frames written since the tap was switched on:
 * 0 while off, or before the writer has run. A write in progress may tear the picture, never the indexes. */
int audio_svc_stereo(int16_t *lr, int frames)
{
    if (!s_st_on || !s_st_ring || !lr || frames <= 0) return 0;
    uint32_t w = s_st_w, got = w - s_st_base;
    if (got == 0) return 0;                             /* nothing written yet; the index only advances, and after a 2^32 wrap got is small for a few ticks: the clamp below gives short reads */
    if (frames > ST_N) frames = ST_N;
    if ((uint32_t)frames > got) frames = (int)got;
    for (int i = 0; i < frames; i++) {
        uint32_t k = (w + (uint32_t)(ST_N - frames + i)) & (ST_N - 1);
        lr[2 * i] = s_st_ring[2 * k];
        lr[2 * i + 1] = s_st_ring[2 * k + 1];
    }
    return frames;
}
