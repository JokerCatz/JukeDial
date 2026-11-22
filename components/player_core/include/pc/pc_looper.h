// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_midi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Event looper (instrument stage 6, after RP2350_player src/looper: events, not audio). Time is the engine's
 * output stream in samples: loop position = (pos - start) mod len. Everything runs on the audio task; the
 * caller hands in where the player heard each live event ("heard": the sample at the DAC when it was played,
 * BLE timestamps mapped to local time first), so recording is stamped on arrival, not on the drain.
 *
 * Tracks are layers (not chip voices): 4 of them, each any MIDI channel / pad; playback goes back through the
 * same router as live input. The first take sets the length: a fixed number of bars, or free (ends at the bar
 * line after a tap). After it: overdub passes (punch in / out) into the selected track, mute, clear, and a
 * single-level undo of the last pass or clear. Quantise snaps note-ons at record time (1/8, 1/16), the
 * note-off moves with its note-on. Events live in a pool (PSRAM on the board) in per-128-sample buckets
 * (linked, sorted), so recording and playback are O(1) per event; nothing is freed until reset / load. */
#define PC_LP_TRACKS 4
#define PC_LP_BUCKET 128                     /* samples per bucket */
#define PC_LP_NIL 0xFFFFu
#define PC_LP_CHANNELS 17                    /* MIDI 0..15 + 16 = a kit pad */
#define PC_LP_MAX_BARS_FREE 16

typedef struct {
    uint32_t frame;                          /* loop position */
    uint16_t next;                           /* next in the bucket, PC_LP_NIL */
    uint16_t pass;                           /* the record pass (op) that made it */
    uint8_t type, ch, a, b;                  /* pc_midi_ev_t; bend: a | b << 8 */
    uint8_t track, flags;                    /* flags: PC_LP_F_* */
    uint16_t pad;
} pc_lp_ev_t;
enum { PC_LP_F_DEAD = 1, PC_LP_F_SKIP = 2 }; /* SKIP: quantised ahead of the playhead, skip it once */

typedef enum { PC_LP_EMPTY = 0, PC_LP_COUNTIN, PC_LP_TAKE, PC_LP_PLAY, PC_LP_STOP } pc_lp_state_t;
enum { PC_LP_UNDO_NONE = 0, PC_LP_UNDO_PASS, PC_LP_UNDO_CLEAR };
typedef void (*pc_lp_emit_fn)(void *ctx, const pc_midi_ev_t *ev);

typedef struct pc_looper {
    pc_lp_ev_t *ev;
    uint32_t cap, n;                         /* pool */
    uint16_t *head;
    uint32_t nhead;                          /* buckets: max length = nhead * PC_LP_BUCKET */
    pc_lp_state_t state;
    bool rec, full;
    int64_t start;                           /* stream sample of loop position 0 */
    uint32_t len;                            /* 0 while the first take's end is open */
    uint32_t bar, beat, grid;                /* samples; grid 0 = no quantise */
    int bars;                                /* first take: 0 = free */
    int64_t take_end;                        /* free take: set by the tap, 0 open */
    uint32_t max_frame;                      /* largest frame recorded in the take */
    uint32_t play_pos;                       /* loop position of the next block */
    uint32_t passes;                         /* wraps since play started */
    uint32_t emit_frame;                     /* loop position of the event being emitted (for the callback) */
    int sel;
    uint8_t mute;                            /* bit per track */
    uint16_t op;                             /* last op id (passes and clears) */
    uint16_t tclear[PC_LP_TRACKS];           /* events of a track with pass <= tclear are cleared */
    uint32_t cnt[PC_LP_TRACKS];              /* live events per track */
    uint8_t undo_kind;
    uint16_t undo_op, undo_tclear;
    int undo_track;
    uint32_t undo_cnt, undo_first;           /* clear: count before; pass: first pool index */
    uint8_t sounding[PC_LP_TRACKS][PC_LP_CHANNELS][16];   /* notes the playback turned on */
    uint16_t pedal[PC_LP_TRACKS];            /* channels whose sustain the playback holds down */
    uint8_t held[PC_LP_CHANNELS][16];        /* recording: note-ons without their off yet */
    int32_t qd[PC_LP_CHANNELS][128];         /* quantise shift of the open note */
    uint8_t htrack[PC_LP_CHANNELS][128];     /* recording: the track an open note-on went to (its off goes there) */
    uint8_t ptrack[16];                      /* recording: 1 + the track holding each channel's sustain down, 0 */
    uint32_t wipe;                           /* buckets [0, wipe) may still hold stale heads after a reset */
} pc_looper_t;

void pc_lp_init(pc_looper_t *lp, pc_lp_ev_t *pool, uint32_t cap, uint16_t *head, uint32_t nhead);
void pc_lp_reset(pc_looper_t *lp);           /* everything gone: EMPTY (bucket heads: lazily, see pc_lp_wipe) */
/* clears up to max stale bucket heads after a reset; returns how many are left (arm / load finish the rest) */
uint32_t pc_lp_wipe(pc_looper_t *lp, uint32_t max);
uint32_t pc_lp_max_len(const pc_looper_t *lp);
/* first take: count in until start, then record bars (0: free) of bar samples; quant 0 off, 1 = 1/8, 2 = 1/16 */
int  pc_lp_arm(pc_looper_t *lp, int64_t start, uint32_t bar, uint32_t beat, int bars, int quant);
void pc_lp_set_quant(pc_looper_t *lp, int quant);
/* the take ends at the first bar line not before now (a free take; a fixed one is shortened) */
void pc_lp_end_take(pc_looper_t *lp, int64_t now);
void pc_lp_overdub(pc_looper_t *lp, bool on, int64_t heard);   /* PLAY: punch in / out (held notes get an off) */
void pc_lp_record(pc_looper_t *lp, const pc_midi_ev_t *e, int64_t heard);
/* one output block [pos, pos + frames): state changes and the events due in it, in order */
void pc_lp_block(pc_looper_t *lp, int64_t pos, int frames, pc_lp_emit_fn emit, void *ctx);
void pc_lp_stop(pc_looper_t *lp, int64_t heard, pc_lp_emit_fn emit, void *ctx);   /* take / count-in: dropped */
void pc_lp_play(pc_looper_t *lp, int64_t start);                /* STOP -> PLAY, position 0 at start */
void pc_lp_select(pc_looper_t *lp, int track);
void pc_lp_mute(pc_looper_t *lp, int track, pc_lp_emit_fn emit, void *ctx);      /* toggles */
int  pc_lp_clear(pc_looper_t *lp, int track, pc_lp_emit_fn emit, void *ctx);     /* events cleared */
int  pc_lp_undo(pc_looper_t *lp, pc_lp_emit_fn emit, void *ctx);                 /* 1 when something was undone */
void pc_lp_all_off(pc_looper_t *lp, int track_mask, pc_lp_emit_fn emit, void *ctx);   /* what playback holds */
bool pc_lp_has_content(const pc_looper_t *lp);
/* the first downbeat at least min_ahead samples after now, from a beat at beat_pos (beat_idx since start) */
int64_t pc_lp_next_downbeat(int64_t now, int64_t beat_pos, uint32_t beat_idx, double interval, int bpb, double min_ahead);

/* files (/sdcard/_player/loops/NAME.jcl): header + live events; load leaves the looper STOPPED */
typedef struct { uint32_t rate, len; uint16_t bpm_x100; uint8_t bpb, bars, quant, mute; char chip[12]; } pc_lp_meta_t;
size_t pc_lp_save(const pc_looper_t *lp, const pc_lp_meta_t *m, uint8_t *out, size_t cap);   /* 0: no room */
size_t pc_lp_save_size(const pc_looper_t *lp);
int    pc_lp_load(pc_looper_t *lp, pc_lp_meta_t *m, const uint8_t *in, size_t n);            /* 0 ok */

/* ProTracker export: 4 rows a beat, "M.K." (4 channels) or "8CHN" when more notes overlap; muted tracks out.
 * Melodic notes use a square instrument, channel 10 / pads a noise one. */
typedef struct { int channels, rows, patterns, notes, dropped, folded; } pc_lp_mod_info_t;
size_t pc_lp_mod(const pc_looper_t *lp, double bpm, int bpb, uint8_t *out, size_t cap, pc_lp_mod_info_t *info);
#ifdef __cplusplus
}
#endif
