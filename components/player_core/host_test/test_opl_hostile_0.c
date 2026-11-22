// SPDX-License-Identifier: LGPL-2.1-or-later
/* Central guards of the AdPlug backend against hostile files (patch 0003): a size field that asks for absurd memory is refused as damaged
 * (no abort, no leak), a tick rate in the millions does not spin render(), a too-short file is refused. Inputs are built here. */
#include <stdbool.h>
#include <unistd.h>
#include "pc/pc_backend.h"
#include "pc/pc_chipseq.h"
#include "pc_test.h"

static uint32_t g_bseed;
static unsigned brand(unsigned range) { g_bseed = g_bseed * 0x08088405u + 1; return (unsigned)(((uint64_t)g_bseed * range) >> 32); }

/* A .dmo (TwinTeam, encrypted with its own LCG) whose decrypted word at offset 12 is `blocks`: the loader allocates 0x2000 * blocks bytes. */
static void make_dmo(uint8_t *b, int n, unsigned blocks)
{
    memset(b, 0, (size_t)n);
    const uint32_t s0 = 0x12345678u, x = 0x0BADF00Du;
    b[0] = (uint8_t)s0; b[1] = (uint8_t)(s0 >> 8); b[2] = (uint8_t)(s0 >> 16); b[3] = (uint8_t)(s0 >> 24);
    b[4] = 0; b[5] = 0;                                       /* the key is stretched (word at 4) + 1 times */
    b[6] = (uint8_t)x; b[7] = (uint8_t)(x >> 8); b[8] = (uint8_t)(x >> 16); b[9] = (uint8_t)(x >> 24);
    g_bseed = s0;
    uint32_t seed = 0;
    for (int i = 0; i <= 0; i++) seed += brand(0xffff);
    g_bseed = seed ^ x;
    unsigned chk = brand(0xffff);
    b[10] = (uint8_t)chk; b[11] = (uint8_t)(chk >> 8);
    uint8_t plain[64] = { 0 };
    plain[12] = (uint8_t)blocks; plain[13] = (uint8_t)(blocks >> 8);
    for (int i = 12; i < n; i++) b[i] = (uint8_t)(plain[i] ^ brand(0x100));
}

static void test_dmo_huge_allocation(void)
{
    uint8_t b[64];
    make_dmo(b, 64, 0xFFFF);                                  /* 0x2000 * 65535 = 512 MiB: the old code abort()ed in operator new */
    pc_reader_t r;
    pc_reader_mem(&r, b, sizeof b);
    pc_reader_set_ext(&r, "x.dmo");
    CHECK_EQ_INT(pc_opl_check(&r), -4);                       /* damaged, with a reason */
    void *h = NULL;
    CHECK(pc_backend_opl.open(&h, &r, 0) != 0);
    make_dmo(b, 64, 1);                                       /* a sane size gets as far as the (failing) unpack, still no crash */
    pc_reader_mem(&r, b, sizeof b);
    pc_reader_set_ext(&r, "x.dmo");
    CHECK(pc_backend_opl.open(&h, &r, 0) != 0);
}

static void test_tiny_dmo(void)
{
    uint8_t b[20];
    make_dmo(b, 20, 1);
    pc_reader_t r;
    pc_reader_mem(&r, b, sizeof b);
    pc_reader_set_ext(&r, "x.dmo");
    void *h;
    CHECK(pc_backend_opl.open(&h, &r, 0) != 0);
}

/* RAW file with clock 1 (1.19 MHz tick rate, RAW's maximum): sanity only; the clamp itself (render_impl: hz <= 2500) matters for players whose
 * rate field is 32 bits (LDS hung with a rate in the billions), which a test cannot build without that player's format */
static void test_tick_rate_clamp(void)
{
    uint8_t b[64];
    memset(b, 0, sizeof b);
    memcpy(b, "RAWADATA", 8);
    b[8] = 1; b[9] = 0;
    int n = 10;
    b[n++] = 0x21; b[n++] = 0x20;                            /* a register write, then waits of 1 tick */
    for (int i = 0; i < 20; i++) { b[n++] = 1; b[n++] = 0; }
    b[n++] = 0xFF; b[n++] = 0xFF;
    pc_reader_t r;
    pc_reader_mem(&r, b, (uint32_t)n);
    pc_reader_set_ext(&r, "x.rac");
    void *h = NULL;
    alarm(20);
    if (!pc_backend_opl.open(&h, &r, 0)) {
        static float out[2 * 441];
        for (int i = 0; i < 20; i++) { int g = pc_backend_opl.render(h, out, 441); CHECK(g >= 0); if (g < 441) break; }
        pc_backend_opl.close(h);
    }
    alarm(0);
}

/* RAD v2 (patch 0008): the order entry a jump marker points at may itself have bit 7 set; its low 7 bits (up to 127) then index Tracks[100] */
static void test_rad_jump_to_marker(void)
{
    uint8_t f[16 + 4 + 1 + 102 + 2];
    memset(f, 0, sizeof f);
    memcpy(f, "RAD by REALiTY!!", 16);
    f[16] = 0x21;                                            /* version 2.1; flags 0; empty description; no instruments */
    f[20] = 102;                                             /* order list: entry 0 jumps to entry 101, which is itself a marker (127) */
    f[21] = 0x80 | 101;
    f[21 + 101] = 0xFF;
    f[21 + 102] = 0xFF;                                      /* no patterns */
    f[21 + 103] = 0xFF;                                      /* no riffs */
    pc_reader_t r;
    pc_reader_mem(&r, f, sizeof f);
    pc_reader_set_ext(&r, "x.rad");
    void *h = NULL;
    if (!pc_backend_opl.open(&h, &r, 0)) {
        static float out[2 * 441];
        for (int i = 0; i < 20; i++) { int g = pc_backend_opl.render(h, out, 441); CHECK(g >= 0); if (g < 441) break; }
        pc_backend_opl.close(h);
    }
}

int main(void)
{
    RUN(test_rad_jump_to_marker);
    RUN(test_dmo_huge_allocation);
    RUN(test_tiny_dmo);
    RUN(test_tick_rate_clamp);
    return 0;
}
