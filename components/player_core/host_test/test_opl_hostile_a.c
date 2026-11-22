// SPDX-License-Identifier: LGPL-2.1-or-later
/* Hostile-file regressions for the AdPlug players rad hsc hsp imf wlf a2m a2t amd xms bam cmf adl (libadplug patch 0004).
 * Every input is built here from synthetic bytes (a minimal header + the offending field); open may refuse or the file may play to its
 * end, but it must neither crash (ASan/UBSan builds) nor loop. */
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc/pc_vmute.h"
#include "pc_test.h"

/* open `d` as `name`, render up to ~2 s (bounded), close; true when it was accepted */
static bool play(const uint8_t *d, uint32_t n, const char *name)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, n);
    pc_reader_set_ext(&r, name);
    void *h = NULL;
    if (pc_backend_opl.open(&h, &r, 0)) return false;
    static float buf[2 * 441];
    for (int i = 0; i < 200; i++) {
        int g = pc_backend_opl.render(h, buf, 441);
        CHECK(g >= 0);
        if (g < 441) break;
    }
    pc_backend_opl.close(h);
    pc_vmute_clear(18);
    return true;
}

/* BAM: a define-instrument command (0x30) in the last byte reads 11 bytes past it; the zero tail was 8 bytes */
static void test_bam_define_instrument_at_end(void)
{
    static const uint8_t f[] = { 'C', 'B', 'M', 'F', 0x30 };
    (void)play(f, sizeof f, "A.BAM");
    static const uint8_t g[] = { 'C', 'B', 'M', 'F' };       /* header only: size 0 */
    (void)play(g, sizeof g, "A.BAM");
    static const uint8_t h[] = { 'C', 'B', 'M' };            /* shorter than the header: size underflow */
    CHECK(!play(h, sizeof h, "A.BAM"));
}

/* RAD v2 validator: an instrument name that ends exactly at the buffer end left the algorithm byte one past it */
static void test_rad_name_to_end(void)
{
    uint8_t f[16 + 5 + 3];
    memcpy(f, "RAD by REALiTY!!", 16);
    f[16] = 0x21; f[17] = 0x00; f[18] = 0x00;                /* version 2.1, flags 0, empty description */
    f[19] = 0x01; f[20] = 4;                                 /* instrument 1, name length 4: 3 bytes + the loader's pad byte */
    memset(f + 21, 'x', 3);
    CHECK(!play(f, sizeof f, "A.RAD"));
}

/* RAD v2 validator: an order list longer than the rest of the file was walked past the buffer */
static void test_rad_order_list_past_end(void)
{
    uint8_t f[16 + 6];
    memcpy(f, "RAD by REALiTY!!", 16);
    f[16] = 0x21; f[17] = 0x00; f[18] = 0x00;                /* version 2.1, flags 0, empty description */
    f[19] = 0;                                               /* no instruments */
    f[20] = 5; f[21] = 0;                                    /* order list of 5 entries, 1 present + the loader's pad byte */
    CHECK(!play(f, sizeof f, "A.RAD"));
}

/* ADL (Coktel): a pitch bend on voice 9/10 with rhythm mode off indexed the per-voice key-on cache (9 entries) past its end */
static void test_adl_pitch_bend_percussive_voice(void)
{
    uint8_t f[3 + 56 + 8];
    memset(f, 0, sizeof f);                                  /* sound mode 0, one timbre of zeros */
    const uint8_t song[8] = { 0x01, 0xAA, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00 };   /* delay, pitch bend voice 10, note on, zero delay */
    memcpy(f + 59, song, 8);
    (void)play(f, sizeof f, "A.ADL");
}

/* ADL: a 15-bit delay (high bit set) as the very last byte read its second byte past the data */
static void test_adl_long_delay_at_end(void)
{
    uint8_t f[3 + 56 + 8];
    memset(f, 0, sizeof f);
    const uint8_t song[8] = { 0x01, 0xAA, 0x40, 0x00, 0x00, 0x00, 0x00, 0xFF };
    memcpy(f + 59, song, 8);
    (void)play(f, sizeof f, "A.ADL");
}

/* A2T (sixpack, version 1): the packed block starts at an odd offset; the decoder loaded it as 16-bit words (misaligned) */
static void test_a2t_sixpack_odd_offset(void)
{
    uint8_t f[23 + 12 + 40];
    memset(f, 0, sizeof f);
    memcpy(f, "_A2tiny_module_", 15);
    f[19] = 1; f[21] = 6; f[22] = 125;                       /* ffver 1, tempo, speed */
    f[23] = 40;                                              /* len[0] = 40 packed bytes of instrument block (UINT16LE) */
    memset(f + 35, 0x55, 40);
    (void)play(f, sizeof f, "A.A2T");
}

int main(void)
{
    RUN(test_bam_define_instrument_at_end);
    RUN(test_rad_name_to_end);
    RUN(test_rad_order_list_past_end);
    RUN(test_adl_pitch_bend_percussive_voice);
    RUN(test_adl_long_delay_at_end);
    RUN(test_a2t_sixpack_odd_offset);
    return 0;
}
