// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_midi.h"
#include "pc/pc_synth.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Instrument router: MIDI events -> the current chip (audio task only).
 * Map modes (RP2350_player midi_map_mode.h): Auto = the chip's own allocator, omni; Direct = channel n -> voice n,
 * mono (a sequencer that assigns voices itself; the channel filter does not apply); GM = per-channel program
 * picks a voice group (pc_gm_table.h). Channel 10 is percussion in every mode.
 * Also: CC7 volume (r->volume, applied by the mixer), CC64 sustain, CC120/123 all off (every channel: chips
 * cannot silence one channel), CC121 reset controllers, pitch bend (+-bend_range semitones). */
typedef enum { PC_MAP_AUTO = 0, PC_MAP_DIRECT, PC_MAP_GM, PC_MAP_COUNT } pc_map_t;

typedef struct {
    const pc_synth_t *chip;
    pc_map_t mode;
    int channel;                 /* -1 omni, else 0..15 */
    int bend_range;              /* semitones, 2 */
    int volume;                  /* CC7, 0..127 (100 at start) */
    bool pedal[16];
    uint8_t held[16][16];        /* notes kept sounding by the pedal (bit per note) */
    int16_t bend[16];            /* cents */
    int last_note;               /* last note-on, -1 */
} pc_instr_t;

void pc_instr_init(pc_instr_t *r, const pc_synth_t *chip, pc_map_t mode, int channel);
/* the voice a note-on landed on (percussion: the last voice), else -1 */
int  pc_instr_event(pc_instr_t *r, const pc_midi_ev_t *e);
void pc_instr_all_off(pc_instr_t *r);             /* chip silent, pedal-held notes and bends cleared */
void pc_instr_set_chip(pc_instr_t *r, const pc_synth_t *chip);   /* all off on the old one; caller resets the new */
void pc_instr_set_mode(pc_instr_t *r, pc_map_t mode);
void pc_instr_set_channel(pc_instr_t *r, int channel);
const char *pc_instr_map_name(pc_map_t m);         /* "auto" "direct" "gm" */

/* Layers (stage 4): where a note goes. Auto: channel 10 to the kit when one is loaded (whatever the channel
 * setting: drums keep working with the chip on channel 1), the rest to the chip. Chip / Kit / Both: every
 * note, the kit then obeys the channel setting like the chip. */
typedef enum { PC_LAYER_AUTO = 0, PC_LAYER_CHIP, PC_LAYER_KIT, PC_LAYER_BOTH, PC_LAYER_COUNT } pc_layer_t;
enum { PC_ROUTE_CHIP = 1, PC_ROUTE_KIT = 2 };
int pc_layer_route(pc_layer_t layer, int channel_setting, int ev_ch, bool kit_loaded);   /* PC_ROUTE_* bits */
const char *pc_layer_name(pc_layer_t l);           /* "auto" "chip" "kit" "both" */
/* Where each sounding note went: its note-off goes there too, even after a layer / channel change or a kit load.
 * pc_noteroute(e, now): a note-on stores and returns now; a note-off returns (and forgets) its note-on's
 * route, or now when none is open; other events: now. Notes outside 0..127: 0. Reset on all off. */
typedef struct { uint8_t r[16][128]; } pc_noteroute_t;
void pc_noteroute_reset(pc_noteroute_t *nr);
int  pc_noteroute(pc_noteroute_t *nr, const pc_midi_ev_t *e, int route_now);

/* Settings, one cfg value: "chip=nes map=auto ch=0 vol=40 timing=now knob=page kit=demo layer=auto clock=int
 * bpm=120 bpb=4 click=on beathap=on fm=0 lbars=2 lq=0 fx=pitch" (ch 0 = omni, 1..16; a space in the kit name is stored as '|') */
typedef struct {
    char chip[12];
    pc_map_t map;
    int channel;                 /* -1 omni, 0..15 */
    int volume;                  /* 0..100 */
    int stable;                  /* 1: timestamp scheduling (+8 ms) */
    int knob_volume;             /* 1: the knob is the volume on every page */
    char kit[48];                /* "" none, "demo", "folder/name" (an .sfz) */
    pc_layer_t layer;
    int clock_ext;               /* metronome follows MIDI clock in */
    int bpm, bpb;                /* 30..300, 2..7 */
    int click, beat_haptic;      /* metronome sound, metronome vibration */
    int fm;                      /* FM program every channel starts on, 0..127 */
    int loop_bars;               /* looper length: 1, 2, 4, 8 bars; 0 = free (ends on a tap) */
    int loop_q;                  /* looper quantise at record time: 0 off, 1 = 1/8, 2 = 1/16 */
    int fx;                      /* rim ring effect, pc_fx_mode_t (fx=pitch|rainbow|ripple|comet|spectrum|off) */
} pc_instr_cfg_t;
void pc_instr_cfg_default(pc_instr_cfg_t *c);
void pc_instr_cfg_parse(const char *s, pc_instr_cfg_t *c);   /* NULL / bad fields -> defaults */
void pc_instr_cfg_format(const pc_instr_cfg_t *c, char *out, int cap);
#ifdef __cplusplus
}
#endif
