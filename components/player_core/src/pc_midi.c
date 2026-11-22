// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_midi.h"
#include <stdio.h>
#include <string.h>

void pc_midi_reset(pc_midi_parser_t *p) { memset(p, 0, sizeof *p); }

static uint8_t data_bytes(uint8_t st)
{
    switch (st & 0xF0) {
    case 0xC0: case 0xD0: return 1;
    default: return 2;
    }
}

bool pc_midi_feed(pc_midi_parser_t *p, uint8_t b, pc_midi_ev_t *out)
{
    if (b >= 0xF8) {                               /* realtime: anywhere, never touches running status */
        uint8_t t = b == 0xF8 ? PC_MIDI_CLOCK : b == 0xFA ? PC_MIDI_START : b == 0xFB ? PC_MIDI_CONTINUE
                  : b == 0xFC ? PC_MIDI_STOP : PC_MIDI_NONE;
        if (t == PC_MIDI_NONE) return false;
        memset(out, 0, sizeof *out);
        out->type = t;
        return true;
    }
    if (b == 0xF0) { p->sysex = true; p->rs = 0; p->count = 0; return false; }
    if (b == 0xF7) { p->sysex = false; return false; }
    if (p->sysex) return false;
    if (b >= 0xF1) { p->rs = 0; p->count = 0; return false; }   /* system common: data dropped (not used here) */
    if (b & 0x80) { p->rs = b; p->count = 0; p->need = data_bytes(b); return false; }
    if (!p->rs) return false;
    p->data[p->count++] = b;
    if (p->count < p->need) return false;
    p->count = 0;
    memset(out, 0, sizeof *out);
    out->ch = p->rs & 0x0F;
    out->a = p->data[0];
    out->b = p->need == 2 ? p->data[1] : 0;
    switch (p->rs & 0xF0) {
    case 0x90: out->type = out->b ? PC_MIDI_NOTE_ON : PC_MIDI_NOTE_OFF; return true;
    case 0x80: out->type = PC_MIDI_NOTE_OFF; return true;
    case 0xB0: out->type = PC_MIDI_CC; return true;
    case 0xC0: out->type = PC_MIDI_PROGRAM; return true;
    case 0xE0: out->type = PC_MIDI_BEND; out->bend = (int16_t)((out->b << 7 | out->a) - 8192); out->a = out->b = 0; return true;
    default: return false;                         /* aftertouch: framed, ignored */
    }
}

/* Every status byte (realtime and F7 included) is preceded by a timestamp byte; data bytes never have bit 7.
 * So a bit-7 byte is a timestamp unless the byte before it was one. */
void pc_blemidi_begin(pc_blemidi_cur_t *c) { memset(c, 0, sizeof *c); c->last = -1; }

int pc_blemidi_next(pc_midi_parser_t *p, pc_blemidi_cur_t *c, const uint8_t *d, int n, pc_midi_ev_t *out, int max)
{
    if (c->i == 0) {
        if (n < 1 || !(d[0] & 0x80)) { c->i = n > 0 ? n : 1; return 0; }
        c->hi = d[0] & 0x3F;
        c->i = 1;
    }
    int k = 0;
    while (c->i < n && k < max) {
        uint8_t b = d[c->i++];
        if ((b & 0x80) && !c->prev_ts) {
            c->lo = b & 0x7F;
            if (c->last >= 0 && c->lo < c->last) c->hi = (c->hi + 1) & 0x3F;
            c->last = c->lo;
            c->prev_ts = true;
            continue;
        }
        c->prev_ts = false;
        pc_midi_ev_t ev;
        if (pc_midi_feed(p, b, &ev)) {
            ev.ts = (uint16_t)(c->hi << 7 | c->lo);
            out[k++] = ev;
        }
    }
    return k;
}

int pc_blemidi_decode(pc_midi_parser_t *p, const uint8_t *d, int n, pc_midi_ev_t *out, int max)
{
    pc_blemidi_cur_t c;
    pc_blemidi_begin(&c);
    int k = pc_blemidi_next(p, &c, d, n, out, max);
    pc_midi_ev_t drop;
    while (pc_blemidi_next(p, &c, d, n, &drop, 1)) ;   /* the parser still sees the rest (running status) */
    return k;
}

int pc_blemidi_encode(uint16_t ts13, const uint8_t *msg, int n, uint8_t *out, int cap)
{
    if (n + 2 > cap) return 0;
    out[0] = (uint8_t)(0x80 | (ts13 >> 7 & 0x3F));
    out[1] = (uint8_t)(0x80 | (ts13 & 0x7F));
    memcpy(out + 2, msg, (size_t)n);
    return n + 2;
}

const char *pc_midi_note_name(int note, char buf[8])
{
    static const char *const N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0 || note > 127) { strcpy(buf, "-"); return buf; }
    snprintf(buf, 8, "%s%d", N[note % 12], note / 12 - 1);
    return buf;
}
