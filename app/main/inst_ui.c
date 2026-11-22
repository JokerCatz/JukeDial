// SPDX-License-Identifier: LGPL-2.1-or-later
#include "inst_ui.h"
#include "i18n.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cfg.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "misc/lv_area_private.h"
#include "esp_timer.h"
#include "inst_beat.h"
#include "inst_ble.h"
#include "inst_chip.h"
#include "inst_engine.h"
#include "inst_fm.h"
#include "inst_kit.h"
#include "inst_loopfile.h"
#include "lowmem.h"
#include "pc/pc_looper.h"
#include "pc/pc_ledfx.h"
#include "pc/pc_ledring.h"
#include "pc/pc_lowmem.h"
#include "pc/pc_midi.h"
#include "pc/pc_spectrum.h"
#include "pc/pc_tempo.h"
#include "pc/pc_volume.h"
#include "ui/font.h"
#include "ui/inst_bars.h"
#include "ui/ledrim.h"
#include "ui/ui.h"
#include "ui/ui_kit.h"
#include "ui/ui_popring.h"
#include "ui/ui_qs.h"
#include "ui/inst_pads.h"

static pc_instr_cfg_t *s_cfg;
static int s_knob_chip = -1;                  /* the chip axis' pending chip, -1 none */
static int64_t s_knob_at, s_last_tick;
/* tap tempo (the metronome ring's Tap cell, the console): from the press times on the UI side (pc_tap); the engine only gets the BPM */
static pc_tap_t s_tap;
static int64_t s_tap_at;
static uint32_t s_beat_seq;
static inst_kit_info_t s_kit;                 /* the pads' notes: a pad press looks its note up here */

static uint32_t voice_rgb(int v) { return inst_voice_rgb(v); }

/* ---- the rim ring (spec 18): the host underlay (ledrim_fx, ui/ledrim.c) shows s_fx; this file ticks it ---- */
static int s_ring_group = 3, s_ring_div = 2;   /* segments per invalidated area; ring frames 30 Hz / div (console A/B) */
static pc_ledfx_t s_fx;
static bool s_still, s_taplive;      /* low internal RAM: no note effects, no spectrum */
static uint32_t s_fft_n, s_fft_us, s_fft_max;

static int vis(int seg16) { int v = (seg16 + 6) % PC_LED_N; return v < PC_FX_SEGS ? v : -1; }   /* pc_ledring index -> 0..11 */

/* spectrum: 15 Hz on the LVGL task (core 0), from the engine's PSRAM tap; never on the audio task */
static void ring_spectrum(void)
{
    static float *mono;
    static float bars[PC_SPECTRUM_BARS];
    if (!mono) mono = heap_caps_malloc(INST_TAP_N * sizeof(float), MALLOC_CAP_SPIRAM);
    if (!mono || inst_engine_tap_read(mono, INST_TAP_N) != INST_TAP_N) return;
    int64_t t0 = esp_timer_get_time();
    pc_spectrum_bars(mono, INST_TAP_N, INST_RATE, bars);
    pc_fx_spectrum(&s_fx, bars + 8, 52);   /* ~85 Hz .. ~11 kHz: outside it the ring would sit still */
    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    s_fft_n++;
    s_fft_us += us;
    if (us > s_fft_max) s_fft_max = us;
}

void inst_ui_save(void)
{
    char b[256];
    pc_instr_cfg_format(s_cfg, b, sizeof b);
    cfg_set_str("inst", b);                  /* debounced write (cfg_tick) */
}

pc_instr_cfg_t *inst_ui_cfg(void) { return s_cfg; }
static void save(void) { inst_ui_save(); }

/* ---- volume ---- */
void inst_ui_set_volume(int v)
{
    s_cfg->volume = v < 0 ? 0 : v > 100 ? 100 : v;
    inst_engine_set_volume(s_cfg->volume);
    save();
}

const char *inst_ui_kit_err(int e)
{
    switch (e) {
    case INST_KIT_E_NOTFOUND: return T(S_FILE_NOT_FOUND) /* 找不到檔案 */;
    case INST_KIT_E_TOOBIG: return T(S_KIT_TOO_BIG_8_MB_MAX) /* 套組太大（上限 8 MB） */;
    case INST_KIT_E_MEMORY: return T(S_OUT_OF_MEMORY) /* 記憶體不足 */;
    case INST_KIT_E_FORMAT: return T(S_UNSUPPORTED_WAV) /* 不支援的 WAV */;
    default: return T(S_KIT_IS_EMPTY) /* 套組是空的 */;
    }
}

void inst_ui_pick_chip(int i)
{
    const pc_synth_t *c = inst_chip(i);
    if (!c) return;
    s_knob_chip = -1;
    inst_engine_set_chip(i);
    snprintf(s_cfg->chip, sizeof s_cfg->chip, "%s", c->id);
    save();
    printf("INST ui chip=%s\n", c->id);
}

/* the chip axis: shown at once (the play page names it), applied once the steps rest (a chip reset per detent would click) */
void inst_ui_chip_step(int d)
{
    int n = inst_chip_count();
    if (n <= 0) return;
    if (s_knob_chip < 0) s_knob_chip = inst_engine_chip();
    s_knob_chip = ((s_knob_chip + d) % n + n) % n;
    s_knob_at = esp_timer_get_time();
}
int inst_ui_chip_shown(void) { return s_knob_chip >= 0 ? s_knob_chip : inst_engine_chip(); }

void inst_ui_set_clock_ext(bool ext)
{
    s_cfg->clock_ext = ext;
    inst_engine_set_clock_ext(ext);
    save();
}

/* ---- the metronome's settings (the page and its ring are in ui/scr_inst.c) ---- */
const char *inst_ui_metre_why(void)
{
    inst_metro_t m;
    inst_engine_metro(&m);
    return m.loop ? T(S_TEMPO_AND_BEATS_THE_LOOP_S) /* 速度與拍號跟著循環 */ : NULL;
}
const char *inst_ui_tempo_why(void)
{
    const char *loop = inst_ui_metre_why();
    if (loop) return loop;
    return s_cfg->clock_ext ? T(S_TEMPO_THE_EXTERNAL_CLOCK) /* 速度跟著外部時脈 */ : NULL;
}
void inst_ui_set_bpm(int bpm)
{
    s_cfg->bpm = bpm < PC_BPM_MIN ? PC_BPM_MIN : bpm > PC_BPM_MAX ? PC_BPM_MAX : bpm;
    inst_engine_set_bpm(s_cfg->bpm);
    save();
}
void inst_ui_set_bpb(int bpb)
{
    s_cfg->bpb = bpb < 2 ? 2 : bpb > 7 ? 7 : bpb;
    inst_engine_set_bpb(s_cfg->bpb);
    save();
}
int inst_ui_tap(void)
{
    if (inst_ui_tempo_why()) return -1;                     /* the clock / the loop sets the tempo */
    int64_t ms = esp_timer_get_time() / 1000;
    int bpm = pc_tap(&s_tap, ms);
    s_tap_at = ms;
    if (bpm) bpm = bpm < INST_BPM_LO ? INST_BPM_LO : bpm > INST_BPM_HI ? INST_BPM_HI : bpm;   /* the tempo cell's range (A-m4) */
    if (bpm) { inst_ui_set_bpm(bpm); printf("INST ui tap bpm=%d\n", bpm); }
    return bpm;
}
int inst_ui_taps(void) { return s_tap.n; }
void inst_ui_metro_toggle(void) { inst_engine_metro_run(2); }

/* ---- pads: the pads page's down/up (ui/inst_pads.c calls them), the console's pad tap; audio first: the flash is a level, then the note ---- */
static void pad_flash(int p)
{
    if (p < 0 || p >= PC_PADS) return;
    inst_pads_flash(p, voice_rgb(3 + p));
    pc_fx_mark(&s_fx, vis(pc_pad_segment(p)), voice_rgb(3 + p), 1.0f);
}
const inst_kit_info_t *inst_ui_kit(void) { inst_kit_info(&s_kit); return &s_kit; }
static void pad_note(int p, bool on)
{
    if (s_kit.note[p] < 0 && on) inst_kit_info(&s_kit);   /* a kit that became ready since the pads page last looked (inst_ui_kit) */
    if (s_kit.note[p] < 0) return;
    pc_midi_ev_t ev = { on ? PC_MIDI_NOTE_ON : PC_MIDI_NOTE_OFF, INST_CH_PAD, (uint8_t)s_kit.note[p], (uint8_t)(on ? 100 : 0), 0, 0 };
    inst_engine_post_ui(&ev);
}
void inst_ui_pad_down(int p)
{
    if (p < 0 || p >= PC_PADS) return;
    pad_flash(p);
    pad_note(p, true);
}
void inst_ui_pad_up(int p) { if (p >= 0 && p < PC_PADS) pad_note(p, false); }
void inst_ui_pad(int p) { inst_ui_pad_down(p); inst_ui_pad_up(p); }   /* a tap: down, then up */

void inst_ui_page(int p)
{
    static const ui_screen_t *const pg[] = { NULL, &scr_inst_pads, &scr_inst_metro, &scr_inst_set, &scr_inst_chips, &scr_inst_loop };
    if (p < 0 || p >= (int)(sizeof pg / sizeof pg[0])) return;
    ui_popring_close();                      /* the play page's ring too: the console's way back is a clean play page */
    ui_home();
    if (pg[p]) ui_push(pg[p], NULL);
    printf("INST ui page=%d\n", p);
}

void inst_ui_exit(void)
{
    printf("INST exit to player\n");
    inst_engine_stop();                      /* all notes off, DAC muted */
    cfg_save_now();
    esp_restart();                           /* the request was one shot: the player boots */
}

/* ---- the host (ui.h ui_host_t): the knob's own job is the instrument's volume (Q1: on every page), the screen never times out,
 * the LED ring is the underlay below every page ---- */
static int inst_volume(void) { return s_cfg->volume; }
static bool inst_knob(int d)
{
    inst_ui_set_volume(pc_volume_step(s_cfg->volume, d, 100));
    ui_volume_shown(inst_volume());
    return true;
}
static uint32_t inst_idle_ms(void) { return 0; }
static lv_obj_t *inst_underlay(lv_obj_t *scr) { return ledrim_fx_create(scr, &s_fx); }
static const ui_host_t k_inst_host = { .name = "inst", .knob_default = inst_knob, .volume = inst_volume, .idle_ms = inst_idle_ms,
                                       .cfg_writes = true, .haptics = true, .underlay = inst_underlay };

/* ---- the 30 Hz refresh ---- */
static void tick(lv_timer_t *t)
{
    (void)t;
    int64_t now = esp_timer_get_time();
    int ms = s_last_tick ? (int)((now - s_last_tick) / 1000) : 33;
    s_last_tick = now;
    if (s_knob_chip >= 0 && now - s_knob_at > 350000) {
        int k = s_knob_chip;
        s_knob_chip = -1;
        if (k != inst_engine_chip()) inst_ui_pick_chip(k);
    }

    inst_note_t n;
    bool any = false;
    bool still = lowmem_level() >= PC_LOWMEM_STILL;       /* low internal RAM: looks go first (beats, pads stay) */
    if (still != s_still) { s_still = still; if (still) pc_fx_set_mode(&s_fx, PC_FX_OFF); else pc_fx_set_mode(&s_fx, (pc_fx_mode_t)s_cfg->fx); }
    bool tap = !still && s_cfg->fx == PC_FX_SPECTRUM;
    if (tap != s_taplive) { s_taplive = tap; inst_engine_tap(tap); }
    while (inst_engine_note_pop(&n)) {
        if (n.pad) pad_flash(n.pad - 1);                   /* a kit pad's note: its pad and its segment */
        else pc_fx_note(&s_fx, n.note, n.vel, voice_rgb(n.voice));
        any = true;
    }
    /* metronome: one segment per beat (the beat task marks it when it is heard) */
    uint8_t beat, acc;
    uint32_t seq = inst_beat_seen(&beat, &acc);
    if (seq != s_beat_seq) {
        s_beat_seq = seq;
        inst_metro_t mm;
        inst_engine_metro(&mm);
        pc_fx_mark(&s_fx, vis(pc_led_beat_segment(beat, mm.bpb)), voice_rgb(acc ? 0 : 1), 1.0f);
        scr_inst_beat(beat, acc);                         /* the metronome page's dots */
    }
    static int fdiv;
    if (s_taplive && ++fdiv % 2 == 0) ring_spectrum();        /* 15 Hz */
    pc_fx_tick(&s_fx, ms);
    static int rdiv;
    if (++rdiv % s_ring_div == 0) ledrim_fx_update(s_ring_group);

    if (s_tap.n && now / 1000 - s_tap_at > 2000) pc_tap_reset(&s_tap);   /* a run ends 2 s after its last tap */
    if (ui_top() == &scr_inst_pads) inst_pads_tick(ms);       /* the pads fade only where they are seen */
    scr_inst_frame(any);                                  /* the pages: play (notes, chip, bars, BLE), pads, metronome, looper, the lists */
}

void inst_ui_knob(int detents) { ui_knob_inject(detents); }
void inst_ui_ring_tune(int group, int div)
{
    if (group >= 1 && group <= PC_FX_SEGS) s_ring_group = group;
    if (div >= 1 && div <= 4) s_ring_div = div;
    printf("INST ring group=%d div=%d\n", s_ring_group, s_ring_div);
}
void inst_ui_fx(int mode)
{
    if (mode < 0 || mode >= PC_FX_COUNT) return;
    s_cfg->fx = mode;
    if (!s_still) pc_fx_set_mode(&s_fx, (pc_fx_mode_t)mode);
}

/* frame time: ui.c counts the refresh cycles (ui_fps_read); here only their split into rendering, flush calls and waits for the panel */
static int64_t s_ev_t0;
static uint32_t s_fr_render, s_fr_flush, s_fr_wait;
static void refr_cb(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();
    switch (lv_event_get_code(e)) {
    case LV_EVENT_RENDER_START: case LV_EVENT_FLUSH_START: case LV_EVENT_FLUSH_WAIT_START: s_ev_t0 = now; break;
    case LV_EVENT_RENDER_READY: s_fr_render += (uint32_t)(now - s_ev_t0); break;
    case LV_EVENT_FLUSH_FINISH: s_fr_flush += (uint32_t)(now - s_ev_t0); s_ev_t0 = now; break;
    case LV_EVENT_FLUSH_WAIT_FINISH: s_fr_wait += (uint32_t)(now - s_ev_t0); s_ev_t0 = now; break;
    default: break;
    }
}
void inst_ui_stat(void)
{
    ui_frames_t f;
    ui_fps_read(&f);
    ledrim_fx_stat_t r;
    ledrim_fx_stat(&r);
    uint32_t n = f.n ? f.n : 1;
    printf("INST uistat frames=%lu frame_avg=%luus frame_max=%luus render=%lu flush=%lu wait=%lu areas=%lu.%02lu px=%lu ring_draws=%lu ring_avg=%luus fft=%lu fft_avg=%luus fft_max=%luus fx=%s still=%d lowmem=%d cur=%d\n",
           (unsigned long)f.n, (unsigned long)f.avg_us, (unsigned long)f.max_us, (unsigned long)(s_fr_render / n),
           (unsigned long)(s_fr_flush / n), (unsigned long)(s_fr_wait / n), (unsigned long)(f.areas_x100 / 100), (unsigned long)(f.areas_x100 % 100),
           (unsigned long)f.px_avg, (unsigned long)r.n, (unsigned long)r.avg_us,
           (unsigned long)s_fft_n, (unsigned long)(s_fft_n ? s_fft_us / s_fft_n : 0), (unsigned long)s_fft_max,
           pc_fx_name((pc_fx_mode_t)s_cfg->fx), s_still, lowmem_level(), scr_inst_cur());
    s_fr_render = s_fr_flush = s_fr_wait = 0;
    s_fft_n = s_fft_us = s_fft_max = 0;
}
void inst_ui_row(int r) { scr_inst_row(r); }
void inst_ui_metro(int what)
{
    if (what == 0) inst_ui_metro_toggle();
    else if (what == 1) inst_ui_tap();
    else if (what == 2) { if (!inst_ui_metre_why()) inst_ui_set_bpb(s_cfg->bpb >= 7 ? 2 : s_cfg->bpb + 1); }
    else if (what == 3) inst_ui_set_clock_ext(!s_cfg->clock_ext);
}
void inst_ui_loop_more(int k) { scr_inst_loop_more(k); }

void inst_ui_start(lv_display_t *disp, lv_indev_t *touch, pc_instr_cfg_t *cfg)
{
    s_cfg = cfg;
    if (disp) {
        static const lv_event_code_t ev[] = { LV_EVENT_RENDER_START, LV_EVENT_RENDER_READY, LV_EVENT_FLUSH_START,
                                              LV_EVENT_FLUSH_FINISH, LV_EVENT_FLUSH_WAIT_START, LV_EVENT_FLUSH_WAIT_FINISH };
        for (unsigned i = 0; i < sizeof ev / sizeof ev[0]; i++) lv_display_add_event_cb(disp, refr_cb, ev[i], NULL);
    }
    for (int k = 0; k < PC_PADS; k++) s_kit.note[k] = -1;
    pc_fx_init(&s_fx, (pc_fx_mode_t)s_cfg->fx);
    ui_start_host(disp, touch, &k_inst_host);   /* theme, haptics on touch, the knob, the shared volume arc, the LED ring below every page */
    lv_timer_create(tick, 33, NULL);            /* before the first page: it is the mode's, not the page's */
    ui_push(&scr_inst_play, NULL);
    printf("INST ui page=0\n");
}

/* ---- the UI half of the instrument's console (portable: the board's instrument.c asks it first, the simulator's console uses it as it is) ----
 * Everything here goes through the UI functions above and the engine API (inst_engine_*, inst_loop_*), which the simulator's fakes implement.
 * Console tests change the engine and the in-memory settings, never the saved cfg (the screen saves when it changes something itself). */
static void pad_async(void *arg) { inst_ui_pad((int)(intptr_t)arg); }
static void loop_more_async(void *arg) { inst_ui_loop_more((int)(intptr_t)arg); }   /* posted to the LVGL task: the console runs on its own */

static bool loop_cmd(const char *c)
{
    int a;
    char w[INST_LOOP_NAME];
    if (strncmp(c, "inst loop", 9)) return false;
    if (!strcmp(c, "inst loop rec")) inst_engine_loop(INST_LOOP_REC, 0);
    else if (!strcmp(c, "inst loop play") || !strcmp(c, "inst loop stop")) inst_engine_loop(INST_LOOP_PLAYSTOP, 0);
    else if (sscanf(c, "inst loop sel %d", &a) == 1) inst_engine_loop(INST_LOOP_SEL, a);
    else if (sscanf(c, "inst loop mute %d", &a) == 1) inst_engine_loop(INST_LOOP_MUTE, a);
    else if (sscanf(c, "inst loop clear %d", &a) == 1) inst_engine_loop(INST_LOOP_CLEAR, a);
    else if (!strcmp(c, "inst loop undo")) inst_engine_loop(INST_LOOP_UNDO, 0);
    else if (!strcmp(c, "inst loop reset")) inst_engine_loop(INST_LOOP_RESET, 0);
    else if (sscanf(c, "inst loop q %d", &a) == 1) inst_engine_loop(INST_LOOP_QUANT, a);
    else if (sscanf(c, "inst loop bars %d", &a) == 1) inst_engine_loop(INST_LOOP_BARS, a);
    else if (sscanf(c, "inst loop more %d", &a) == 1) ui_async(loop_more_async, (void *)(intptr_t)a);
    else if (!strcmp(c, "inst loop save")) { char n[INST_LOOP_NAME] = ""; int r = inst_loop_save(n); printf("INST loop saved rc=%d name=%s\n", r, n); }
    else if (!strncmp(c, "inst loop load ", 15) && sscanf(c + 15, "%23s", w) == 1) printf("INST loop loaded rc=%d\n", inst_loop_load(w));
    else if (!strncmp(c, "inst loop export ", 17) && sscanf(c + 17, "%23s", w) == 1) printf("INST loop exported rc=%d\n", inst_loop_export(w, NULL));
    else if (!strncmp(c, "inst loop rm ", 13) && sscanf(c + 13, "%23s", w) == 1) inst_loop_remove(w);
    else if (!strcmp(c, "inst loop ls")) {
        static char names[INST_LOOP_FILES][INST_LOOP_NAME];
        int n = inst_loop_list(names, INST_LOOP_FILES);
        for (int i = 0; i < n; i++) printf("INST loop file %s\n", names[i]);
        printf("INST loop files n=%d\n", n);
    }
    else if (!strcmp(c, "inst loop timing")) {
        inst_loop_timing_t t;
        inst_engine_loop_timing(&t);
        printf("INST loop timing rec n=%d err p50=%.2fms p95=%.2fms max=%.2fms | replay n=%d err p50=%.2fms p95=%.2fms max=%.2fms | "
               "emitted=%d early avg=%.2fms max=%.2fms\n", t.rec_n, t.rec_p50, t.rec_p95, t.rec_max, t.play_n, t.play_p50, t.play_p95,
               t.play_max, t.emitted, t.early_avg, t.early_max);
    }
    else if (!strcmp(c, "inst loop") || !strcmp(c, "inst loop state")) {
        inst_loop_t s;
        inst_engine_loop_state(&s);
        printf("INST loop state=%d rec=%d sel=%d mute=%x cnt=%lu/%lu/%lu/%lu len=%lu pos=%lu countin=%ld bar=%lu events=%lu/%lu full=%d undo=%d\n",
               s.state, s.rec, s.sel, s.mute, (unsigned long)s.cnt[0], (unsigned long)s.cnt[1], (unsigned long)s.cnt[2],
               (unsigned long)s.cnt[3], (unsigned long)s.len, (unsigned long)s.pos, (long)s.countin, (unsigned long)s.bar,
               (unsigned long)s.events, (unsigned long)s.cap, s.full, s.can_undo);
    }
    else return false;
    return true;
}

bool inst_cmd_ui(const char *c)
{
    int a, b;
    char w[16], kw[INST_KIT_NAME];
    /* a word argument (%s) is matched with its space first: "inst kit %47s" alone took "inst kits" as the kit "s" (batch B fix A-I1) */
    if (loop_cmd(c)) ;
    else if (!strcmp(c, "qs") || !strncmp(c, "qs ", 3)) ui_qs_cmd(c[2] ? c + 3 : "");   /* GS: the quick-settings page (its switches are grey in this boot mode) */
    else if (!strncmp(c, "lang ", 5) && pc_lang_parse(c + 5) >= 0) { i18n_set_lang((pc_lang_t)pc_lang_parse(c + 5)); printf("APP lang=%s\n", pc_lang_code(i18n_lang())); }   /* i18n B: 簡中 width pictures of the instrument pages */
    else if (!strcmp(c, "inst kits")) {                    /* moved from instrument.c (batch B fix): same lines */
        static char names[24][INST_KIT_NAME];
        int n = inst_kit_list(names, 24);
        for (int i = 0; i < n; i++) printf("INST kit %d %s\n", i, names[i]);
        printf("INST kits n=%d\n", n);
    }
    else if (!strcmp(c, "inst kitstat")) {
        int e, sk;
        size_t by;
        inst_kit_state_t st = inst_kit_state(&e, &by, &sk);
        inst_kit_info_t ki;
        inst_kit_info(&ki);
        printf("INST kitstat state=%d err=%d bytes=%u skipped=%d name='%s' voices=%d pads=", st, e, (unsigned)by, sk, ki.name, inst_engine_kit_voices());
        for (int p = 0; p < 8; p++) printf("%d:%s ", ki.note[p], ki.label[p]);
        printf("\n");
    }
    else if (!strcmp(c, "inst cfg")) printf("INST cfg '%s'\n", cfg_str("inst", ""));
    else if (!strcmp(c, "inst metro")) inst_ui_metro(0);
    else if (!strcmp(c, "inst tap")) inst_ui_metro(1);
    else if (sscanf(c, "inst bpm %d", &a) == 1) { s_cfg->bpm = a < 30 ? 30 : a > 300 ? 300 : a; inst_engine_set_bpm(s_cfg->bpm); }
    else if (!strcmp(c, "inst clock ext") || !strcmp(c, "inst clock int")) { s_cfg->clock_ext = c[11] == 'e'; inst_engine_set_clock_ext(s_cfg->clock_ext); }
    else if (sscanf(c, "inst pad %d", &a) == 1) ui_async(pad_async, (void *)(intptr_t)a);
    else if (!strcmp(c, "inst exit") || !strcmp(c, "player")) inst_ui_exit();
    else if (sscanf(c, "inst knob %d", &a) == 1) inst_ui_knob(a);
    else if (!strncmp(c, "inst kit ", 9) && sscanf(c + 9, "%47s", kw) == 1) { snprintf(s_cfg->kit, sizeof s_cfg->kit, "%s", strcmp(kw, "none") ? kw : ""); inst_kit_load(s_cfg->kit); }   /* moved from instrument.c (T10) */
    else if (sscanf(c, "inst row %d", &a) == 1) inst_ui_row(a);
    else if (sscanf(c, "inst page %d", &a) == 1) inst_ui_page(a);
    else if (!strncmp(c, "inst fx ", 8) && sscanf(c + 8, "%15s", w) == 1) inst_ui_fx(pc_fx_parse(w));
    else if (!strcmp(c, "inst uistat")) inst_ui_stat();
    else if (sscanf(c, "inst ring %d %d", &a, &b) == 2) inst_ui_ring_tune(a, b);
    else return false;
    return true;
}
