// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_extstream.h"
#include "pc/pc_extuac.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 外接模式 T5: the whole audio path of the sound card up to audio_svc, as pure code (spec 2026-11-15-external-mode.md section 3). Two entry points, both on the audio task:
 *   pc_extpath_render  = the render callback of audio_svc_play_mixer: the ring -> float stereo times the feature unit's gain (<= 1), silence until the limiter is in place
 *   pc_extpath_post    = audio_svc_set_post's hook: the FIXED chain (the limiter, nothing else, the user cannot turn it off; the fx service is not running in this mode) and a final
 *                        clamp at the limiter's ceiling
 * Behind both stays audio_svc.c untouched: the device volume and pc_rv_block (no sink in this mode: s_sink is NULL, so a block never reaches the wired I2S at unity gain).
 * AUDIO SAFETY: render returns `frames` always (a return <= 0 would end the source), never produces a sample above the gain (<= 1) and while the limiter is not confirmed
 * running (installed, warmed, no fade in progress) it produces silence; the post hook clamps whatever the chain leaves at the ceiling even if the limiter were suspended. */
#define PC_EXTPATH_CEIL 0.8914f        /* a hair above -1 dB (0.89125), the limiter's default ceiling: the final clamp only ever cuts what the limiter did not */
#define PC_EXTPATH_GATE_BLOCKS 3       /* blocks of silence after the limiter reports ready (the chain's own fade is one block) */
#define PC_EXTPATH_LIM_RATE 384000     /* the limiter block is built for the highest rate once: its look-ahead line fits every lower one */

typedef struct pc_extpath pc_extpath_t;
/* alloc: PSRAM on the board. NULL when an allocation failed or the registry has no limiter. s and u must outlive it (the audio task reads them). */
pc_extpath_t *pc_extpath_create(void *(*alloc)(size_t), pc_extstream_t *s, const pc_extuac_t *u);
void pc_extpath_destroy(pc_extpath_t *p, void (*release)(void *));   /* host tests and the simulator */
int pc_extpath_render(float *stereo, int frames, void *p);
void pc_extpath_post(float *stereo, int frames, uint32_t rate, int src, void *p);
bool pc_extpath_ready(const pc_extpath_t *p);                         /* audio task: the limiter runs and the gate has passed */
uint32_t pc_extpath_limited(const pc_extpath_t *p);                   /* blocks the final clamp had to cut (0 while the limiter does its job) */

#ifdef __cplusplus
}
#endif
