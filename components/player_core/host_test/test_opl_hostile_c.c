// SPDX-License-Identifier: LGPL-2.1-or-later
/* AdPlug hostile-file regressions, group c (libadplug patch 0006): dmo dtm mtk mtr rac sat sa2 xad bmf lds plx rol.
 * Every input is built here from synthetic bytes (a minimal header + the offending field). A file may be refused or play to its end;
 * it must neither crash (ASan) nor loop (alarm). */
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include "pc/pc_backend.h"
#include "pc/pc_chipseq.h"
#include "pc_test.h"

/* open `b` as extension `ext`, render up to ~8 s of audio, close */
static void play_hostile(const uint8_t *b, uint32_t n, const char *ext)
{
    pc_reader_t r;
    pc_reader_mem(&r, b, n);
    pc_reader_set_ext(&r, ext);
    void *h = NULL;
    alarm(30);
    if (!pc_backend_opl.open(&h, &r, 0)) {
        static float out[2 * 441];
        for (int i = 0; i < 800; i++) { int g = pc_backend_opl.render(h, out, 441); CHECK(g >= 0); if (g < 441) break; }
        pc_backend_opl.close(h);
    }
    alarm(0);
}

/* MTRACK (version 2) with `ninst` instruments in the header, one pattern of one voice whose every row plays instrument number `used` */
static void mtr_inst(int ninst, int used, int nv)
{
    static uint8_t b[70 + 256 + 64 * 256 + 64 * 4 * 40];
    memset(b, 0, sizeof b);
    char hdr[64];
    snprintf(hdr, sizeof hdr, "MTRACK NC%c%02x %02x %02x %02x %02x %02x %04x %08x", ' ', nv, 0, 0, 0, ninst, 0, 0x428F, 0);
    memset(b, ' ', 50);
    memcpy(b, hdr, strlen(hdr));
    memset(b + 50, 'A', 20);
    uint32_t n = 70 + 256 + (uint32_t)ninst * 64;             /* order (1 entry + 255 skipped), instruments */
    for (int k = 0; k < 64 * (nv + 1); k++) { b[n] = 0x31; b[n + 1] = (uint8_t)used; b[n + 2] = 0; b[n + 3] = 0; n += 4; }
    play_hostile(b, n, "x.mtr");
}
static void test_mtr_instrument_count(void)
{
    mtr_inst(0, 1, 0);       /* no instrument table at all: the player still read inst[0] */
    mtr_inst(1, 40, 0);      /* instrument number past the table */
}

/* 33 voices: the active-channel mask is 32 bits wide and was shifted by 31 - chan (negative from channel 32) */
static void test_mtr_many_voices(void)
{
    mtr_inst(2, 1, 32);
}

/* SAdT version 3 (old 5-byte events): one pattern whose events name instrument 200 while the loader has a table of 31 */
static void test_sat_instrument_number(void)
{
    static uint8_t b[5 + 341 + 493 + 3 + 128 + 4 + 2 + 64 * 9 * 5];
    memset(b, 0, sizeof b);
    memcpy(b, "SAdT", 4);
    b[4] = 3;
    uint32_t n = 5 + 341 + 493 + 3 + 128;
    b[n] = 1; b[n + 1] = 0;                                    /* nop = 1 */
    b[n + 2] = 1;                                              /* order length 1 (order[0] = 0), restart 0 */
    b[n + 4] = 0x00; b[n + 5] = 0x01;                          /* bpm word */
    n += 6;
    for (int e = 0; e < 64 * 9; e++) { b[n] = 20; b[n + 1] = 200; n += 5; }
    play_hostile(b, n, "x.sat");
}

/* LDS (.ld0, no MIDI fields): a note on high = 0 through a patch whose fine tune is -128 gave a negative frequency table index */
static void lds_note(int finetune, int arpeggio)
{
    static uint8_t b[1 + 2 + 1 + 1 + 9 + 1 + 2 + 2 * 40 + 2 + 9 * 3 + 2 + 64];
    memset(b, 0, sizeof b);
    uint32_t n = 0;
    b[n++] = 0;                                               /* mode */
    b[n++] = 70; b[n++] = 0;                                  /* speed */
    b[n++] = 1;                                               /* tempo */
    b[n++] = 40;                                              /* pattern length */
    n += 9 + 1;                                               /* channel delays, regbd */
    b[n++] = 2; b[n++] = 0;                                   /* two patches */
    for (int p = 0; p < 2; p++) { b[n + 14] = (uint8_t)finetune; b[n + 20] = (uint8_t)arpeggio; n += 40; }   /* fine tune -128, arpeggio table unused */
    b[n++] = 1; b[n++] = 0;                                   /* one position */
    for (int c = 0; c < 9; c++) { b[n++] = 0; b[n++] = 0; b[n++] = 0; }
    n += 2;                                                   /* digital sounds */
    b[n++] = 1; b[n++] = 0;                                   /* word 0x0001 (little endian): note 0 of patch 1, then empty words */
    play_hostile(b, n, "x.ld0");
}
static void test_lds_negative_tune(void) { lds_note(0x80, 1); }
/* arpeggio size 15 (low nibble) on a 12-entry table: the position ran past the end */
static void test_lds_arp_size(void) { lds_note(0, 0x0F); }

/* U6 music (.m files are LZW-packed; the packer is built here from nothing but the song bytes): a song whose every tick runs ~65 000
 * commands through nested subsong calls (81 = call, F = return) and then delays 0 ticks, so the silent length scan ran for minutes. */
static uint32_t lzw_pack(const uint8_t *song, int len, uint8_t *out)
{
    static uint16_t prefix[4096]; static uint8_t chr[4096];
    uint32_t bits = 0, nbits = 0; int next = 0x102, nout = 0;
    uint8_t acc[4];
#define EMIT(code) do { bits |= (uint32_t)(code) << nbits; nbits += 9; while (nbits >= 8) { out[nout++] = (uint8_t)bits; bits >>= 8; nbits -= 8; } } while (0)
    (void)acc;
    EMIT(0x100);
    int w = song[0];
    for (int i = 1; i < len; i++) {
        int c = song[i], hit = -1;
        for (int e = 0x102; e < next; e++) if (prefix[e] == w && chr[e] == c) { hit = e; break; }
        if (hit >= 0) { w = hit; continue; }
        EMIT(w);
        prefix[next] = (uint16_t)w; chr[next] = (uint8_t)c; next++;
        w = c;
    }
    EMIT(w); EMIT(0x101);
    if (nbits) out[nout++] = (uint8_t)bits;
#undef EMIT
    return (uint32_t)nout;
}
static void test_u6m_nested_calls(void)
{
    static const uint8_t prog[17] = {
        0xE0,                                  /* 0: loop point */
        0x81, 0xFF, 0x0C, 0x00,                /* 1: call 12, 255 times (continue at 5) */
        0x82, 0x00,                            /* 5: end of tick, delay 0 */
        0x81, 0x00, 0x01, 0x00,                /* 7: jump to 1 (a call that is never returned) */
        0x00,                                  /* 11: unused */
        0x81, 0xFF, 0x10, 0x00,                /* 12: call 16, 255 times */
        0xF0 };                                /* 16: return */
    uint8_t song[300] = { 0 };
    memcpy(song, prog, sizeof prog);           /* zero padding so that the packed file is smaller than the song, as the format requires */
    uint8_t file[600] = { 0 };
    uint32_t n = lzw_pack(song, (int)sizeof song, file + 4);
    file[0] = (uint8_t)(sizeof song & 0xFF); file[1] = (uint8_t)(sizeof song >> 8);
    play_hostile(file, 4 + n, "x.m");
}

int main(void)
{
    RUN(test_mtr_instrument_count);
    RUN(test_mtr_many_voices);
    RUN(test_sat_instrument_number);
    RUN(test_lds_negative_tune);
    RUN(test_lds_arp_size);
    RUN(test_u6m_nested_calls);
    return 0;
}
