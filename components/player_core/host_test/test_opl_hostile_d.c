// SPDX-License-Identifier: LGPL-2.1-or-later
/* Hostile-file regressions for the AdPlug players msc, pis, u6m (.m), herad (.sdb/.agd/.ha2/.hsq/.sqx) and sng (libadplug patch 0007).
 * Every input is built here from a minimal header plus the one offending field (no bytes of a real tune). open() may refuse or the
 * song may play to its end; it must neither crash (ASan) nor loop. */
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc_test.h"

static void put16(uint8_t *b, int o, unsigned v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }

/* open + play a few blocks + close; returns true when the file was accepted */
static bool try_play(const uint8_t *d, uint32_t n, const char *ext)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, n);
    pc_reader_set_ext(&r, ext);
    void *h = NULL;
    if (pc_backend_opl.open(&h, &r, 0)) return false;
    float out[2 * 441];
    for (int i = 0; i < 20; i++) { int g = pc_backend_opl.render(h, out, 441); CHECK(g >= 0); if (g == 0) break; }
    pc_backend_opl.close(h);
    return true;
}

/* msc: a prefix byte (155) is the last byte of its block, so the length / distance byte would be read past the block */
static void test_msc_prefix_at_block_end(void)
{
    uint8_t b[128] = { 0 };
    memcpy(b, "Ceres \x13 MSCplay ", 16);
    int o = 16 + 2 + 64;                       /* signature, version 0, description */
    put16(b, o, 1000); put16(b, o + 2, 1); put16(b, o + 4, 64);   /* timer, one block, block length */
    o += 6;
    put16(b, o, 1); b[o + 2] = 155;            /* the block: one byte, a prefix */
    (void)try_play(b, (uint32_t)(o + 3), ".msc");
}

/* pis: header with `npat` patterns / `ninst` instruments, one order row, optional order byte */
static uint32_t make_pis(uint8_t *b, int npat, int ninst, int len, uint8_t ord)
{
    memset(b, 0, 4096);
    b[0] = (uint8_t)len; b[1] = (uint8_t)npat; b[2] = (uint8_t)ninst;
    int o = 3 + npat + ninst;
    for (int v = 0; v < 9 * len; v++) b[o + v] = ord;
    return (uint32_t)(o + 9 * len + 4 * 64 * 3);
}
/* pis: more patterns than the 128-entry map */
static void test_pis_too_many_patterns(void)
{
    static uint8_t b[4096];
    CHECK(!try_play(b, make_pis(b, 200, 0, 1, 0), ".pis"));
}
/* pis: more instruments than the 32-entry map */
static void test_pis_too_many_instruments(void)
{
    static uint8_t b[4096];
    CHECK(!try_play(b, make_pis(b, 0, 200, 1, 0), ".pis"));
}
/* pis: the order list names pattern 255 (the table holds 128) */
static void test_pis_order_pattern_index(void)
{
    static uint8_t b[4096];
    (void)try_play(b, make_pis(b, 0, 0, 1, 255), ".pis");
}
/* pis: note 11 then an arpeggio of +15 / +14 on the next row: the arpeggio notes (26, 25) indexed the 12-entry frequency table out of range */
static void test_pis_arpeggio_past_octave(void)
{
    static uint8_t b[4096];
    memset(b, 0, sizeof b);
    b[0] = 1; b[1] = 1; b[2] = 0;                           /* one order row, one pattern, no instrument */
    int o = 3 + 1 + 9;                                      /* pattern 0 after the pattern map and the order row */
    b[o] = 0xB0; b[o + 1] = 0x10;                           /* row 0: note 11, instrument 1 */
    b[o + 3] = 0xF0; b[o + 5] = 0xFE;                       /* row 1: no note, arpeggio +15 / +14 */
    CHECK(try_play(b, (uint32_t)(o + 192), ".pis"));
}

/* pis: pattern break (effect D) to row 255 of a 64-row pattern, speed 1: the row counter never reached 64 again and ran out of the pattern table */
static void test_pis_break_row_unbounded(void)
{
    static uint8_t b[4096];
    memset(b, 0, sizeof b);
    b[0] = 2; b[1] = 2; b[2] = 0;                           /* two order rows, two patterns */
    b[3] = 0; b[4] = 1;                                     /* pattern map */
    int o = 5;
    b[o + 1] = 1;                                           /* order row 0: voice 0 plays pattern 0, voice 1 pattern 1; row 1: pattern 0 */
    o += 18;
    b[o + 1] = 0x0D; b[o + 2] = 0xFF;                       /* pattern 0, row 0: effect Dxx = 0xFF */
    b[o + 192 + 1] = 0x0F; b[o + 192 + 2] = 0x01;           /* pattern 1, row 0: effect F01 = speed 1 */
    CHECK(try_play(b, (uint32_t)(o + 2 * 192), ".pis"));
}

/* sop: a panning event with a raw value of 11 indexed the 3-entry pan table */
static void test_sop_pan_value_range(void)
{
    uint8_t b[256] = { 0 };
    memcpy(b, "sopepos", 7); b[7] = 0x00; b[8] = 0x01;     /* version 0x100 */
    b[7 + 3 + 13 + 31 + 2] = 4;                            /* ticks per beat (byte after percussive + 0) */
    b[7 + 3 + 13 + 31 + 4] = 4;                            /* beats per measure */
    b[7 + 3 + 13 + 31 + 5] = 120;                          /* tempo */
    int o = 76 - 3;                                        /* number of tracks, instruments, 0 */
    b[o] = 1; b[o + 1] = 1;
    o = 76;
    b[o++] = 2;                                            /* chanMode[0]: 2 operators */
    b[o] = 12; o += 1 + 8 + 19;                            /* one unused instrument */
    b[o] = 4; b[o + 2] = 6; o += 6;                        /* track 0: size 6 */
    b[o + 2] = 7; b[o + 3] = 11; o += 6;                   /* delay 0, pan event, value 11, delay 0 */
    CHECK(try_play(b, (uint32_t)(o + 6), ".sop"));         /* track 1 (control): empty */
}

/* sop: a track whose last byte is the first half of a 2-byte delay: the delay / next-delay check read past the track data */
static void test_sop_track_cut_in_delay(void)
{
    uint8_t b[256] = { 0 };
    memcpy(b, "sopepos", 7); b[7] = 0x00; b[8] = 0x01;
    b[7 + 3 + 13 + 31 + 2] = 4; b[7 + 3 + 13 + 31 + 4] = 4; b[7 + 3 + 13 + 31 + 5] = 120;
    b[73] = 1; b[74] = 1;
    int o = 76;
    b[o++] = 2;
    b[o] = 12; o += 1 + 8 + 19;
    b[o] = 2; b[o + 2] = 5; o += 6;                        /* track 0: 5 bytes: delay 0, pan event, value 1, one byte of the next delay */
    b[o + 2] = 7; b[o + 3] = 1; o += 5;
    CHECK(try_play(b, (uint32_t)(o + 6), ".sop"));
}

/* u6m (reached through the .rix ext, which falls back to every player): packs 9-bit LZW codes (the stream is a reset, literals, a repeated 2-byte entry, the end marker) */
static uint32_t make_u6m(uint8_t *b, const uint8_t *song, int ns, int repeats)
{
    int codes[64], nc = 0;
    codes[nc++] = 0x100;
    for (int i = 0; i < ns; i++) codes[nc++] = song[i];
    for (int i = 0; i < repeats; i++) codes[nc++] = 0x100 + 4;   /* entry 0x104 = bytes 3 and 4 of the song */
    codes[nc++] = 0x101;
    uint8_t *p = b + 4;                                     /* the stream starts at offset 4: its first code (reset) is the pseudo header's bytes 4-5 */
    memset(b, 0, 64);
    for (int i = 0; i < nc; i++) for (int k = 0; k < 9; k++) { int bit = i * 9 + k; if (codes[i] >> k & 1) p[bit / 8] |= (uint8_t)(1 << (bit % 8)); }
    uint32_t clen = (uint32_t)((nc * 9 + 7) / 8);
    put16(b, 0, (unsigned)(ns + 2 * repeats));
    return 4 + clen;
}
/* u6m: command 7 (assign an instrument) before any instrument was loaded used an uninitialised offset */
static void test_u6m_instrument_not_loaded(void)
{
    uint8_t b[128], song[4] = { 0x70, 0x00, 0x82, 0x01 };
    uint32_t n = make_u6m(b, song, 4, 12);
    CHECK(try_play(b, n, ".rix"));   /* the stream must decode, otherwise the song never starts */
}

/* herad: header (u16 instrument offset, u16 track offsets, speed at 0x32), one 40-byte instrument at the end, one track of `tl` bytes at 0x34 */
static uint32_t make_herad(uint8_t *b, const uint8_t *trk, int tl)
{
    memset(b, 0, 512);
    put16(b, 0, (unsigned)(0x34 + tl));        /* instruments start after the track */
    put16(b, 2, 0x32);                         /* first track offset 0x32 = v1 file */
    put16(b, 0x32, 0x0100);                    /* speed */
    memcpy(b + 0x34, trk, (size_t)tl);
    return (uint32_t)(0x34 + tl + 40);
}
/* herad: the track ends in a continuation delay byte, the event after it would be read past the track data */
static void test_herad_truncated_event(void)
{
    uint8_t b[512], trk[1] = { 0x80 };
    uint32_t n = make_herad(b, trk, 1);
    (void)try_play(b, n, ".sdb");
    uint8_t t2[3] = { 0x00, 0x90, 0x40 };      /* note-on without its velocity byte */
    n = make_herad(b, t2, 3);
    (void)try_play(b, n, ".sdb");
}
/* herad: a second track offset far beyond the file; the first track's size was computed from it */
static void test_herad_track_offset_beyond_file(void)
{
    uint8_t b[512], trk[2] = { 0x00, 0x90 };
    uint32_t n = make_herad(b, trk, 2);
    put16(b, 4, 0xFFF0);
    (void)try_play(b, n, ".sdb");
    put16(b, 4, 0x0010);                       /* a track that starts before the header's end: next < offset */
    (void)try_play(b, n, ".sdb");
}

/* sng: start (and loop) point beyond the data */
static void test_sng_start_beyond_data(void)
{
    uint8_t b[64] = { 0 };
    memcpy(b, "ObsM", 4);
    put16(b, 4, 2); put16(b, 6, 200); put16(b, 8, 0); b[10] = 1; b[11] = 0;   /* length 2 bytes = 1 entry, start 200 */
    b[12] = 1; b[13] = 1;
    CHECK(!try_play(b, 14, ".sng"));
}
/* sng: no entry has register 0, so update() never reached the end of its inner loop */
static void test_sng_no_terminator(void)
{
    uint8_t b[64] = { 0 };
    memcpy(b, "ObsM", 4);
    put16(b, 4, 4); put16(b, 6, 0); put16(b, 8, 0); b[10] = 1; b[11] = 1;
    b[12] = 1; b[13] = 1; b[14] = 1; b[15] = 1;
    (void)try_play(b, 16, ".sng");
}

/* xsm: song length 0 allocates an empty note table that update() reads */
static void test_xsm_zero_length(void)
{
    uint8_t b[256] = { 0 };
    memcpy(b, "ofTAZ!", 6);                 /* songlen (word at 6) stays 0 */
    CHECK(!try_play(b, 6 + 2 + 9 * 16, ".xsm"));
}

/* optional argument: run only the named test (to check one class on its own) */
static const char *g_only;
#define RUN1(t) do { if (!g_only || !strcmp(g_only, #t)) RUN(t); } while (0)

int main(int argc, char **argv)
{
    g_only = argc > 1 ? argv[1] : NULL;
    RUN1(test_xsm_zero_length);
    RUN1(test_msc_prefix_at_block_end);
    RUN1(test_pis_too_many_patterns);
    RUN1(test_pis_too_many_instruments);
    RUN1(test_pis_order_pattern_index);
    RUN1(test_pis_arpeggio_past_octave);
    RUN1(test_pis_break_row_unbounded);
    RUN1(test_sop_pan_value_range);
    RUN1(test_sop_track_cut_in_delay);
    RUN1(test_u6m_instrument_not_loaded);
    RUN1(test_herad_truncated_event);
    RUN1(test_herad_track_offset_beyond_file);
    RUN1(test_sng_start_beyond_data);
    RUN1(test_sng_no_terminator);
    printf("all ok\n");
    return 0;
}
