// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* The tuner's listening service (tools ring: 調音器; pitch maths in player_core pc_tuner.h). The microphone (bsp_mic: PDM at 16 kHz, I2S0) is read without a task: the page's
 * LVGL timer calls tuner_poll(), which takes what the DMA queue holds (timeout 0), keeps the newest 2048 samples and runs the detector once per 1024 new samples (64 ms).
 * Internal RAM while listening is the driver's only (6 DMA descriptors of 240 mono 16-bit frames = 2880 B of DMA buffer, their descriptors, the channel object: ~3.5 KB
 * estimated; the console line "APP tuner mic internal ..." prints the real numbers); the 4 KB sample ring and the 7 KB detector scratch are PSRAM and freed at stop.
 * No task, no stack. Stopped by the page on every exit (destroy, covered, screen off): tuner_stop() frees everything and is a no-op when not listening. LVGL task only. */
typedef struct {
    bool voiced;               /* a steady pitch (two analyses in a row within a semitone; held 0.4 s after the sound stops) */
    float hz;                  /* the median of the last three analyses */
    int midi;                  /* the nearest note for the reference (pc_note_from_hz) */
    float cents;               /* -50..+50 */
    float rms;                 /* the newest block's level (int16 units) */
    uint32_t seq;              /* counts analyses */
} tuner_reading_t;
bool tuner_start(void);                     /* mic on, buffers made; false: no mic or no memory (nothing left running) */
void tuner_stop(void);
bool tuner_listening(void);
bool tuner_poll(tuner_reading_t *r);        /* true: r holds a new analysis (seq changed); not listening: false */
void tuner_set_a4(int hz);                  /* 430..450 (clamped) */
int  tuner_a4(void);
void tuner_print(void);                     /* console `tuner`: "APP tuner on= a4= analyses= reads= samples= last_us= voiced= hz= note= cents= rms=" */
