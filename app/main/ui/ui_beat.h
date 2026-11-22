// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
/* The beat feed (shared piece, ideas G3): pc_beat (the onset / tempo / phase detector, pure, host-tested) fed with the spectrum the now-playing ring
 * already makes (audio_svc_spectrum: the same 1024-point FFT the viz page asks for; no second FFT, no touch of the audio path). One detector for
 * the whole device (its state is one PSRAM block), switched on by a page for as long as the page lives: ui_beat_on(owner) starts an LVGL timer
 * (20 Hz) that is deleted with `owner`, so no page can leave it running. The timer runs on the UI task (audio_svc_spectrum is a UI-task call), not on
 * core 1: its cost is the FFT (measured: console `beat` prints `us=` the average and `max=`) plus pc_beat_step (< 1 us on the board's scale).
 * Time: the detector's clock is the real elapsed time between its own steps (lv_tick), so a user needs no fixed rate; ui_beat_info extrapolates the
 * grid to the moment of the call.
 * Users (built for them): beat tap now; haptic metronome, BPM meter, clap control, Game of Life seeding later (they read ui_beat_info / ui_beat_onsets).
 * Console `beat`: "" dumps "APP beat on=<0|1> src=<spec|sim> bpm= conf= locked=<0|1> onsets= ticks= steps= us= max= lat= users=" ; "sim <bpm>" feeds a synthetic click
 * track (a click every 60000/bpm ms: the simulator's and the board's self test of the whole chain without music; 0 = the real spectrum again);
 * "latency <ms>" shifts the grid (the feed's delay; the default is per sink: wired 30 ms, Bluetooth 180 ms, a guess to measure; "latency auto" goes back to it); "reset" clears the detector. */
typedef struct { bool locked; float bpm, confidence; uint32_t to_next_ms, period_ms; } ui_beat_info_t;
void ui_beat_on(lv_obj_t *owner);          /* use the feed for as long as `owner` lives (up to 4 users share one detector; the first one starts it clean, the last one's end stops it) */
void ui_beat_hold(bool hold);              /* pause / resume the timer (no FFT while held; the detector keeps its state): a page that is paused, covered, screen off or in a rule that does not listen */
void ui_beat_info(ui_beat_info_t *out);    /* locked = a tempo with confidence; to_next_ms = from now to the next grid beat (> 0), period_ms the beat's length */
uint32_t ui_beat_onsets(void);             /* onsets seen since ui_beat_on (a counter: a user that wants every onset compares with its last value) */
void ui_beat_cmd(const char *arg);
