// SPDX-License-Identifier: LGPL-2.1-or-later
/* AdPlug players against hostile files, group b (patch 0005): MAD / protracker core, MUS+IMS, MDI + composer core, KSM, D00, MKJ.
 * Every input is built here from a minimal header plus the offending field; the test must neither crash (ASan) nor loop nor leak. */
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc/pc_chipseq.h"
#include "pc_test.h"

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, unsigned v) { put16(p, v); put16(p + 2, v >> 16); }
static void be32(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* open (may refuse), play a bounded number of blocks, close: returns whether it opened */
static bool play(const uint8_t *d, uint32_t n, const char *ext)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, n);
    pc_reader_set_ext(&r, ext);
    void *h;
    if (pc_backend_opl.open(&h, &r, 0)) return false;
    float out[2 * 441];
    for (int i = 0; i < 40; i++) {
        int g = pc_backend_opl.render(h, out, 441);
        CHECK(g >= 0);
        if (g == 0) break;
    }
    pc_backend_opl.close(h);
    return true;
}

/* MAD+: 9 instruments, length / patterns / timer, nop x 32 rows x 9 channels of events, then the order list */
static uint32_t make_mad(uint8_t *b, unsigned length, unsigned nop, unsigned order0)
{
    memset(b, 0, 600);
    memcpy(b, "MAD+", 4);
    uint32_t p = 4 + 9 * 20 + 1;
    b[p++] = (uint8_t)length; b[p++] = (uint8_t)nop; b[p++] = 50;
    p += nop * 32 * 9;
    if (length) b[p++] = (uint8_t)order0;
    return p;
}

static void test_protrack_order_names_missing_pattern(void)
{
    uint8_t b[600];
    uint32_t n = make_mad(b, 1, 1, 5);             /* order entry 4, but only pattern 0 exists */
    play(b, n, ".mad");
}

static void test_protrack_order_jump_cycle(void)
{
    uint8_t b[600];
    uint32_t n = make_mad(b, 2, 1, 0x82);          /* order[0] = jump to 1 ... */
    b[n++] = 0x81;                                 /* ... order[1] = jump to 0 */
    play(b, n, ".mad");
}

static void test_protrack_zero_patterns_no_leak(void)
{
    uint8_t b[600];
    uint32_t n = make_mad(b, 1, 0, 1);             /* no patterns: the empty track / channel arrays were never freed */
    play(b, n, ".mad");
}

/* MUS: 70-byte header, then the MIDI-like data */
static uint32_t make_mus(uint8_t *b, const uint8_t *data, unsigned dn)
{
    memset(b, 0, 70);
    b[0] = 1; b[1] = 0;
    b[36] = 1; b[37] = 1;                          /* tick/beat, beat/measure */
    put32(b + 38, 1);                              /* total ticks */
    put32(b + 42, dn);                             /* data size */
    put32(b + 46, 1);                              /* commands */
    put16(b + 60, 100);                            /* tempo */
    memcpy(b + 70, data, dn);
    return 70 + dn;
}

static void test_mus_command_cut_off_by_end(void)
{
    uint8_t b[128];
    static const uint8_t d[2] = { 0x00, 0x90 };    /* delay 0, note-on whose two arguments are past the data */
    uint32_t n = make_mus(b, d, sizeof d);
    play(b, n, ".mus");
    static const uint8_t d2[3] = { 0x00, 0xF0, 0x7F };   /* tempo sysex cut off */
    n = make_mus(b, d2, sizeof d2);
    play(b, n, ".mus");
    static const uint8_t d3[2] = { 0xF8, 0xF8 };   /* only overflow delays */
    n = make_mus(b, d3, sizeof d3);
    play(b, n, ".ims");
}

/* MDI: format 0 MIDI file with one track */
static uint32_t make_mdi(uint8_t *b, const uint8_t *data, unsigned dn)
{
    memcpy(b, "MThd", 4); be32(b + 4, 6); b[8] = 0; b[9] = 0; b[10] = 0; b[11] = 1; b[12] = 0; b[13] = 96;
    memcpy(b + 14, "MTrk", 4); be32(b + 18, dn);
    memcpy(b + 22, data, dn);
    return 22 + dn;
}

static void test_mdi_event_length_past_data(void)
{
    uint8_t b[128];
    static const uint8_t d[] = { 0x00, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x3F };   /* sequencer-specific, length 0x0FFFFFFF */
    uint32_t n = make_mdi(b, d, sizeof d);
    play(b, n, ".mdi");
    static const uint8_t d2[] = { 0x00, 0xF0, 0xFF, 0xFF, 0xFF, 0x7F, 0x00 };                    /* sysex, same length */
    n = make_mdi(b, d2, sizeof d2);
    play(b, n, ".mdi");
    static const uint8_t d3[] = { 0x00, 0x90 };                                                   /* note-on cut off */
    n = make_mdi(b, d3, sizeof d3);
    play(b, n, ".mdi");
}

static void test_composer_voice_from_file(void)
{
    uint8_t b[160], d[2 + 1 + 1 + 5 + 1 + 28 + 1];
    memset(d, 0, sizeof d);
    int p = 0;
    d[p++] = 0x00; d[p++] = 0xFF; d[p++] = 0x7F; d[p++] = 34;       /* meta, sequencer-specific, 6 + 28 bytes */
    d[p++] = 0; d[p++] = 0; d[p++] = 0x3F; d[p++] = 0; d[p++] = 1;   /* AdLib id, timbre */
    d[p++] = 200;                                                    /* voice: far above the 11 the chip has */
    p += 28;
    uint32_t n = make_mdi(b, d, (unsigned)p);
    play(b, n, ".mdi");
}

/* KSM: 5 x 16 bytes of track tables, note count, notes (the instrument bank "insts.dat" is served from the same bytes) */
static uint32_t make_ksm(uint8_t *b, unsigned nnotes, unsigned quant, unsigned note0)
{
    memset(b, 0, 128);
    for (int i = 0; i < 16; i++) b[16 + i] = (uint8_t)quant;
    b[32] = 1;                                     /* track 0 on one channel */
    put16(b + 80, nnotes);
    if (nnotes) put32(b + 82, note0);
    return 82 + 4 * nnotes;
}

static void test_ksm_no_notes(void)
{
    uint8_t b[128];
    uint32_t n = make_ksm(b, 0, 4, 0);
    play(b, n, ".ksm");
}

static void test_ksm_quant_zero_or_large(void)
{
    uint8_t b[128];
    uint32_t n = make_ksm(b, 1, 0, 0x1000);
    play(b, n, ".ksm");
    n = make_ksm(b, 1, 255, 0x1000);
    play(b, n, ".ksm");
}

static void test_ksm_note_index_63(void)
{
    uint8_t b[128];
    uint32_t n = make_ksm(b, 1, 4, 0x1000 | 0x40 | 63);
    play(b, n, ".ksm");
}

/* D00 v2 header (119 bytes), a track table of 32 bytes, then 4 bytes of order list and a 0 byte */
static uint32_t make_d00(uint8_t *b, unsigned version, const uint8_t *order, unsigned on)
{
    memset(b, 0, 200);
    memcpy(b, "JCH\x26\x02\x66", 6);
    b[7] = (uint8_t)version; b[8] = 70; b[9] = 1;
    put16(b + 6 + 5 + 96, 119);                    /* tpoin */
    uint32_t ord = 119 + 32, end = ord + 2 + on;
    put16(b + 119, ord);                           /* channel 0: speed word, then the order list */
    put16(b + ord, 1);
    memcpy(b + ord + 2, order, on);
    put16(b + 6 + 5 + 96 + 2, end);                /* seqptr */
    put16(b + 6 + 5 + 96 + 4, version == 4 ? end : end);   /* instptr: at the very end: no instrument fits */
    put16(b + 6 + 5 + 96 + 6, end);                /* infoptr: the closing 0 byte */
    put16(b + 6 + 5 + 96 + 8, version == 4 ? 0 : end);     /* spfxptr */
    return end + 1;
}

static void test_d00_order_jump_to_itself(void)
{
    uint8_t b[200];
    static const uint8_t o[4] = { 0xFF, 0xFF, 0x00, 0x00 };   /* order[0] = jump, to order position 0 */
    uint32_t n = make_d00(b, 2, o, sizeof o);
    play(b, n, ".d00");
}

static void test_d00_instrument_index_outside_file(void)
{
    uint8_t b[200];
    static const uint8_t o[2] = { 0xFE, 0xFF };               /* end of arrangement */
    uint32_t n = make_d00(b, 4, o, sizeof o);                 /* version 4 reads the instrument timer for the hard restart */
    play(b, n, ".d00");
}

/* MKJ: id, version float, channel count, 8 instrument words each, notes per channel, "defined" word each, then the song */
static void test_mkj_song_without_delay(void)
{
    uint8_t b[128];
    memset(b, 0, sizeof b);
    memcpy(b, "MKJamz", 6);
    put32(b + 6, 0x3F800000u);                     /* 1.0f */
    put16(b + 10, 1);                              /* one channel */
    put16(b + 12 + 16, 2);                         /* two notes */
    put16(b + 12 + 16 + 2, 1);                     /* channel 0 defined; the song is all 0: no note, no delay */
    play(b, 12 + 16 + 4 + 8, ".mkj");
}

int main(void)
{
    RUN(test_protrack_order_names_missing_pattern);
    RUN(test_protrack_order_jump_cycle);
    RUN(test_protrack_zero_patterns_no_leak);
    RUN(test_mus_command_cut_off_by_end);
    RUN(test_mdi_event_length_past_data);
    RUN(test_composer_voice_from_file);
    RUN(test_ksm_no_notes);
    RUN(test_ksm_quant_zero_or_large);
    RUN(test_ksm_note_index_63);
    RUN(test_d00_order_jump_to_itself);
    RUN(test_d00_instrument_index_outside_file);
    RUN(test_mkj_song_without_delay);
    return 0;
}
