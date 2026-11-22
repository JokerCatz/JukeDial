// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* MIDI 1.0 byte parser (after RP2350_player src/midi/midi_parser.c) with CC, pitch bend and the realtime
 * transport messages, plus the BLE MIDI 1.0 packet framing (header, 13-bit ms timestamps). */
typedef enum {
    PC_MIDI_NONE = 0, PC_MIDI_NOTE_ON, PC_MIDI_NOTE_OFF, PC_MIDI_PROGRAM, PC_MIDI_CC, PC_MIDI_BEND,
    PC_MIDI_CLOCK, PC_MIDI_START, PC_MIDI_CONTINUE, PC_MIDI_STOP,
} pc_midi_type_t;

/* note on/off: a = note, b = velocity; CC: a = number, b = value; program: a; bend: -8192..8191 */
typedef struct { uint8_t type, ch, a, b; uint16_t ts; int16_t bend; } pc_midi_ev_t;

typedef struct { uint8_t rs, data[2], count, need; bool sysex; } pc_midi_parser_t;

void pc_midi_reset(pc_midi_parser_t *p);
/* true when this byte completes an event (*out filled, ts = 0) */
bool pc_midi_feed(pc_midi_parser_t *p, uint8_t byte, pc_midi_ev_t *out);

/* One BLE MIDI packet (a characteristic write). Running status and sysex carry over between packets in *p.
 * Returns the events written (at most max, the rest of the packet is dropped), each with its 13-bit timestamp;
 * 0 for a malformed header. */
int pc_blemidi_decode(pc_midi_parser_t *p, const uint8_t *d, int n, pc_midi_ev_t *out, int max);
/* The same packet in chunks (an MTU-128 packet can hold ~60 running-status events): begin, then next until it
 * returns 0. Nothing is dropped; each call gives at most max events. */
typedef struct { int i, hi, lo, last; bool prev_ts; } pc_blemidi_cur_t;
void pc_blemidi_begin(pc_blemidi_cur_t *c);
int  pc_blemidi_next(pc_midi_parser_t *p, pc_blemidi_cur_t *c, const uint8_t *d, int n, pc_midi_ev_t *out, int max);
/* One message as a BLE MIDI packet; bytes written, 0 when cap is too small */
int pc_blemidi_encode(uint16_t ts13, const uint8_t *msg, int n, uint8_t *out, int cap);

/* "C4" for 60, "C#4", "-" outside 0..127 */
const char *pc_midi_note_name(int note, char buf[8]);
#ifdef __cplusplus
}
#endif
