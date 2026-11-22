// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Metronome in the sample domain (instrument spec 7): beats sit on sample positions of the engine's output
 * stream, so the click is sample accurate. A beat is "committed" a little before it sounds (the engine plans
 * up to a horizon ahead) so the haptic / clock-out task can be told its time early; once committed it does
 * not move. Tempo changes apply from the next uncommitted beat.
 * The click is RP2350_player's looper_click: a 20 ms square burst under a linear fade, 1760 Hz on beat 1
 * (accent), 880 Hz on the others. */
typedef struct { int64_t pos; uint32_t idx; uint8_t beat, accent; } pc_beat_t;   /* beat: 0..bpb-1 in the bar */

typedef struct {
    uint32_t rate;
    int bpb;                     /* beats per bar, 1..16 (UI: 2..7) */
    double interval;             /* samples per beat */
    bool running, pending;
    pc_beat_t next;              /* committed, not sounded yet (pending) */
    double next_pos;             /* where the beat after the last committed one goes (internal tempo) */
    uint32_t idx;                /* beats committed since start */
    uint32_t c_left, c_total, c_half, c_phase;   /* the click being played */
} pc_metro_t;

void pc_metro_init(pc_metro_t *m, uint32_t rate);
void pc_metro_set_bpm(pc_metro_t *m, double bpm);         /* clamped 30..300 */
void pc_metro_set_bpb(pc_metro_t *m, int bpb);
void pc_metro_start(pc_metro_t *m, int64_t first_pos);    /* beat 0 (accent) at first_pos */
void pc_metro_stop(pc_metro_t *m);                        /* a pending beat is dropped; a click sounding ends */
/* internal tempo: commit the next beat if it falls before horizon (a beat already in the past goes to now);
 * 1 and *out when one was committed */
int  pc_metro_plan(pc_metro_t *m, int64_t now, int64_t horizon, pc_beat_t *out);
/* external tempo: commit beat k (bar position k % bpb) at pos (not before now); 0 when one is pending */
int  pc_metro_commit(pc_metro_t *m, int64_t now, int64_t pos, uint32_t k, pc_beat_t *out);
/* adds the click of a committed beat in [pos, pos + frames) and any click still ringing, to stereo int32 */
void pc_metro_render(pc_metro_t *m, int32_t *mix, int frames, int64_t pos, int32_t gain_q15);

/* Output clock: when a sample reaches the DAC. After each blocking I2S write returns at t_us with pos samples
 * written so far, sample pos plays at about t_us + latency (the DMA queue). A write can return late (the task
 * was held up), never early, so the offset is the smallest seen over the last two 1 s windows. */
typedef struct { uint32_t rate; int64_t lat_us, off_us, win_min, prev_min, win_t; bool have; } pc_aclock_t;
void    pc_aclock_init(pc_aclock_t *a, uint32_t rate, int64_t latency_us);
void    pc_aclock_mark(pc_aclock_t *a, int64_t pos, int64_t t_us);
int64_t pc_aclock_time(const pc_aclock_t *a, int64_t pos);   /* local us when sample pos is heard */
int64_t pc_aclock_pos(const pc_aclock_t *a, int64_t t_us);   /* the sample heard at t_us */
#ifdef __cplusplus
}
#endif
