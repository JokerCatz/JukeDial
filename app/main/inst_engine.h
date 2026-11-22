// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_instr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/* Instrument audio engine (spec 6): one task on core 1, 48 kHz, 64-frame blocks, I2S 3 x 64 (4 ms).
 * Inputs are single-producer rings drained at each block start: BLE (Bluedroid's task) and UI (LVGL task).
 * Chip registers are written on the audio task only. */
#define INST_RATE 48000
#define INST_BLOCK 64

typedef struct { uint8_t voice, note, vel, pad; } inst_note_t;   /* pad: 1..8 when the note is a kit pad's, else 0 */
#define INST_VOICE_KIT 8           /* inst_note_t.voice of a kit (sampler) note */
#define INST_CH_PAD 16             /* inst_engine_post_ui: a pad tap, straight to the kit */
typedef struct {
    float cpu_pct;                 /* render share of the block time, last second */
    uint32_t block_max_us;         /* slowest block, last second */
    uint32_t underruns, blocks;
    uint32_t slow[3];              /* blocks over 500 / 750 / 1000 us (of 1333), since start */
    uint32_t worst_us;             /* slowest block since start (or the last reset) and where it went: events + */
    uint16_t worst_part[4];        /* metronome + looper, chip render, kit render, the rest (mix, click, gain) */
    uint16_t worst_ev[4];          /* the first part split: UI ring, BLE ring, metronome, looper */
    uint32_t deferred;             /* times a burst was cut at the event budget (the rest went to the next block) */
    uint32_t events;               /* MIDI events played */
    uint32_t by_type[PC_MIDI_STOP + 1];   /* the same per pc_midi_type_t */
    uint32_t wait_avg_us, wait_max_us;   /* arrival (or due time) -> played, last second */
    int peak;                      /* largest |sample| since inst_engine_peak_reset */
    bool chip_ok;
} inst_stats_t;

int  inst_engine_start(const pc_instr_cfg_t *c, int chip);   /* 0 ok */
void inst_engine_post_ble(const pc_midi_ev_t *ev, int n, int64_t arrival_us);   /* BLE task only */
void inst_engine_post_ui(const pc_midi_ev_t *ev);                               /* LVGL task only */
void inst_engine_ble_connected(bool on);   /* off: all notes off; on: timestamp mapping starts over */
void inst_engine_set_chip(int idx);        /* applied at the next block (all off first) */
void inst_engine_log_poll(void);   /* main task: prints what the audio task noted (chip changes) */
int  inst_engine_chip(void);
const pc_synth_t *inst_engine_synth(void);
void inst_engine_set_map(pc_map_t m);
void inst_engine_set_channel(int ch);      /* -1 omni */
void inst_engine_set_volume(int vol);      /* 0..100 */
void inst_engine_set_stable(bool on);
void inst_engine_all_off(void);
void inst_engine_stop(void);               /* all off + mute, waits for it (before a restart) */
bool inst_engine_note_pop(inst_note_t *n); /* LVGL task: note-ons for the screen */
/* spectrum tap (spec 18): the mix before the volume, mono, the last INST_TAP_N samples in PSRAM; costs the audio
 * task nothing while off */
#define INST_TAP_N 1024
void inst_engine_tap(bool on);
int  inst_engine_tap_read(float *mono, int n);   /* LVGL task: the newest n samples, -1..1; 0 while off */
int  inst_engine_last_note(void);
void inst_engine_stats(inst_stats_t *s);
void inst_engine_peak_reset(void);
void inst_engine_worst_reset(void);       /* inst_stats_t.worst_us */
/* ---- metronome (stage 3) ---- */
enum { INST_BEAT_TICK = 0, INST_BEAT_START, INST_BEAT_STOP };
typedef struct {
    int64_t t_us;                  /* local time the click reaches the DAC (estimate) */
    int64_t planned_us;            /* when the engine committed it */
    uint32_t idx;
    int32_t period_us;             /* beat length */
    uint8_t beat, accent, kind, ext;
} inst_beat_t;
typedef struct { bool running, ext, locked; float bpm, ext_bpm; int bpb; uint32_t beats; bool loop; } inst_metro_t;   /* loop: a loop sets the tempo */
void inst_engine_metro_run(int on);        /* 0 stop, 1 start, 2 toggle */
void inst_engine_set_bpm(int bpm);
void inst_engine_set_bpb(int bpb);
void inst_engine_set_clock_ext(bool ext);
void inst_engine_set_click(bool on);
void inst_engine_metro(inst_metro_t *m);
void inst_engine_set_beat_task(TaskHandle_t t);   /* notified for every beat posted */
bool inst_engine_beat_pop(inst_beat_t *b);        /* the beat task only */
/* external clock: beat-to-beat spacing error of the committed beats against the follower's tempo, 0.25 ms bins */
void inst_engine_clkstat(int *n, float *p50, float *p95, float *max, float *bpm_min, float *bpm_max);
/* ---- kit layer (stage 4) ---- */
struct inst_kit;
void inst_engine_set_kit(const struct inst_kit *k);   /* NULL: none; returns once the engine has let go of the old one */
void inst_engine_set_layer(pc_layer_t l);
int  inst_engine_kit_voices(void);
#define INST_KIT_VOICES_FM 8       /* kit polyphony while FM is the chip (spec 16: CPU) */
void inst_engine_set_kit_voices(int n);   /* 0: by chip (default), else 1..16 (console A/B) */

/* BLE note-on timing error (played spacing vs sender timestamps) in ms, since the last call; resets */
void inst_engine_jitter(int *n, float *p50, float *p95, float *p99, float *max);
/* ---- looper (stage 6): pc_looper on the audio task; commands go through a queue (any task) ---- */
enum { INST_LOOP_REC = 0,          /* the big button: arm / end the take / overdub on-off / play + overdub */
       INST_LOOP_PLAYSTOP, INST_LOOP_SEL, INST_LOOP_MUTE, INST_LOOP_CLEAR, INST_LOOP_UNDO,
       INST_LOOP_RESET,            /* everything gone */
       INST_LOOP_QUANT, INST_LOOP_BARS };
enum { INST_LOOP_MSG_NONE = 0, INST_LOOP_MSG_NOCLOCK, INST_LOOP_MSG_TOOLONG };
typedef struct {
    int state;                     /* pc_lp_state_t */
    bool rec, full, can_undo;
    int sel;
    uint8_t mute;
    uint32_t cnt[4];
    uint32_t len, pos;             /* samples; pos: in the loop (PLAY) or since the take began */
    int32_t countin;               /* beats left (COUNTIN) */
    uint32_t bar;
    uint32_t events, cap;
    int msg;
} inst_loop_t;
struct pc_looper;
void inst_engine_loop(int op, int arg);
void inst_engine_loop_state(inst_loop_t *s);
int  inst_engine_loop_msg(void);                  /* INST_LOOP_MSG_*, once */
/* files: park (stops it; a take is dropped) -> the looper is the caller's until unparked; NULL on time-out */
struct pc_looper *inst_engine_loop_park(bool park);
struct pc_looper *inst_engine_looper(void);       /* read-only use while STOPPED and not recording */
typedef struct {
    int rec_n, play_n, emitted;
    float rec_p50, rec_p95, rec_max;              /* recorded spacing vs sender timestamps, ms */
    float play_p50, play_p95, play_max;           /* replayed spacing (block starts) vs sender timestamps, ms */
    float early_avg, early_max;                   /* replay: block start before the exact position, ms */
} inst_loop_timing_t;
void inst_engine_loop_timing(inst_loop_timing_t *t);
