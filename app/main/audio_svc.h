// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "pc/pc_backend.h"
#include "pc/pc_voice.h"
/* Audio on core 1 (spec 4.2): pulls a backend (or a mixer), applies volume and the post hook,
 * writes I2S in 10 ms blocks. Sources are owned by the service once handed over. */
/* FADED: a fade-out stop finished (or the source ended during it). Events only, for sources played with near_ms:
 * NEAR = about near_ms left (queue the next one now), NEXT = the queued source took over (the event's gen is its gen). */
typedef enum { AUD_IDLE, AUD_PLAYING, AUD_ENDED, AUD_ERROR, AUD_FADED, AUD_PAUSED, AUD_NEAR, AUD_NEXT, AUD_XFADE } aud_state_t;   /* AUD_XFADE: the next track starts fading in under this one (gen = the NEXT track's): a display notice only, the join still ends in AUD_NEXT */
typedef struct { aud_state_t state; uint32_t rate; int32_t pos_ms, len_ms; int track, tracks; uint32_t gen, pos_byte; } aud_status_t;   /* pos_byte: 0 unknown */
typedef struct { aud_state_t type; uint32_t gen; } aud_event_t;     /* AUD_ENDED, AUD_ERROR, AUD_FADED (AUD_NEAR, AUD_NEXT) */
typedef void (*aud_release_fn)(void *arg);                          /* closes the source; runs on the audio task */

void audio_svc_start(void);
/* Opens be on r (copied) at start_off, selects track when the backend has several.
 * release(release_arg) is called when the service is done with the source (also when open fails).
 * Returns the play generation (events carry it), or 0 when the command could not be queued. */
uint32_t audio_svc_play(const pc_backend_t *be, const pc_reader_t *r, uint32_t start_off, int track, aud_release_fn release, void *release_arg);
/* Library playback: gain = ReplayGain (linear, applied first, before the effects and the volume; 0 = 1.0);
 * near_ms > 0 asks for an AUD_NEAR event that much before the end, to queue the next track (gapless). */
typedef struct { float gain; int32_t near_ms; bool speed; } aud_opts_t;   /* speed: a library file may take the speed setting and the crossfade; radio, chip, alarm, toys, instruments never (live sources would drain or stall) */
uint32_t audio_svc_play_ex(const pc_backend_t *be, const pc_reader_t *r, uint32_t start_off, int track, const aud_opts_t *o,
                           aud_release_fn release, void *release_arg);
/* Opens be on r off the audio task and holds it; when the source of after_gen ends cleanly it carries on in the
 * same block (no silence, no ramp) if the rate matches, else after a rate switch (a short mute). Dropped when
 * another play / stop comes first. Returns the queued source's gen (its AUD_NEXT carries it), 0 if not queued. */
uint32_t audio_svc_queue_next(uint32_t after_gen, const pc_backend_t *be, const pc_reader_t *r, const aud_opts_t *o,
                              aud_release_fn release, void *release_arg);
/* Track B: speed playback with the pitch kept (percent, 50..200, 100 = off; the audio task uses it only for a file source without a halving stage at
 * <= 48 kHz) and the crossfade between tracks (ms, 0 = off; only into a next track that joins gapless). Any task; speed_svc.c decides what is asked
 * for (it is zero when the mode is too heavy). Both take effect from the next block. */
void audio_svc_set_speed(int pct);
void audio_svc_set_xfade(int ms);
void audio_svc_set_gain(uint32_t gen, float gain);   /* the source of gen (playing or queued) gets a new gain */
/* A mixer that renders the whole output itself (white noise). render gets up to PC_AUDIO_MAX_BLOCK (3840) frames per call: one 10 ms block at the
 * pipeline rate, which is the mixer's own rate except after a failed clock switch (then whatever the DAC kept); size any scratch by that.
 * rate must be pc_mixer_rate_ok (never halved, so it needs a clock row), else the play is refused. */
uint32_t audio_svc_play_mixer(int (*render)(float *stereo, int frames, void *u), void *u, uint32_t rate, aud_release_fn release, void *release_arg);
int  audio_svc_next_track(int track);       /* same source, another sub-track; 0 if queued */
void audio_svc_stop(bool fade_10s);
void audio_svc_pause(bool pause);
/* paused as commanded, in queue order: the last audio_svc_pause, cleared by a play / mixer / stop (they start or end a
 * source un-paused). Read by the UI so a page created over a paused source shows it paused, without waiting for the task. */
bool audio_svc_paused(void);
/* another output instead of I2S (Bluetooth headphones): called on the audio task with each 10 ms block of
 * interleaved stereo float at `rate` (after volume and fades, before any narrowing; full scale = +-1.0), never more than PC_SINK_MAX_BLOCK
 * frames per call (a longer block, the one made just before the pipeline was re-planned for the sink, comes in several calls); it may block,
 * which then paces the audio task. NULL = back to I2S. */
typedef void (*aud_sink_fn)(const float *lr, int frames, uint32_t rate);
void audio_svc_set_sink(aud_sink_fn fn);          /* spec 5.8: hold the source, output silence; false resumes */
void audio_svc_set_volume(int vol);
/* Remote volume (Bluetooth headphones with AVRCP absolute volume): while set, blocks that go to a sink are at unity gain (the
 * headset does the volume) and every audio_svc_set_volume value is also handed to fn (any task: fn must only post). Blocks to
 * I2S always get the device's volume, set or not. NULL ends it. */
typedef void (*aud_remote_vol_fn)(int vol);
void audio_svc_set_remote_volume(aud_remote_vol_fn fn);
int audio_svc_volume_last(void);                  /* the last audio_svc_set_volume value (an alarm ramp's current step, not only app_volume) */
/* BTVOL (volumes per output; only vol_out.c uses these). hold(cap): until release(), no value above cap becomes the device volume, whoever calls
 * audio_svc_set_volume (what plays now is lowered to cap if it is above); the newest value asked for is kept and release() applies it.
 * A second hold only lowers the ceiling. req(): the newest value asked for (= volume_last() without a hold). block_mark(): changes once per
 * audio block, before the block's sink snapshot: a change seen after audio_svc_set_sink() means every block from now on has the new sink. */
void audio_svc_volume_hold(int cap);
void audio_svc_volume_release(void);
bool audio_svc_volume_held(void);
int  audio_svc_volume_req(void);
uint32_t audio_svc_block_mark(void);
void audio_svc_status(aud_status_t *out);
/* any task: the live generation (the existing s_live_gen, read only): the gen of the play / mixer / queued next the service was last handed,
 * 0 after a stop or a fade. While it is set and differs from audio_svc_status().gen a source is still opening and its rate is not known yet
 * (hires T8c: no cover write then; pc_thumb_opening) */
uint32_t audio_svc_live_gen(void);
/* The wired DAC refused the last source for its rate (pc_dac_unsupported): the rate in Hz once, then 0 (read and cleared). The player shows the reason before it skips. */
uint32_t audio_svc_take_low_rate(void);
bool audio_svc_low_is_hard(void);       /* the refusal read by take_low_rate was the hard limit (dac_hard_rate), not an unsupported rate (FakePod) */
/* A thing about the wired DAC the user should be told once (FakePod): kind 1 = the PLL did not lock (plays from the crystal clock), 2 = the DAC did not answer (muted, reset),
 * 3 = serial-port errors at *arg Hz (one tier down). Read and cleared; 0 = nothing. */
uint8_t audio_svc_take_dac_note(uint32_t *arg);
/* The pipeline as the audio task planned it (hires T5): filled on the audio task at every open, re-plan and rate switch (counters every 160 ms),
 * copied under the service's lock. src_rate: the source's own rate (0: nothing opened); pipe_rate: after the halving stage = the DAC's or the
 * sink's input rate; out_rate / bclk / out_bits: the I2S channel (0 while it is released for a sink); hw_cap: the highest rate the DAC clock
 * took this boot (drops one tier on a refused or wrong clock); cap: the wired ceiling the current source was planned with (dac_max_rate); pipe_why:
 * pc_pipe_why_t of the last halving (native when none); sink: the plan is for a sink (Bluetooth, 48k); underruns_src: I2S underruns since the
 * current source started; wr_timeouts: failed I2S writes since boot. hires T7 (the 現況 page): fmt: the source's format name (backend `name`, "Synth" for a
 * mixer, a static string), NULL = no source; src_bits: its bit depth, 0 = lossy or unknown; kbps: its reader size * 8 / length_ms, 0 when either is unknown;
 * released: the I2S channel is released (a sink carries the sound, or a reinit is pending: the page says so when no sink is on). */
typedef struct {
    uint32_t src_rate, pipe_rate, out_rate, bclk, hw_cap, cap;
    uint8_t pipe_why, halvings, out_bits;
    bool sink;
    uint32_t underruns_src, wr_timeouts;
    uint32_t start_underruns_src;   /* night3 C: the underruns of the start gap (before the first data of the source reached the DMA: silence while the first block decodes), apart from underruns_src (steady) */
    const char *fmt;
    uint8_t src_bits;
    uint32_t kbps;
    bool released;
    uint32_t next_rate;     /* hires T10b fix 1: the queued gapless next track's raw rate (0: none queued), set when it is queued (CMD_QUEUE); the Bluetooth
                             * resampler's builder prepares its table before the join */
} aud_info_t;
void audio_svc_info(aud_info_t *out);
/* The wired ceiling (dac_max_rate, hires T6): any task; the value is a pc_dacmax_norm result. It applies at the next source open, never to the one
 * playing (its plan stays; audio_svc_info().cap is the one the playing source was planned with). Prints "APP audio cap=<hz> (next track)" while a
 * source is playing or paused, "APP audio cap=<hz>" otherwise. audio_svc_start reads pc_dacmax_norm(cfg_int("dac_max_rate", 0), BSP_DAC_RATE_DEFAULT). */
void audio_svc_set_cap(uint32_t hz);
/* BTAUDIO: a wired setting changed (dac_max_rate, dac_hard_rate, dac_slot24_from): any task; the audio task re-plans what plays and re-opens the output with a short mute (never a reboot) */
void audio_svc_wired_changed(void);
/* totals of the 10 s stats windows since the last call (and resets them): for the soak log */
typedef struct { uint32_t underruns, blk_max_us, windows; float cpu1; } aud_acc_t;
void audio_svc_take_acc(aud_acc_t *out);
QueueHandle_t audio_svc_events(void);
void audio_svc_spectrum(float bars[64]);    /* call from the UI task; floor -60 dB */
int audio_svc_wave(float *out, int n);      /* UI task: the newest n (<= 1024) mono samples, -1..1, newest last; returns how many (0: the tap ring is not allocated) */
bool audio_svc_want_stereo(bool on);        /* UI task: on allocates (first time, PSRAM 4 KB, kept) and starts the audio task writing a 1024-frame int16 L/R ring; returns true when the tap is on (allocation failed: false); off stops the writes */
int audio_svc_stereo(int16_t *lr, int frames);   /* UI task: the newest frames (<= 1024) interleaved L/R, oldest first, only those written since the tap went on; returns how many (0: off, no ring, or nothing written yet) */
int audio_svc_voices(pc_voices_t *out);     /* any task: copies the per-voice level snapshot of the chip or tracker file that is playing (the audio task writes it after every block); all levels 0 while paused; returns n, 0 = this source has no separate voices (MP3, radio, noise, nothing playing) */
void audio_svc_spectrum_db(float floor_db, float bars[64]);   /* the same with the bottom of the scale at floor_db (all 0 before the tap ring exists) */
typedef void (*aud_process_fn)(float *stereo, int frames, uint32_t rate, int src, void *u);   /* src: pc_src_t, or -1 when no source delivered frames this block (buf is silent; called anyway so an effect chain can finish fades, and whatever the hook leaves in buf on such a block is discarded: the audio task zeroes it, idle tails are never heard) */
void audio_svc_set_post(aud_process_fn fn, void *u);   /* effect chain; called on the audio task */
