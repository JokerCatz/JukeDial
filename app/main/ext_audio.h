// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_extdesc.h"
#include "pc/pc_extstream.h"
#include "pc/pc_extuac.h"
/* 外接模式 T5: the sound card's audio glue, compiled by the firmware AND the simulator (spec 2026-11-15-external-mode.md section 3). The TinyUSB side (ext_uac.c: the class
 * requests, the pump task that moves the endpoint's data) and the simulator's fake host (sim/fake_ext_uac.c) call the same functions; everything about audio_svc is here.
 *
 * THE AUDIO PATH (AUDIO SAFETY): USB endpoint FIFO -> ext_uac.c pump (tud_audio_read) -> ext_audio_feed -> pc_extstream_write (the PSRAM ring) ... audio task:
 * audio_svc.c's mixer branch -> pc_extpath_render (ring -> float x the feature unit's gain <= 1, silence until the limiter is in) -> pc_extpath_post (audio_svc_set_post:
 * the fixed limiter chain + a final clamp) -> audio_svc.c, unchanged: taps, toy sounds (before the gain), pc_rv_block with the sink snapshot (no sink in this mode: s_sink is
 * NULL, never unity) x the device volume -> I2S. The stream has no other way in: nothing here writes to the I2S, the codec or a volume register.
 * Order at the mode start (ext_audio_start): audio_svc_start, audio_svc_set_volume(min(saved, 20)), the limiter chain installed with audio_svc_set_post, and only then may a mixer start. */
void ext_audio_start(void);                          /* once, at the mode start, before the USB stack comes up; safe to call again */
bool ext_audio_started(void);
pc_extuac_t *ext_audio_uac(void);                    /* the class requests' state (ext_uac.c SETs, the audio task reads the gain) */
void ext_audio_set_alts(const pc_extdesc_info_t *info);   /* the alternate settings' bit depths (alt n = info->alt_bits[n-1]) */
/* the pump task / the simulator: */
void ext_audio_event(int alt, uint32_t rate);        /* any task: the host chose alt (0 = stop) at rate (the clock source's current rate); the pump applies it at its next turn */
void ext_audio_pump(void);                           /* the pump's turn: applies a pending event (format, ring flush, feedback restart), starts or re-starts the mixer when needed */
bool ext_audio_event_pending(void);              /* an alt / rate event the pump has not applied yet */
uint32_t ext_audio_feed(const uint8_t *data, uint32_t bytes);   /* the pump: endpoint data into the ring (whole frames); returns the bytes taken */
uint32_t ext_audio_fb(uint64_t now_us, uint32_t extra_frames);  /* the pump, about every ms: the feedback value to send (16.16); extra_frames = what still sits in the endpoint FIFO */
const pc_extstream_t *ext_audio_stream(void);          /* diagnostics only (ext_diag): the ring counters, read-only */
int ext_audio_frame_bytes(void);                     /* 2 * bytes per sample of the active alt, 0 = no stream */
typedef struct {
    int alt, bits;
    uint32_t rate;
    bool mixer;              /* the mixer was started at this rate */
    bool refused;            /* this rate cannot be output by this board (not a mixer rate, or the DAC clock read back wrong): the stream is silence */
    uint32_t fill_frames, target_frames;
    uint32_t underruns, overruns, drops, limited;
    uint32_t fb;
} ext_audio_status_t;
void ext_audio_status(ext_audio_status_t *out);
bool ext_audio_volume_step(int detents);             /* LVGL task: the knob = the device volume (pc_volume_step, volume_max) while the computer streams (alt != 0); false otherwise (the layer's own role) */
uint16_t ext_audio_drop_mask(void);                  /* pc_extdesc_cfg_t.drop_mask: rates this board could not read back, kept in RTC memory for the next entry into the mode */
const char *ext_audio_rate_str(uint32_t rate, char *buf, int n);   /* "44.1" "48" "352.8" */
