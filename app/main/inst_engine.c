// SPDX-License-Identifier: LGPL-2.1-or-later
#include "inst_engine.h"
#include "cfg.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bsp/bsp_audio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "inst_chip.h"
#include "inst_kit.h"
#include "pc/pc_looper.h"
#include "pc/pc_metro.h"
#include "pc/pc_sampler.h"
#include "pc/pc_tempo.h"
#include "pc/pc_midits.h"
#include "pc/pc_ring.h"
#include "pc/pc_volume.h"
#include "pc/pc_cs43131.h"

typedef struct { pc_midi_ev_t ev; int64_t t_us, src_us; } msg_t;   /* t: when it should sound; src: clock tick time */

/* rings, buffers and the router live in PSRAM (app .bss, linker.lf) */
static uint8_t s_ble_mem[128 * sizeof(msg_t)], s_ui_mem[32 * sizeof(msg_t)], s_note_mem[64 * sizeof(inst_note_t)];
static uint8_t s_beat_mem[16 * sizeof(inst_beat_t)];
static pc_ring_t s_ble_q, s_ui_q, s_note_q, s_beat_q;
static pc_instr_t s_r;
static pc_midits_t s_ts;
static int16_t *s_tmp, *s_out;                              /* 2 x 256 B internal: the render path */
static int32_t *s_mix;                                      /* 512 B internal */
static uint32_t s_under0;                                   /* underruns before the first real block */

static volatile int s_want_chip, s_chip = -1, s_want_map, s_want_ch, s_vol;
static volatile bool s_stable, s_want_off, s_stop, s_stopped, s_ts_reset, s_chip_ok, s_ble_flush;
static volatile int s_chip_log = -1;                        /* a chip change for the main task to print */
static pc_noteroute_t s_nr;                                 /* where each sounding note went (its off goes there) */
static volatile int s_last_note = -1, s_peak;
static volatile uint32_t s_events, s_by_type[PC_MIDI_STOP + 1];
static inst_stats_t s_st;
static uint32_t s_deferred;                                 /* event bursts cut at EV_BUDGET_US */

/* metronome, clock follower, kit layer: audio task state */
#define LOOK (INST_RATE * 30 / 1000)                        /* beats are committed 30 ms ahead (haptic lead) */
#define DAC_LATENCY_US 4500                                 /* 3 x 64-frame DMA buffers + the PCM5102A filter */
static pc_metro_t s_metro;
static pc_clk_t s_clk;
static pc_aclock_t s_ac;
static pc_sampler_t s_samp;
static int64_t s_pos;                                       /* frames written */
static uint32_t s_ext_k;                                    /* next external beat; UINT32_MAX: find it again */
static const inst_kit_t *s_kit;
static const inst_kit_t *volatile s_want_kit, *volatile s_kit_ack;
static volatile int s_m_cmd = -1, s_m_bpm = 120, s_m_bpb = 4, s_layer;
static volatile bool s_clk_ext, s_click = true;
static volatile inst_metro_t s_mst;
static TaskHandle_t s_beat_task;
static int64_t s_last_ext_pos;
static uint32_t s_chist[41];
static volatile bool s_creset = true;
static float s_bpm_lo, s_bpm_hi;
/* looper (stage 6): pool + buckets in PSRAM, commands through a queue (UI, console), state published per block */
#define LOOP_POOL 16384
#define LOOP_HEADS 65536                                    /* x 128 samples: 174 s */
static pc_looper_t s_lp;
static QueueHandle_t s_loop_q;
static volatile bool s_loop_park, s_loop_parked;
static volatile uint32_t s_loop_posted;
static bool s_loop_metro;                                   /* the looper started the metronome */
static int s_loop_bars = 2, s_loop_quant;
static volatile int s_loop_msg;
static inst_loop_t s_lst;
#define LST 256                                             /* timing check: BLE note-ons recorded / replayed */
static struct { uint32_t frame; uint16_t ts; } s_lrec[LST];
static struct { uint32_t frame; int32_t err; } s_lemit[LST];
static volatile int s_nlrec, s_nlemit;
static volatile int s_kit_max, s_kit_max_auto = PC_SAMP_VOICES;   /* 0: by chip (FM: INST_KIT_VOICES_FM) */

void inst_engine_post_ble(const pc_midi_ev_t *ev, int n, int64_t arrival_us)
{
    if (s_ts_reset) { pc_midits_reset(&s_ts); s_ts_reset = false; }
    for (int i = 0; i < n; i++) {
        msg_t m = { ev[i], arrival_us, 0 };
        m.src_us = pc_midits_map(&s_ts, ev[i].ts, arrival_us);   /* when it was played: clock follower, looper */
        if (ev[i].type < PC_MIDI_CLOCK && s_stable) m.t_us = pc_midits_due(&s_ts, ev[i].ts, arrival_us, 8000);
        if (pc_ring_free(&s_ble_q) >= sizeof m) pc_ring_write(&s_ble_q, &m, sizeof m);
    }
}

void inst_engine_post_ui(const pc_midi_ev_t *ev)
{
    int64_t t = esp_timer_get_time();
    msg_t m = { *ev, t, t };
    if (pc_ring_free(&s_ui_q) >= sizeof m) pc_ring_write(&s_ui_q, &m, sizeof m);
}

void inst_engine_ble_connected(bool on)
{
    s_ts_reset = true;
    if (!on) { s_ble_flush = true; s_want_off = true; }    /* what is still queued must not play after the all off */
}
/* The new chip is reset here, on the caller's task, while the audio task still renders the old one (a reset
 * allocates its buffer: up to 22 ms, measured). Never the chip being rendered or already queued. */
static bool s_reset_ok[16];
void inst_engine_log_poll(void)
{
    int c = __atomic_exchange_n(&s_chip_log, -1, __ATOMIC_ACQ_REL);
    if (c >= 0) printf("INST chip=%s ok=%d\n", inst_chip(c)->id, s_reset_ok[c]);
}
void inst_engine_set_chip(int idx)
{
    const pc_synth_t *c = inst_chip(idx);
    if (!c || idx >= 16) return;
    if (idx != s_chip && idx != s_want_chip) s_reset_ok[idx] = c->reset(INST_RATE) == 0;
    __atomic_store_n(&s_want_chip, idx, __ATOMIC_RELEASE);
}
int  inst_engine_chip(void) { return s_want_chip; }
const pc_synth_t *inst_engine_synth(void) { return inst_chip(s_want_chip); }
void inst_engine_set_map(pc_map_t m) { s_want_map = m; }
void inst_engine_set_channel(int ch) { s_want_ch = ch; }
void inst_engine_set_volume(int vol) { s_vol = vol < 0 ? 0 : vol > 100 ? 100 : vol; bsp_audio_set_volume(s_vol); }   /* FakePod: the DAC register (DACFS); no-op elsewhere */
void inst_engine_set_stable(bool on) { s_stable = on; s_ts_reset = true; }
void inst_engine_all_off(void) { s_want_off = true; }
int  inst_engine_last_note(void) { return s_last_note; }
bool inst_engine_note_pop(inst_note_t *n) { return pc_ring_used(&s_note_q) >= sizeof *n && pc_ring_read(&s_note_q, n, sizeof *n) == sizeof *n; }
void inst_engine_peak_reset(void) { s_peak = 0; }
void inst_engine_worst_reset(void) { s_st.worst_us = 0; }

void inst_engine_metro_run(int on) { s_m_cmd = on; }
void inst_engine_set_bpm(int bpm) { s_m_bpm = bpm < PC_BPM_MIN ? PC_BPM_MIN : bpm > PC_BPM_MAX ? PC_BPM_MAX : bpm; }
void inst_engine_set_bpb(int bpb) { s_m_bpb = bpb < 2 ? 2 : bpb > 7 ? 7 : bpb; }
void inst_engine_set_clock_ext(bool ext) { s_clk_ext = ext; }
void inst_engine_set_click(bool on) { s_click = on; }
void inst_engine_metro(inst_metro_t *m) { *m = *(const inst_metro_t *)&s_mst; }
void inst_engine_set_beat_task(TaskHandle_t t) { s_beat_task = t; }
bool inst_engine_beat_pop(inst_beat_t *b) { return pc_ring_used(&s_beat_q) >= sizeof *b && pc_ring_read(&s_beat_q, b, sizeof *b) == sizeof *b; }
void inst_engine_set_layer(pc_layer_t l) { s_layer = l; }
int  inst_engine_kit_voices(void) { return pc_sampler_active(&s_samp); }
void inst_engine_set_kit_voices(int n) { s_kit_max = n < 0 ? 0 : n > PC_SAMP_VOICES ? PC_SAMP_VOICES : n; }

void inst_engine_set_kit(const inst_kit_t *k)
{
    s_want_kit = k;
    for (int i = 0; i < 100 && s_kit_ack != k && !s_stopped; i++) vTaskDelay(pdMS_TO_TICKS(2));
}

void inst_engine_clkstat(int *n, float *p50, float *p95, float *max, float *bpm_min, float *bpm_max)
{
    uint32_t h[41], tot = 0;
    memcpy(h, s_chist, sizeof h);
    for (int i = 0; i < 41; i++) tot += h[i];
    *n = (int)tot;
    *p50 = *p95 = *max = 0;
    uint32_t c = 0;
    for (int i = 0; i < 41; i++) {
        c += h[i];
        if (!*p50 && tot && c >= tot / 2) *p50 = (i + 1) * 0.25f;
        if (!*p95 && tot && c >= tot * 95 / 100) *p95 = (i + 1) * 0.25f;
        if (h[i]) *max = (i + 1) * 0.25f;
    }
    *bpm_min = s_bpm_lo;
    *bpm_max = s_bpm_hi;
    s_creset = true;
}

void inst_engine_stats(inst_stats_t *s)
{
    *s = s_st;
    s->underruns = bsp_audio_underruns() - s_under0;
    s->events = s_events;
    memcpy(s->by_type, (const void *)s_by_type, sizeof s->by_type);
    s->peak = s_peak;
    s->chip_ok = s_chip_ok;
    s->deferred = s_deferred;
}

void inst_engine_stop(void)
{
    s_stop = true;
    for (int i = 0; i < 50 && !s_stopped; i++) vTaskDelay(pdMS_TO_TICKS(2));
}

/* timing error of BLE note-ons: played spacing minus the sender's timestamp spacing, 0.5 ms bins */
#define JBINS 41
static uint32_t s_jhist[JBINS];
static int64_t s_jprev_t;
static uint16_t s_jprev_ts;
static volatile bool s_jreset = true;
static void jitter_note(const msg_t *m, int64_t now)
{
    if (s_jreset) { memset(s_jhist, 0, sizeof s_jhist); s_jprev_t = 0; s_jreset = false; }
    if (s_jprev_t) {
        int64_t want = (int64_t)((m->ev.ts - s_jprev_ts) & 0x1FFF) * 1000, got = now - s_jprev_t;
        int64_t e = got > want ? got - want : want - got;
        if (want < 1000000) s_jhist[e / 500 < JBINS - 1 ? e / 500 : JBINS - 1]++;
    }
    s_jprev_t = now;
    s_jprev_ts = m->ev.ts;
}
void inst_engine_jitter(int *n, float *p50, float *p95, float *p99, float *max)
{
    uint32_t h[JBINS], tot = 0;
    memcpy(h, s_jhist, sizeof h);
    for (int i = 0; i < JBINS; i++) tot += h[i];
    *n = (int)tot;
    float q[3] = { 0.5f, 0.95f, 0.99f }, *o[3] = { p50, p95, p99 };
    for (int k = 0; k < 3; k++) {
        uint32_t c = 0;
        *o[k] = 0;
        for (int i = 0; i < JBINS; i++) { c += h[i]; if (tot && c >= q[k] * tot) { *o[k] = (i + 1) * 0.5f; break; } }
    }
    *max = 0;
    for (int i = JBINS - 1; i >= 0; i--) if (h[i]) { *max = (i + 1) * 0.5f; break; }
    s_jreset = true;
}

static void note_post(int voice, int note, int vel, int pad)
{
    inst_note_t n = { (uint8_t)voice, (uint8_t)note, (uint8_t)vel, (uint8_t)pad };
    s_last_note = note;
    if (pc_ring_free(&s_note_q) >= sizeof n) pc_ring_write(&s_note_q, &n, sizeof n);
}

/* a note into the kit layer (MIDI or a pad) */
static void kit_note(const pc_midi_ev_t *e)
{
    const inst_kit_t *k = s_kit;
    if (!k || e->a > 127 || e->b > 127) return;
    if (e->type != PC_MIDI_NOTE_ON && e->type != PC_MIDI_NOTE_OFF) return;
    int r = pc_kit_find(k->def, e->a);
    if (r < 0 || k->smp_of[r] < 0) return;
    const pc_kit_region_t *g = &k->def->r[r];
    if (e->type == PC_MIDI_NOTE_OFF || !e->b) { pc_sampler_note_off(&s_samp, e->ch, e->a); return; }
    pc_sampler_note_on(&s_samp, &k->smp[k->smp_of[r]], e->a, g->center, e->b, e->ch, g->one_shot);
    note_post(INST_VOICE_KIT, e->a, e->b, pc_kit_pad_of(k->def, e->a) + 1);
}

static void ext_resync(void) { s_ext_k = UINT32_MAX; }

static void transport(const msg_t *m)
{
    switch (m->ev.type) {
    case PC_MIDI_CLOCK: pc_clk_tick(&s_clk, m->src_us); break;
    case PC_MIDI_START:
        pc_clk_start(&s_clk);
        if (s_clk_ext) { s_m_cmd = 1; s_ext_k = 0; }
        break;
    case PC_MIDI_CONTINUE:
        pc_clk_continue(&s_clk);
        if (s_clk_ext) { s_m_cmd = 1; ext_resync(); }
        break;
    case PC_MIDI_STOP:
        pc_clk_stop(&s_clk);
        if (s_clk_ext) s_m_cmd = 0;
        break;
    default: break;
    }
}

static void route(const pc_midi_ev_t *e)
{
    int v = -1;
    if (e->ch == INST_CH_PAD) kit_note(e);                  /* notes only (kit_note checks) */
    else if (e->type == PC_MIDI_NOTE_ON || e->type == PC_MIDI_NOTE_OFF) {
        /* a note-off goes where its note-on went (layer / channel / kit may have changed since) */
        int r = pc_noteroute(&s_nr, e, pc_layer_route((pc_layer_t)s_layer, s_r.channel, e->ch, s_kit != NULL));
        if (r & PC_ROUTE_KIT) kit_note(e);
        if (r & PC_ROUTE_CHIP) v = pc_instr_event(&s_r, e);
    } else {
        v = pc_instr_event(&s_r, e);
        if (e->type == PC_MIDI_CC && (e->a == 120 || e->a == 123)) { pc_sampler_all_off(&s_samp); pc_noteroute_reset(&s_nr); }
    }
    if (v >= 0) note_post(v, e->a, e->b, 0);
}

/* a live event into the looper, at the sample the player heard when they played it */
static void loop_live(const msg_t *m, bool ble)
{
    if (s_loop_parked) return;
    uint32_t n0 = s_lp.n;
    pc_lp_record(&s_lp, &m->ev, pc_aclock_pos(&s_ac, m->src_us));
    if (ble && s_lp.n > n0 && m->ev.type == PC_MIDI_NOTE_ON && m->ev.b && s_nlrec < LST) {
        s_lrec[s_nlrec].frame = s_lp.ev[s_lp.n - 1].frame;
        s_lrec[s_nlrec].ts = m->ev.ts;
        s_nlrec++;
    }
}

static void play(const msg_t *m, int64_t now, uint64_t *wait_sum, uint32_t *wait_n, uint32_t *wait_max, bool ble)
{
    if (m->ev.type >= PC_MIDI_CLOCK) { transport(m); s_by_type[m->ev.type]++; return; }
    if (ble && m->ev.type == PC_MIDI_NOTE_ON) jitter_note(m, now);
    route(&m->ev);
    loop_live(m, ble);
    s_events++;
    if (m->ev.type <= PC_MIDI_STOP) s_by_type[m->ev.type]++;
    uint32_t w = now > m->t_us ? (uint32_t)(now - m->t_us) : 0;
    *wait_sum += w; (*wait_n)++;
    if (w > *wait_max) *wait_max = w;
}

/* A burst (a chord plus 16 pads: 300-600 us measured) is spread over blocks: once EV_BUDGET_US has gone on
 * events in this block, the rest waits for the next one (1.33 ms later; spec 16) */
#define EV_BUDGET_US 300
static bool drain(pc_ring_t *q, int64_t until, int64_t now, uint64_t *ws, uint32_t *wn, uint32_t *wm, bool ble, bool first)
{
    msg_t m;
    while (pc_ring_used(q) >= sizeof m) {
        pc_ring_peek(q, &m, sizeof m);
        if (m.t_us > until) break;                          /* stable timing: not yet (keeps the order) */
        if (!first && esp_timer_get_time() - now > EV_BUDGET_US) { s_deferred++; return false; }
        first = false;
        pc_ring_read(q, &m, sizeof m);
        play(&m, now, ws, wn, wm, ble);
    }
    return first;
}

static void all_off(void)
{
    pc_instr_all_off(&s_r);
    pc_sampler_all_off(&s_samp);
    pc_noteroute_reset(&s_nr);
}

static void apply_controls(void)
{
    int want = __atomic_load_n(&s_want_chip, __ATOMIC_ACQUIRE);   /* once: set_chip resets by this value */
    if (want != s_chip) {
        const pc_synth_t *c = inst_chip(want);
        pc_instr_set_chip(&s_r, c);                         /* all off on the old one; c was reset by set_chip */
        s_chip = want;
        s_chip_ok = s_reset_ok[want];
        s_chip_log = want;                                  /* printed by the main task: no printf here */
        s_kit_max_auto = !strcmp(c->id, "fm") ? INST_KIT_VOICES_FM : PC_SAMP_VOICES;
    }
    if ((pc_map_t)s_want_map != s_r.mode) pc_instr_set_mode(&s_r, (pc_map_t)s_want_map);
    if (s_want_ch != s_r.channel) { pc_instr_set_channel(&s_r, s_want_ch); pc_sampler_all_off(&s_samp); }
    int kv = s_kit_max ? s_kit_max : s_kit_max_auto;
    if (kv != pc_sampler_voices(&s_samp)) pc_sampler_set_voices(&s_samp, kv);
    if (s_ble_flush) {                                      /* disconnect: drop what is still queued */
        s_ble_flush = false;
        msg_t m;
        while (pc_ring_read(&s_ble_q, &m, sizeof m) == sizeof m) ;
    }
    if (s_want_off) { s_want_off = false; all_off(); }
    if (s_want_kit != s_kit) {                              /* the old kit's memory is about to go: stop now */
        pc_sampler_kill(&s_samp);
        s_kit = s_want_kit;
        s_kit_ack = s_kit;
    }
}

static void beat_post(const pc_beat_t *b, int kind, int64_t period_us, int64_t now);

/* ---- looper ---- */
static int64_t s_emit_pos;
static void loop_emit(void *ctx, const pc_midi_ev_t *e)
{
    (void)ctx;
    route(e);
    if (e->type == PC_MIDI_NOTE_ON && e->b && s_lp.len && s_nlemit < LST) {
        /* where it plays (this block's start) against where it is (start + whole passes + its position) */
        int64_t rel = s_emit_pos - s_lp.start;
        int64_t lo = rel < 0 ? 0 : rel % s_lp.len, f = s_lp.emit_frame;
        int64_t ideal = s_emit_pos - lo + f + (f < lo ? s_lp.len : 0);
        s_lemit[s_nlemit].frame = (uint32_t)f;
        s_lemit[s_nlemit].err = (int32_t)(s_emit_pos - ideal);
        s_nlemit++;
    }
}

static double beat_samples(void)
{
    return s_clk_ext ? 24.0 * pc_clk_period_us(&s_clk) * INST_RATE / 1e6 : s_metro.interval;
}

/* a beat of the running metronome to count from */
static void metro_ref(int64_t *pos, uint32_t *idx)
{
    if (s_metro.pending || s_metro.idx > 0) { *pos = s_metro.next.pos; *idx = s_metro.next.idx; }
    else { *pos = llround(s_metro.next_pos); *idx = s_metro.idx; }
}

static void metro_own_stop(int64_t now)
{
    if (s_loop_metro && s_metro.running) { pc_metro_stop(&s_metro); beat_post(NULL, INST_BEAT_STOP, 0, now); }
    s_loop_metro = false;
}

static void loop_all_off(void)
{
    pc_lp_all_off(&s_lp, (1 << PC_LP_TRACKS) - 1, loop_emit, NULL);
    all_off();
}

/* the first take: one bar counted in, on the metronome's bar lines (started here when it is not running) */
static void loop_arm(int64_t now)
{
    bool locked = pc_clk_locked(&s_clk, now);
    if (s_clk_ext && !(s_metro.running && locked)) { s_loop_msg = INST_LOOP_MSG_NOCLOCK; return; }
    double iv = beat_samples();
    int bpb = s_metro.bpb;
    int64_t start;
    if (!s_metro.running) {
        int64_t m0 = s_pos + INST_BLOCK + LOOK;
        pc_metro_start(&s_metro, m0);
        beat_post(NULL, INST_BEAT_START, 0, now);
        s_loop_metro = true;
        start = m0 + llround(iv * bpb);
    } else {
        int64_t bp;
        uint32_t bi;
        metro_ref(&bp, &bi);
        start = pc_lp_next_downbeat(s_pos + INST_BLOCK + LOOK, bp, bi, iv, bpb, iv * bpb - iv / 2);
    }
    uint32_t beat = (uint32_t)llround(iv);                  /* whole samples: the metronome then runs on exactly this */
    if (pc_lp_arm(&s_lp, start, beat * (uint32_t)bpb, beat, s_loop_bars, s_loop_quant) != 0)
        s_loop_msg = INST_LOOP_MSG_TOOLONG;
    s_nlrec = s_nlemit = 0;
}

static void loop_play(void)
{
    int64_t start = s_pos + INST_BLOCK;
    if (s_metro.running) {                                  /* back in on the next bar line */
        int64_t bp;
        uint32_t bi;
        metro_ref(&bp, &bi);
        start = pc_lp_next_downbeat(s_pos + INST_BLOCK + LOOK, bp, bi, beat_samples(), s_metro.bpb, 0);
    }
    pc_lp_play(&s_lp, start);
    s_nlemit = 0;
}

static void loop_cmd(int op, int arg, int64_t now)
{
    int64_t heard = pc_aclock_pos(&s_ac, now);
    switch (op) {
    case INST_LOOP_REC:
        switch (s_lp.state) {
        case PC_LP_EMPTY: loop_arm(now); break;
        case PC_LP_COUNTIN: pc_lp_stop(&s_lp, heard, loop_emit, NULL); metro_own_stop(now); break;
        case PC_LP_TAKE: pc_lp_end_take(&s_lp, s_pos); break;
        case PC_LP_PLAY: pc_lp_overdub(&s_lp, !s_lp.rec, heard); break;
        case PC_LP_STOP: loop_play(); pc_lp_overdub(&s_lp, true, heard); break;
        }
        break;
    case INST_LOOP_PLAYSTOP:
        if (s_lp.state == PC_LP_STOP) loop_play();
        else if (s_lp.state != PC_LP_EMPTY) {
            bool take = s_lp.state != PC_LP_PLAY;
            pc_lp_stop(&s_lp, heard, loop_emit, NULL);
            loop_all_off();
            if (take || s_loop_metro) metro_own_stop(now);
        }
        break;
    case INST_LOOP_SEL: pc_lp_select(&s_lp, arg); break;
    case INST_LOOP_MUTE: pc_lp_mute(&s_lp, arg, loop_emit, NULL); break;
    case INST_LOOP_CLEAR: pc_lp_clear(&s_lp, arg, loop_emit, NULL); break;
    case INST_LOOP_UNDO: pc_lp_undo(&s_lp, loop_emit, NULL); break;
    case INST_LOOP_RESET:
        if (s_lp.state == PC_LP_PLAY) { pc_lp_stop(&s_lp, heard, loop_emit, NULL); loop_all_off(); }
        pc_lp_reset(&s_lp);
        metro_own_stop(now);
        break;
    case INST_LOOP_QUANT: s_loop_quant = arg < 0 ? 0 : arg > 2 ? 2 : arg; pc_lp_set_quant(&s_lp, s_loop_quant); break;
    case INST_LOOP_BARS: s_loop_bars = arg; break;
    default: break;
    }
}

static void loop_block(int64_t now)
{
    if (s_loop_park != s_loop_parked) {                     /* file access from another task */
        if (s_loop_park) {
            if (s_lp.state == PC_LP_PLAY) { pc_lp_stop(&s_lp, 0, loop_emit, NULL); loop_all_off(); }
            if (s_lp.state != PC_LP_STOP) pc_lp_reset(&s_lp);
            metro_own_stop(now);
        }
        s_loop_parked = s_loop_park;
    }
    if (s_loop_parked) return;
    if (s_lp.wipe) pc_lp_wipe(&s_lp, 2048);                 /* after a reset: 4 KB of bucket heads a block */
    static uint32_t taken, div;
    bool news = false;
    if (s_loop_posted != taken) {                           /* the queue only when something was posted */
        struct { int8_t op, arg; } c;
        while (xQueueReceive(s_loop_q, &c, 0) == pdTRUE) { loop_cmd(c.op, c.arg, now); taken++; news = true; }
    }
    pc_lp_state_t was = s_lp.state;
    if (was != PC_LP_EMPTY && was != PC_LP_STOP) {
        s_emit_pos = s_pos;
        pc_lp_block(&s_lp, s_pos, INST_BLOCK, loop_emit, NULL);
        if (was == PC_LP_TAKE && s_lp.state == PC_LP_PLAY) s_nlemit = 0;
    }
    if (!news && ++div < 8 && s_lp.state == was) return;    /* the screen reads it at 10 Hz: every 8th block will do */
    div = 0;
    bool undo = s_lp.undo_kind != PC_LP_UNDO_NONE && (s_lp.state == PC_LP_PLAY || s_lp.state == PC_LP_STOP);
    inst_loop_t st = { (int)s_lp.state, s_lp.rec, s_lp.full, undo, s_lp.sel, s_lp.mute,
                       { s_lp.cnt[0], s_lp.cnt[1], s_lp.cnt[2], s_lp.cnt[3] }, s_lp.len, 0, 0, 0, s_lp.n, s_lp.cap, s_loop_msg };
    if (s_lp.state == PC_LP_PLAY && s_lp.len) {
        int64_t rel = s_pos - s_lp.start;
        st.pos = rel < 0 ? 0 : (uint32_t)(rel % s_lp.len);
    } else if (s_lp.state == PC_LP_TAKE) st.pos = (uint32_t)(s_pos - s_lp.start);
    else if (s_lp.state == PC_LP_COUNTIN) st.countin = (int32_t)((s_lp.start - s_pos) / (s_lp.beat ? s_lp.beat : 1)) + 1;
    st.bar = s_lp.bar;
    s_lst = st;
}

static void beat_post(const pc_beat_t *b, int kind, int64_t period_us, int64_t now)
{
    inst_beat_t m = { b ? pc_aclock_time(&s_ac, b->pos) : now, now, b ? b->idx : 0, (int32_t)period_us,
                      b ? b->beat : 0, b ? b->accent : 0, (uint8_t)kind, s_clk_ext };
    if (pc_ring_free(&s_beat_q) >= sizeof m) pc_ring_write(&s_beat_q, &m, sizeof m);
    if (s_beat_task) xTaskNotifyGive(s_beat_task);
}

/* start / stop, tempo, and the next beat (committed LOOK ahead of the block being rendered) */
static void metro_block(int64_t now)
{
    int cmd = __atomic_exchange_n(&s_m_cmd, -1, __ATOMIC_ACQ_REL);
    if (cmd == 2) cmd = !s_metro.running;
    bool ext = s_clk_ext, fixed = s_lp.state != PC_LP_EMPTY && s_lp.beat;   /* a loop: its own tempo and bar */
    int bpb = fixed ? (int)(s_lp.bar / s_lp.beat) : s_m_bpb;
    if (s_metro.bpb != bpb) pc_metro_set_bpb(&s_metro, bpb);
    if (fixed && !ext) { if (s_metro.interval != (double)s_lp.beat) pc_metro_set_bpm(&s_metro, 60.0 * INST_RATE / s_lp.beat); }
    else if (!ext && (int)lround(60.0 * INST_RATE / s_metro.interval) != s_m_bpm) pc_metro_set_bpm(&s_metro, s_m_bpm);
    if (cmd == 1 && !s_metro.running) {
        pc_metro_start(&s_metro, s_pos + INST_BLOCK + LOOK);
        if (ext && s_ext_k != 0) ext_resync();
        beat_post(NULL, INST_BEAT_START, 0, now);
    } else if (cmd == 0 && s_metro.running) {
        pc_metro_stop(&s_metro);
        beat_post(NULL, INST_BEAT_STOP, 0, now);
    }
    bool locked = pc_clk_locked(&s_clk, now);
    pc_beat_t b;
    if (s_metro.running && !ext) {
        if (pc_metro_plan(&s_metro, s_pos, s_pos + INST_BLOCK + LOOK, &b))
            beat_post(&b, INST_BEAT_TICK, (int64_t)(s_metro.interval * 1000000.0 / INST_RATE), now);
    } else if (s_metro.running && ext && locked && !s_metro.pending) {
        int64_t heard = pc_aclock_time(&s_ac, s_pos);       /* the earliest a click can still sound */
        if (s_ext_k == UINT32_MAX) s_ext_k = pc_clk_beat_after(&s_clk, heard);
        int64_t t;
        if (pc_clk_beat_time(&s_clk, s_ext_k, &t)) {
            double beat_us = 24.0 * pc_clk_period_us(&s_clk);
            if (t < heard - (int64_t)(beat_us / 2)) {        /* fell behind (the fit moved): pick up from here */
                s_ext_k = pc_clk_beat_after(&s_clk, heard);
                pc_clk_beat_time(&s_clk, s_ext_k, &t);
            }
            int64_t pos = pc_aclock_pos(&s_ac, t);
            if (pos < s_pos + INST_BLOCK + LOOK && pc_metro_commit(&s_metro, s_pos, pos, s_ext_k, &b)) {
                s_ext_k++;
                beat_post(&b, INST_BEAT_TICK, (int64_t)beat_us, now);
                if (s_creset) { memset(s_chist, 0, sizeof s_chist); s_last_ext_pos = 0; s_bpm_lo = 1e9f; s_bpm_hi = 0; s_creset = false; }
                float bpm = (float)pc_clk_bpm(&s_clk);
                if (bpm < s_bpm_lo) s_bpm_lo = bpm;
                if (bpm > s_bpm_hi) s_bpm_hi = bpm;
                if (s_last_ext_pos) {
                    double e = fabs((double)(b.pos - s_last_ext_pos) * 1e6 / INST_RATE - beat_us) / 250.0;
                    s_chist[e < 40 ? (int)e : 40]++;
                }
                s_last_ext_pos = b.pos;
            }
        }
    }
    inst_metro_t st = { s_metro.running, ext, locked, ext ? (float)pc_clk_bpm(&s_clk) : fixed ? (float)(60.0 * INST_RATE / s_lp.beat) : (float)s_m_bpm,
                        (float)pc_clk_bpm(&s_clk), s_metro.bpb, s_metro.idx, fixed };
    *(inst_metro_t *)&s_mst = st;
}

/* spectrum tap: written by the audio task while on, read by the LVGL task (a torn block only blurs one frame) */
static int16_t *s_tap;
static volatile uint32_t s_tap_w;
static volatile bool s_tap_on;

void inst_engine_tap(bool on)
{
    if (on && !s_tap) s_tap = heap_caps_calloc(INST_TAP_N, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    s_tap_on = on && s_tap;
}

int inst_engine_tap_read(float *mono, int n)
{
    if (!s_tap_on || n > INST_TAP_N) return 0;
    uint32_t w = s_tap_w - (uint32_t)n;
    for (int i = 0; i < n; i++) mono[i] = s_tap[(w + (uint32_t)i) & (INST_TAP_N - 1)] * (1.0f / 32768.0f);
    return n;
}

static void audio_task(void *arg)
{
    (void)arg;
    float g = 0;
    int blocks = 0, warm = 0;
    uint64_t busy = 0, ws = 0;
    uint32_t bmax = 0, wn = 0, wm = 0;
    int64_t win = esp_timer_get_time();
    for (;;) {
        int64_t t0 = esp_timer_get_time();
        if (s_stop) {
            pc_lp_all_off(&s_lp, (1 << PC_LP_TRACKS) - 1, NULL, NULL);
            pc_instr_all_off(&s_r);
            pc_sampler_kill(&s_samp);
            memset(s_out, 0, INST_BLOCK * 4);
            for (int i = 0; i < 4; i++) bsp_audio_write(s_out, INST_BLOCK, NULL, 20);
            bsp_audio_mute(true);
            s_stopped = true;
            vTaskDelete(NULL);
        }
        apply_controls();
        int64_t until = t0 + INST_BLOCK * 1000000LL / INST_RATE / 2;   /* due before mid-block */
        bool none = drain(&s_ui_q, until, t0, &ws, &wn, &wm, false, true);
        int64_t t_ui = esp_timer_get_time();
        drain(&s_ble_q, until, t0, &ws, &wn, &wm, true, none);
        int64_t t_ble = esp_timer_get_time();
        metro_block(t0);
        int64_t t_me = esp_timer_get_time();
        loop_block(t0);
        int64_t ta = esp_timer_get_time();
        if (s_chip_ok) s_r.chip->render(s_tmp, INST_BLOCK);
        else memset(s_tmp, 0, INST_BLOCK * 4);
        for (int i = 0; i < INST_BLOCK * 2; i++) s_mix[i] = s_tmp[i];
        int64_t tb = esp_timer_get_time();
        pc_sampler_render(&s_samp, s_mix, INST_BLOCK);
        if (s_tap_on) {                       /* the screen's spectrum: the music before the volume, mono, PSRAM */
            uint32_t w = s_tap_w;
            for (int f = 0; f < INST_BLOCK; f++) {
                int32_t x = (s_mix[2 * f] + s_mix[2 * f + 1]) >> 1;
                s_tap[(w + (uint32_t)f) & (INST_TAP_N - 1)] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            }
            s_tap_w = w + INST_BLOCK;
        }
        int64_t tc = esp_timer_get_time();
        /* gain: volume curve x CC7 (squared, GM), ramped over the block (no zipper noise); the click after it
         * takes the volume only */
#ifdef BSP_DAC_CS43131   /* DACFS: the DAC's volume register is the volume (inst_engine_set_volume), the digital stage is unity */
        float vg = pc_cs_digital_gain(s_vol), cc = s_r.volume / 127.0f, target = vg * cc * cc;
#else
        float vg = pc_volume_gain(s_vol), cc = s_r.volume / 127.0f, target = vg * cc * cc;
#endif
        float step = (target - g) / INST_BLOCK;
        int peak = s_peak;
        for (int f = 0; f < INST_BLOCK; f++) {
            g += step;
            int32_t gq = (int32_t)(g * 32768.0f);
            for (int c = 0; c < 2; c++) {
                int32_t x = s_mix[2 * f + c];
                int a = x < 0 ? -x : x;
                if (a > peak) peak = a;
                s_mix[2 * f + c] = (int32_t)((int64_t)x * gq >> 15);
            }
        }
        g = target;
        s_peak = peak;
        pc_metro_render(&s_metro, s_mix, INST_BLOCK, s_pos, s_click ? (int32_t)(vg * 32768.0f) : 0);
        for (int i = 0; i < INST_BLOCK * 2; i++) {
            int32_t x = s_mix[i];
            s_out[i] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
        }
        int64_t t1 = esp_timer_get_time();
        uint32_t dt = (uint32_t)(t1 - t0);
        busy += dt;
        if (dt > bmax) bmax = dt;
        if (dt > s_st.worst_us) { s_st.worst_us = dt; s_st.worst_ev[0] = (uint16_t)(t_ui - t0); s_st.worst_ev[1] = (uint16_t)(t_ble - t_ui); s_st.worst_ev[2] = (uint16_t)(t_me - t_ble); s_st.worst_ev[3] = (uint16_t)(ta - t_me); s_st.worst_part[0] = (uint16_t)(ta - t0); s_st.worst_part[1] = (uint16_t)(tb - ta); s_st.worst_part[2] = (uint16_t)(tc - tb); s_st.worst_part[3] = (uint16_t)(t1 - tc); }
        if (dt > 500) s_st.slow[dt > 1000 ? 2 : dt > 750 ? 1 : 0]++;
        s_st.blocks = ++blocks;
        if (t1 - win >= 1000000) {
            s_st.cpu_pct = 100.0f * (float)busy / (float)(t1 - win);
            s_st.block_max_us = bmax;
            s_st.wait_avg_us = wn ? (uint32_t)(ws / wn) : 0;
            s_st.wait_max_us = wm;
            busy = ws = 0; bmax = wn = wm = 0;
            win = t1;
        }
        bsp_audio_write(s_out, INST_BLOCK, NULL, portMAX_DELAY);
        s_pos += INST_BLOCK;
        if (warm >= 4) pc_aclock_mark(&s_ac, s_pos, esp_timer_get_time());   /* the DMA queue is full: writes pace */
        if (warm < 4 && ++warm == 4) { s_under0 = bsp_audio_underruns(); bsp_audio_mute(false); }   /* real data flows: XSMT up */
    }
}

int inst_engine_start(const pc_instr_cfg_t *c, int chip)
{
    pc_ring_init(&s_ble_q, s_ble_mem, sizeof s_ble_mem);
    pc_ring_init(&s_ui_q, s_ui_mem, sizeof s_ui_mem);
    pc_ring_init(&s_note_q, s_note_mem, sizeof s_note_mem);
    pc_ring_init(&s_beat_q, s_beat_mem, sizeof s_beat_mem);
    pc_metro_init(&s_metro, INST_RATE);
    pc_clk_reset(&s_clk);
    pc_aclock_init(&s_ac, INST_RATE, DAC_LATENCY_US);
    pc_sampler_init(&s_samp, INST_RATE);
    s_m_bpm = c->bpm;
    s_m_bpb = c->bpb;
    pc_metro_set_bpm(&s_metro, c->bpm);
    pc_metro_set_bpb(&s_metro, c->bpb);
    s_clk_ext = c->clock_ext;
    s_click = c->click;
    s_layer = c->layer;
    pc_midits_reset(&s_ts);
    pc_instr_init(&s_r, NULL, c->map, c->channel);
    s_want_map = c->map;
    s_want_ch = c->channel;
    s_vol = c->volume;
    bsp_audio_set_volume(s_vol);
    s_stable = c->stable;
    s_want_chip = inst_chip(chip) ? chip : 0;
    s_reset_ok[s_want_chip] = inst_chip(s_want_chip)->reset(INST_RATE) == 0;
    pc_lp_ev_t *lpool = heap_caps_malloc(LOOP_POOL * sizeof(pc_lp_ev_t), MALLOC_CAP_SPIRAM);
    uint16_t *lheads = heap_caps_malloc(LOOP_HEADS * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    s_loop_q = xQueueCreate(16, 2);
    if (!lpool || !lheads || !s_loop_q) return -1;
    pc_lp_init(&s_lp, lpool, LOOP_POOL, lheads, LOOP_HEADS);
    s_loop_bars = c->loop_bars;
    s_loop_quant = c->loop_q;
    s_tmp = heap_caps_calloc(INST_BLOCK * 2, sizeof(int16_t), MALLOC_CAP_INTERNAL);
    s_out = heap_caps_calloc(INST_BLOCK * 2, sizeof(int16_t), MALLOC_CAP_INTERNAL);
    s_mix = heap_caps_calloc(INST_BLOCK * 2, sizeof(int32_t), MALLOC_CAP_INTERNAL);
    if (!s_tmp || !s_out || !s_mix) return -1;
    bsp_audio_dac_config(cfg_int("dac_hpf", 1), cfg_int("dac_fs", 0), cfg_int("dac_amute", 1));   /* the user's DAC options here too (FakePod; no-op elsewhere) */
    esp_err_t e = bsp_audio_init_ex(INST_RATE, 3, INST_BLOCK);   /* muted until real blocks flow */
    printf("INST audio i2s=%s 3x%d frames\n", esp_err_to_name(e), INST_BLOCK);
    if (e != ESP_OK) return -1;
    /* internal stack: GME render runs on it at high priority */
    return xTaskCreatePinnedToCore(audio_task, "inst_audio", 6144, NULL, 22, NULL, 1) == pdPASS ? 0 : -1;
}

/* ---- looper API ---- */
void inst_engine_loop(int op, int arg)
{
    struct { int8_t op, arg; } c = { (int8_t)op, (int8_t)arg };
    if (s_loop_q && xQueueSend(s_loop_q, &c, 0) == pdTRUE) __atomic_add_fetch(&s_loop_posted, 1, __ATOMIC_RELEASE);
}
void inst_engine_loop_state(inst_loop_t *s) { *s = s_lst; }
int inst_engine_loop_msg(void) { return __atomic_exchange_n(&s_loop_msg, 0, __ATOMIC_ACQ_REL); }
pc_looper_t *inst_engine_loop_park(bool park)
{
    s_loop_park = park;
    for (int i = 0; i < 100 && s_loop_parked != park && !s_stopped; i++) vTaskDelay(pdMS_TO_TICKS(2));
    return s_loop_parked == park ? &s_lp : NULL;
}
pc_looper_t *inst_engine_looper(void) { return &s_lp; }

/* BLE note-ons: recorded spacing vs the sender's timestamps (recording), and the replay's spacing (block start
 * it was played in, measured in loop_emit) vs the same timestamps (end to end); ms */
static int emit_err(uint32_t frame, int32_t *err)
{
    for (int i = 0; i < s_nlemit; i++) if (s_lemit[i].frame == frame) { *err = s_lemit[i].err; return 1; }
    return 0;
}
static void sortf(float *v, int n) { for (int a = 1; a < n; a++) { float x = v[a]; int b = a; while (b && v[b - 1] > x) { v[b] = v[b - 1]; b--; } v[b] = x; } }
void inst_engine_loop_timing(inst_loop_timing_t *t)
{
    static float e1[LST], e2[LST];
    int k1 = 0, k2 = 0;
    double early = 0, early_max = 0;
    memset(t, 0, sizeof *t);
    for (int i = 0; i < s_nlemit; i++) { early += -s_lemit[i].err; if (-s_lemit[i].err > early_max) early_max = -s_lemit[i].err; }
    for (int i = 1; i < s_nlrec; i++) {
        int32_t df = (int32_t)(s_lrec[i].frame - s_lrec[i - 1].frame);
        if (df <= 0) continue;                              /* across the seam */
        double want = (double)((s_lrec[i].ts - s_lrec[i - 1].ts) & 0x1FFF);
        e1[k1++] = (float)fabs(df * 1000.0 / INST_RATE - want);
        int32_t ea, eb;
        if (emit_err(s_lrec[i - 1].frame, &ea) && emit_err(s_lrec[i].frame, &eb))
            e2[k2++] = (float)fabs((df + eb - ea) * 1000.0 / INST_RATE - want);
    }
    sortf(e1, k1);
    sortf(e2, k2);
    t->rec_n = k1;
    t->play_n = k2;
    t->emitted = s_nlemit;
    if (k1) { t->rec_p50 = e1[k1 / 2]; t->rec_p95 = e1[k1 * 95 / 100]; t->rec_max = e1[k1 - 1]; }
    if (k2) { t->play_p50 = e2[k2 / 2]; t->play_p95 = e2[k2 * 95 / 100]; t->play_max = e2[k2 - 1]; }
    if (s_nlemit) { t->early_avg = (float)(early / s_nlemit * 1000.0 / INST_RATE); t->early_max = (float)(early_max * 1000.0 / INST_RATE); }
}
