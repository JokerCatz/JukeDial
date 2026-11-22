// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_midi.h"

static int feed(pc_midi_parser_t *p, const uint8_t *b, int n, pc_midi_ev_t *ev, int max)
{
    int k = 0;
    for (int i = 0; i < n; i++)
        if (pc_midi_feed(p, b[i], &ev[k]) && k < max - 1) k++;
    return k;
}

static void test_note_running_status(void)
{
    pc_midi_parser_t p;
    pc_midi_reset(&p);
    pc_midi_ev_t ev[8];
    const uint8_t b[] = { 0x91, 60, 100, 64, 0, 0x81, 60, 10 };
    int n = feed(&p, b, sizeof b, ev, 8);
    CHECK_EQ_INT(n, 3);
    CHECK_EQ_INT(ev[0].type, PC_MIDI_NOTE_ON); CHECK_EQ_INT(ev[0].ch, 1); CHECK_EQ_INT(ev[0].a, 60); CHECK_EQ_INT(ev[0].b, 100);
    CHECK_EQ_INT(ev[1].type, PC_MIDI_NOTE_OFF); CHECK_EQ_INT(ev[1].a, 64);     /* running status, velocity 0 */
    CHECK_EQ_INT(ev[2].type, PC_MIDI_NOTE_OFF); CHECK_EQ_INT(ev[2].a, 60);
}

static void test_cc_bend_program(void)
{
    pc_midi_parser_t p;
    pc_midi_reset(&p);
    pc_midi_ev_t ev[8];
    const uint8_t b[] = { 0xB0, 7, 90, 64, 127, 0xE2, 0x00, 0x40, 0xE2, 0x7F, 0x7F, 0xE2, 0, 0, 0xC3, 5, 0xD0, 33 };
    int n = feed(&p, b, sizeof b, ev, 8);
    CHECK_EQ_INT(n, 6);
    CHECK_EQ_INT(ev[0].type, PC_MIDI_CC); CHECK_EQ_INT(ev[0].a, 7); CHECK_EQ_INT(ev[0].b, 90);
    CHECK_EQ_INT(ev[1].type, PC_MIDI_CC); CHECK_EQ_INT(ev[1].a, 64); CHECK_EQ_INT(ev[1].b, 127);
    CHECK_EQ_INT(ev[2].type, PC_MIDI_BEND); CHECK_EQ_INT(ev[2].ch, 2); CHECK_EQ_INT(ev[2].bend, 0);
    CHECK_EQ_INT(ev[3].bend, 8191);
    CHECK_EQ_INT(ev[4].bend, -8192);
    CHECK_EQ_INT(ev[5].type, PC_MIDI_PROGRAM); CHECK_EQ_INT(ev[5].ch, 3); CHECK_EQ_INT(ev[5].a, 5);
}

static void test_realtime_and_sysex(void)
{
    pc_midi_parser_t p;
    pc_midi_reset(&p);
    pc_midi_ev_t ev[8];
    /* clock in the middle of a note-on; a sysex is skipped; start/continue/stop */
    const uint8_t b[] = { 0x90, 60, 0xF8, 70, 0xF0, 1, 2, 3, 0xF7, 0xFA, 0xFB, 0xFC, 61, 1 };
    int n = feed(&p, b, sizeof b, ev, 8);
    CHECK_EQ_INT(n, 5);
    CHECK_EQ_INT(ev[0].type, PC_MIDI_CLOCK);
    CHECK_EQ_INT(ev[1].type, PC_MIDI_NOTE_ON); CHECK_EQ_INT(ev[1].a, 60); CHECK_EQ_INT(ev[1].b, 70);
    CHECK_EQ_INT(ev[2].type, PC_MIDI_START);
    CHECK_EQ_INT(ev[3].type, PC_MIDI_CONTINUE);
    CHECK_EQ_INT(ev[4].type, PC_MIDI_STOP);
    /* the sysex cleared running status: the trailing data bytes make nothing */
}

static void test_ble_packet(void)
{
    pc_midi_parser_t p;
    pc_midi_reset(&p);
    pc_midi_ev_t ev[8];
    /* header (ts high 0x05), ts 0x10 note-on, running status note (no ts), ts 0x12 note-off */
    const uint8_t pk[] = { 0x85, 0x90, 0x90, 60, 100, 62, 90, 0x92, 0x80, 60, 0 };
    int n = pc_blemidi_decode(&p, pk, sizeof pk, ev, 8);
    CHECK_EQ_INT(n, 3);
    CHECK_EQ_INT(ev[0].type, PC_MIDI_NOTE_ON); CHECK_EQ_INT(ev[0].ts, 5 << 7 | 0x10);
    CHECK_EQ_INT(ev[1].type, PC_MIDI_NOTE_ON); CHECK_EQ_INT(ev[1].a, 62); CHECK_EQ_INT(ev[1].ts, 5 << 7 | 0x10);
    CHECK_EQ_INT(ev[2].type, PC_MIDI_NOTE_OFF); CHECK_EQ_INT(ev[2].ts, 5 << 7 | 0x12);
    /* running status with a new timestamp: ts byte then data */
    const uint8_t pk2[] = { 0x80, 0x81, 0x90, 64, 1, 0x83, 65, 2 };
    n = pc_blemidi_decode(&p, pk2, sizeof pk2, ev, 8);
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_INT(ev[1].a, 65); CHECK_EQ_INT(ev[1].ts, 3);
    /* timestamp low wraps inside a packet: the high part goes up by one */
    const uint8_t pk3[] = { 0x81, 0xFE, 0xF8, 0x82, 0xF8 };
    n = pc_blemidi_decode(&p, pk3, sizeof pk3, ev, 8);
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_INT(ev[0].type, PC_MIDI_CLOCK); CHECK_EQ_INT(ev[0].ts, 1 << 7 | 0x7E);
    CHECK_EQ_INT(ev[1].ts, 2 << 7 | 0x02);
    /* not a BLE MIDI packet (header without bit 7) */
    const uint8_t bad[] = { 0x05, 0x90, 0x90, 60, 100 };
    CHECK_EQ_INT(pc_blemidi_decode(&p, bad, sizeof bad, ev, 8), 0);
    /* out has room for only one event */
    n = pc_blemidi_decode(&p, pk, sizeof pk, ev, 1);
    CHECK_EQ_INT(n, 1);
}

/* a full MTU-128 packet: 60 running-status events, half of them note-offs; decoded in chunks nothing is lost */
static void test_ble_packet_chunks(void)
{
    uint8_t pk[128];
    int n = 0;
    pk[n++] = 0x81;
    pk[n++] = 0x90; pk[n++] = 0x90; pk[n++] = 40; pk[n++] = 100;
    for (int i = 1; i < 60; i++) {
        if (i == 30) pk[n++] = 0x95;                  /* a new timestamp halfway (running status goes on) */
        pk[n++] = (uint8_t)(40 + i / 2);
        pk[n++] = i & 1 ? 0 : 100;                    /* odd: velocity 0 = note-off */
    }
    CHECK(n <= 125);                                   /* MTU 128 - 3 */
    pc_midi_ev_t all[128];
    for (int max = 1; max <= 64; max = max == 1 ? 7 : max == 7 ? 32 : 65) {
        pc_midi_parser_t p;
        pc_midi_reset(&p);
        pc_blemidi_cur_t c;
        pc_blemidi_begin(&c);
        int k = 0, got;
        while ((got = pc_blemidi_next(&p, &c, pk, n, all + k, max)) > 0) { CHECK(got <= max); k += got; }
        CHECK_EQ_INT(k, 60);
        int offs = 0;
        for (int i = 0; i < k; i++) offs += all[i].type == PC_MIDI_NOTE_OFF;
        CHECK_EQ_INT(offs, 30);
        CHECK_EQ_INT(all[58].type, PC_MIDI_NOTE_ON); CHECK_EQ_INT(all[58].a, 69);
        CHECK_EQ_INT(all[0].ts, 1 << 7 | 0x10);
        CHECK_EQ_INT(all[29].ts, 1 << 7 | 0x10);
        CHECK_EQ_INT(all[30].ts, 1 << 7 | 0x15);
        CHECK_EQ_INT(all[59].ts, 1 << 7 | 0x15);
    }
    pc_midi_parser_t p;
    pc_midi_reset(&p);
    pc_blemidi_cur_t c;
    pc_blemidi_begin(&c);
    const uint8_t bad[] = { 0x05, 0x90, 0x90, 60, 100 };
    CHECK_EQ_INT(pc_blemidi_next(&p, &c, bad, sizeof bad, all, 8), 0);
}

static void test_ble_sysex_across_packets(void)
{
    pc_midi_parser_t p;
    pc_midi_reset(&p);
    pc_midi_ev_t ev[8];
    const uint8_t a[] = { 0x80, 0x80, 0xF0, 1, 2, 3 };
    const uint8_t b[] = { 0x80, 4, 5, 0x81, 0xF7, 0x82, 0x90, 60, 1 };
    CHECK_EQ_INT(pc_blemidi_decode(&p, a, sizeof a, ev, 8), 0);
    int n = pc_blemidi_decode(&p, b, sizeof b, ev, 8);
    CHECK_EQ_INT(n, 1);
    CHECK_EQ_INT(ev[0].type, PC_MIDI_NOTE_ON); CHECK_EQ_INT(ev[0].ts, 2);
}

static void test_encode(void)
{
    /* the board's own messages out (clock out, echo): header + ts + bytes */
    uint8_t out[16];
    const uint8_t m[] = { 0x90, 60, 100 };
    int n = pc_blemidi_encode(0x1234 & 0x1FFF, m, 3, out, sizeof out);
    CHECK_EQ_INT(n, 5);
    CHECK_EQ_INT(out[0], 0x80 | ((0x1234 & 0x1FFF) >> 7));
    CHECK_EQ_INT(out[1], 0x80 | (0x1234 & 0x7F));
    CHECK_EQ_INT(out[2], 0x90);
    pc_midi_parser_t p;
    pc_midi_reset(&p);
    pc_midi_ev_t ev[2];
    CHECK_EQ_INT(pc_blemidi_decode(&p, out, n, ev, 2), 1);
    CHECK_EQ_INT(ev[0].a, 60);
    CHECK_EQ_INT(pc_blemidi_encode(0, m, 3, out, 4), 0);
}

static void test_note_name(void)
{
    char b[8];
    CHECK_STR(pc_midi_note_name(60, b), "C4");
    CHECK_STR(pc_midi_note_name(61, b), "C#4");
    CHECK_STR(pc_midi_note_name(21, b), "A0");
    CHECK_STR(pc_midi_note_name(0, b), "C-1");
    CHECK_STR(pc_midi_note_name(127, b), "G9");
    CHECK_STR(pc_midi_note_name(-1, b), "-");
}

int main(void)
{
    RUN(test_note_running_status);
    RUN(test_cc_bend_program);
    RUN(test_realtime_and_sysex);
    RUN(test_ble_packet);
    RUN(test_ble_packet_chunks);
    RUN(test_ble_sysex_across_packets);
    RUN(test_encode);
    RUN(test_note_name);
    return 0;
}
