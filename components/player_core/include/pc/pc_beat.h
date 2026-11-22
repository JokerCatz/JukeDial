// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* The shared beat detector (ideas G3, pc_beat): onsets, a tempo and a beat phase out of the spectrum the now-playing ring already makes
 * (pc_spectrum_bars: 64 bars 0..1, log-spaced 40 Hz .. 16 kHz, dB-scaled). Pure C: no ESP, no LVGL, no malloc, one fixed-size state, no second FFT and
 * no touch of the audio path: a read-only consumer of bars somebody else computed. Users (built for them): 節拍點擊 (the beat game), later 觸感節拍,
 * BPM 計, 拍手控制 and the life game's seeding.
 *
 * pc_beat_step(st, bars, dt_ms): one frame of bars, dt_ms since the previous call (any rate from ~10 to 60 Hz; ui_beat calls it at 20 Hz). It returns an
 * event mask (the plain event stream): PC_BEAT_ONSET (a sound event happened in this frame: the flux of the low and mid bars rose over its adaptive
 * threshold, at most one per PC_BEAT_REFRACT_MS), PC_BEAT_TICK (the predicted beat grid ticked: only while locked). The state's public fields:
 *   bpm         60..180, 0 when there is no tempo (silence, noise, too few onsets)
 *   confidence  0..1: the share of the last 6 s of onsets that sit on the beat grid (+-PC_BEAT_TOL_MS), scaled down while there are few; locked = >= PC_BEAT_CONF_MIN
 *   now_ms      the detector's own clock (the sum of the dt_ms): every time below is on this clock
 * and the readers pc_beat_phase (0..1 since the last grid beat), pc_beat_next_ms (the clock time of the next grid beat), pc_beat_locked.
 * How: flux = the mean positive rise of bars 0..PC_BEAT_BANDS-1 (low and mid) since the previous frame; the threshold = 1.5 x the mean flux of the last
 * PC_BEAT_HIST frames + a floor; onsets go into a ring of PC_BEAT_ONSETS; every 500 ms the tempo is the lag (330..1000 ms) with the most onset pairs
 * one, two and three beats apart (an autocorrelation of the onset train, 10 ms bins), refined by the mean of the matching pairs; the grid's anchor is the
 * circular mean of the onsets' positions modulo the period. Known limits: a track that is all eighth notes may read as double its tempo at low BPM
 * (the range is 60..180 so only one octave fits); the time of an onset is the frame that crossed, +- one frame of dt, plus whatever latency the
 * feed has (the caller's `latency_ms` shifts the grid: pc_beat_latency). */
#define PC_BEAT_BARS      64
#define PC_BEAT_BANDS     40                  /* bars 0..39 (40 Hz .. ~3.4 kHz) feed the flux */
#define PC_BEAT_HIST      48                  /* flux frames of the adaptive threshold (~1.6 s at 30 Hz) */
#define PC_BEAT_ONSETS    40
#define PC_BEAT_REFRACT_MS 150
#define PC_BEAT_WINDOW_MS 6000
#define PC_BEAT_TOL_MS    45
#define PC_BEAT_CONF_MIN  0.6f
#define PC_BEAT_STABLE    3                   /* estimates (500 ms apart) in a row within 2 % before the lock */
#define PC_BEAT_BPM_MIN   60
#define PC_BEAT_BPM_MAX   180
enum { PC_BEAT_ONSET = 1, PC_BEAT_TICK = 2 };
typedef struct {
    float bpm, confidence;
    float onset_flux;          /* the flux of the latest onset (0..~1: how hard the hit was; 生命遊戲 seeding, 觸感節拍 strength) */
    uint32_t now_ms;
    /* private */
    float prev[PC_BEAT_BANDS], hist[PC_BEAT_HIST], hsum, period, phase_prev;
    uint8_t hn, hi, n_on, i_on, stable;
    uint32_t onset[PC_BEAT_ONSETS], last_onset, est_ms, anchor;
    bool have_onset, grid;
    int32_t latency;
} pc_beat_t;
void pc_beat_init(pc_beat_t *b);
uint32_t pc_beat_step(pc_beat_t *b, const float bars[PC_BEAT_BARS], uint32_t dt_ms);
bool pc_beat_locked(const pc_beat_t *b);                 /* a tempo with confidence >= PC_BEAT_CONF_MIN */
float pc_beat_phase(const pc_beat_t *b);                 /* 0..1 since the last grid beat (0 when not locked) */
uint32_t pc_beat_next_ms(const pc_beat_t *b);            /* now_ms scale: the next grid beat after now_ms (0 when not locked) */
void pc_beat_latency(pc_beat_t *b, int32_t ms);          /* the feed's delay: the grid is moved that much later (+) or earlier (-) */
