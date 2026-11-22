// SPDX-License-Identifier: LGPL-2.1-or-later
/* The voice meter adapters of app/main/backend_gme.cpp against the real emulators: a minimal NSF built from bytes here. */
#include <stdlib.h>
#include <time.h>
#include "backend_gme.h"
#include "pc_test.h"
#include "pc/pc_vmute.h"
#include "emu2413.h"

/* NSF: song 0 = a square note on pulse 1, song 1 = nothing, song 2 = the triangle. INIT is at $8000, PLAY is its RTS. */
static const uint8_t CODE[] = {
    0xC9, 0x01, 0xF0, 0x19, 0xC9, 0x02, 0xF0, 0x1B,                 /* CMP #1 BEQ silent; CMP #2 BEQ tri */
    0xA9, 0x0F, 0x8D, 0x15, 0x40, 0xA9, 0xBF, 0x8D, 0x00, 0x40,     /* $4015=0F, $4000=BF (duty 2, halt, volume 15) */
    0xA9, 0xFD, 0x8D, 0x02, 0x40, 0xA9, 0x08, 0x8D, 0x03, 0x40,     /* period $0FD, length index 1 */
    0x60,                                                           /* $801C: RTS (PLAY) */
    0xA9, 0x00, 0x8D, 0x15, 0x40, 0x60,                             /* silent: $4015=0 */
    0xA9, 0x04, 0x8D, 0x15, 0x40, 0xA9, 0xFF, 0x8D, 0x08, 0x40,     /* tri: $4015=04, $4008=FF (halt, linear 127) */
    0xA9, 0xFD, 0x8D, 0x0A, 0x40, 0xA9, 0x08, 0x8D, 0x0B, 0x40, 0x60,
};

static void nsf_image(uint8_t *out, uint32_t *len)
{
    memset(out, 0, 128);
    memcpy(out, "NESM\x1A", 5);
    out[5] = 1; out[6] = 3; out[7] = 1;                              /* version, songs, first song */
    out[8] = 0x00; out[9] = 0x80; out[10] = 0x00; out[11] = 0x80;    /* load, init $8000 */
    out[12] = 0x1C; out[13] = 0x80;                                  /* play $801C */
    out[110] = 0x1A; out[111] = 0x41;                                /* NTSC speed 16666 us */
    memcpy(out + 128, CODE, sizeof CODE);
    *len = 128 + (uint32_t)sizeof CODE;
}

static int run(void *h, int blocks, pc_voices_t *v)
{
    float buf[2 * 441];
    int n = 0;
    for (int i = 0; i < blocks; i++) {
        CHECK_EQ_INT(backend_gme.render(h, buf, 441), 441);
        n = backend_gme_voices(v);
    }
    return n;
}

static void test_nsf_square_triangle_and_silence(void)
{
    uint8_t img[256];
    uint32_t len;
    nsf_image(img, &len);
    pc_reader_t r;
    pc_reader_mem(&r, img, len);
    void *h;
    pc_voices_t v;
    CHECK_EQ_INT(backend_gme_voices(&v), 0);
    CHECK_EQ_INT(backend_gme.open(&h, &r, 0), 0);
    CHECK_EQ_INT(run(h, 20, &v), 5);
    CHECK_STR(v.name[0], "S1"); CHECK_STR(v.name[2], "T"); CHECK_STR(v.name[3], "N"); CHECK_STR(v.name[4], "D");
    int seen = 0;
    for (int i = 0; i < 40; i++) { run(h, 1, &v); if (v.level[0] > seen) seen = v.level[0]; CHECK_EQ_INT(v.level[2], 0); CHECK_EQ_INT(v.level[4], 0); }
    CHECK(seen > 0);                                                 /* the square note shows... */
    CHECK_EQ_INT(v.level[1], 0); CHECK_EQ_INT(v.level[3], 0);        /* ...the others, and the idle triangle, rest at 0 */
    CHECK_EQ_INT(backend_gme.start_track(h, 2), 0);                  /* the triangle plays */
    run(h, 20, &v);
    CHECK_EQ_INT(v.level[2], 192); CHECK_EQ_INT(v.level[0], 0);
    CHECK_EQ_INT(backend_gme.start_track(h, 0), 0);                  /* back to the square song, then straight to silence: */
    seen = 0;
    for (int i = 0; i < 40; i++) { run(h, 1, &v); if (v.level[0] > seen) seen = v.level[0]; }
    CHECK(seen > 0);
    CHECK_EQ_INT(backend_gme.start_track(h, 1), 0);                  /* the very first block of the new song reads DMC and triangle as 0 */
    run(h, 1, &v);
    CHECK_EQ_INT(v.level[2], 0); CHECK_EQ_INT(v.level[4], 0);
    CHECK_EQ_INT(backend_gme.start_track(h, 2), 0);
    CHECK_EQ_INT(backend_gme.start_track(h, 1), 0);                  /* silence: everything falls to 0 */
    run(h, 60, &v);
    for (int i = 0; i < 5; i++) CHECK_EQ_INT(v.level[i], 0);
    backend_gme.close(h);
    CHECK_EQ_INT(backend_gme_voices(&v), 0);
}

/* visualiser phase 1: the pulse period $0FD is 440.4 Hz = A4, the triangle at the same period an octave lower; one strike per note; the mute mask silences the voice */
static float peak_of(void *h, int blocks)
{
    float buf[2 * 441], pk = 0;
    for (int b = 0; b < blocks; b++) {
        CHECK_EQ_INT(backend_gme.render(h, buf, 441), 441);
        for (int i = 0; i < 2 * 441; i++) if (fabsf(buf[i]) > pk) pk = fabsf(buf[i]);
    }
    return pk;
}

static void test_pitch_strike_mute(void)
{
    uint8_t img[256];
    uint32_t len;
    nsf_image(img, &len);
    pc_reader_t r;
    pc_reader_mem(&r, img, len);
    void *h;
    pc_voices_t v;
    CHECK_EQ_INT(backend_gme.open(&h, &r, 0), 0);
    run(h, 60, &v);                                                  /* the first note starts after about 30 blocks */
    CHECK_NEAR(v.cents[0], 6900, 3);
    CHECK(v.flags[0] & PC_VF_PITCH);
    CHECK(!(v.flags[3] & PC_VF_PITCH)); CHECK(v.flags[3] & PC_VF_NOISE);   /* the 2A03 noise voice: no pitch, flagged noise */
    CHECK_EQ_INT(v.pan[0], PC_PAN_UNKNOWN);
    CHECK(v.strikes[0] >= 1);
    const uint8_t s0 = v.strikes[0];
    run(h, 30, &v);
    CHECK_EQ_INT(v.strikes[0], s0);                                  /* a held note is no new strike */
    CHECK(peak_of(h, 20) > 0.01);                                    /* the note is audible before the mute */
    pc_vmute_sync();
    pc_vmute_set(0, PC_VM_MUTED, 5);                                 /* the UI mutes voice 0: the next blocks decode without it */
    run(h, 40, &v);
    CHECK_EQ_INT(v.level[0], 0);
    CHECK_EQ_INT((int)pc_vmute_mask(), 1);
    CHECK_NEAR(peak_of(h, 20), 0.0, 1e-6);                           /* the pulse was the only sound: the decoder output is silent */
    pc_vmute_clear(5);
    CHECK(peak_of(h, 20) > 0.01);                                    /* and it is back at once (gme must not skip ahead through the muted silence: ignore_silence) */
    pc_vmute_set(1, PC_VM_SOLO, 5);                                  /* solo on a silent voice: everything else is muted */
    run(h, 40, &v);
    CHECK_EQ_INT(v.level[0], 0);
    CHECK_EQ_INT(backend_gme.start_track(h, 2), 0);                  /* a track change: all voices on again, the mask back at 0 */
    CHECK_EQ_INT((int)pc_vmute_mask(), 0);
    run(h, 30, &v);
    CHECK_NEAR(v.cents[2], 5700, 3);
    CHECK_EQ_INT(v.level[2], 192);
    backend_gme.close(h);
}

/* MMC5: an NSF whose header selects the chip (bit 3) and whose INIT starts a square on MMC5 pulse 1 ($5000-$5003) */
static void test_nsf_mmc5_meter(void)
{
    static const uint8_t code[] = { 0xA9, 0x03, 0x8D, 0x15, 0x50, 0xA9, 0xBF, 0x8D, 0x00, 0x50, 0xA9, 0xFD, 0x8D, 0x02, 0x50, 0xA9, 0x08, 0x8D, 0x03, 0x50, 0x60 };
    uint8_t img[256];
    memset(img, 0, sizeof img);
    memcpy(img, "NESM\x1A", 5);
    img[5] = 1; img[6] = 1; img[7] = 1;
    img[8] = 0x00; img[9] = 0x80; img[10] = 0x00; img[11] = 0x80; img[12] = 0x14; img[13] = 0x80;     /* load, init $8000, play = the RTS at $8014 */
    img[110] = 0x1A; img[111] = 0x41;
    img[0x7B] = 0x08;
    memcpy(img + 128, code, sizeof code);
    pc_reader_t r;
    pc_reader_mem(&r, img, 128 + (uint32_t)sizeof code);
    void *h;
    pc_voices_t v;
    CHECK_EQ_INT(backend_gme.open(&h, &r, 0), 0);
    int n = run(h, 60, &v);
    CHECK_EQ_INT(n, 8);                                              /* 5 + the MMC5's 3 */
    CHECK(v.level[5] > 100);                                         /* MMC5 square 1 (volume 15 of 15) */
    CHECK_EQ_INT(v.level[6], 0);
    backend_gme.close(h);
}

#ifdef CHIPTEST_DIR
/* the real expansion-chip NSFs of the main checkout's .local/chiptest: the FDS and VRC7 voices must show a level at some moment of the first 20 s */
static int max_level_of(const char *rel, int first, int count)
{
    char path[512];
    snprintf(path, sizeof path, CHIPTEST_DIR "/%s", rel);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("skip %s\n", rel); return -1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    CHECK(fread(d, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    pc_reader_t r;
    pc_reader_mem(&r, d, (uint32_t)n);
    void *h;
    int best = 0;
    CHECK_EQ_INT(backend_gme.open(&h, &r, 0), 0);
    float buf[2 * 441];
    pc_voices_t v;
    for (int i = 0; i < 2000; i++) {
        CHECK(backend_gme.render(h, buf, 441) > 0);
        if (backend_gme_voices(&v) > 0) for (int k = first; k < first + count && k < v.n; k++) if (v.level[k] > best) best = v.level[k];
    }
    backend_gme.close(h);
    free(d);
    return best;
}
static void test_real_expansion_meters(void)
{
    int fds = max_level_of("nsf/r-nsf-expansion/bio-miracle-fds.nsf", 5, 1), vrc7 = max_level_of("nsf/r-nsf-expansion/lagrange-point-vrc7.nsf", 5, 6);
    printf("FDS meter max %d, VRC7 meter max %d\n", fds, vrc7);
    if (fds >= 0) CHECK(fds > 20);
    if (vrc7 >= 0) CHECK(vrc7 > 20);
}
#else
static void test_real_expansion_meters(void) {}
#endif

/* MSX-MUSIC: a KSS (device flags bit 0 = FM) whose INIT programs FM channel 1 (violin, full volume, key on) through ports $7C / $7D */
static double kss_energy(bool fm_on)
{
    static const uint8_t code[] = {
        0x3E, 0x30, 0xD3, 0x7C, 0x3E, 0x10, 0xD3, 0x7D,             /* reg $30 = $10: instrument 1, volume 0 */
        0x3E, 0x10, 0xD3, 0x7C, 0x3E, 0xAC, 0xD3, 0x7D,             /* reg $10 = $AC: F-number low */
        0x3E, 0x20, 0xD3, 0x7C, 0x3E, 0x1C, 0xD3, 0x7D,             /* reg $20 = $1C: key on, block 6, F-number high 0 */
        0xC9,                                                       /* RET (INIT and PLAY) */
    };
    uint8_t img[16 + sizeof code];
    memset(img, 0, sizeof img);
    memcpy(img, "KSCC", 4);
    img[4] = 0x00; img[5] = 0x40; img[6] = (uint8_t)sizeof code; img[7] = 0;     /* load $4000, size */
    img[8] = 0x00; img[9] = 0x40;                                    /* init $4000 */
    img[10] = 0x18 + 0x00; img[11] = 0x40;                           /* play $4018 = the RET */
    img[15] = 0x01;                                                  /* device flags: FM */
    memcpy(img + 16, code, sizeof code);
    backend_gme_kss_fm(fm_on);
    pc_reader_t r;
    pc_reader_mem(&r, img, sizeof img);
    void *h;
    CHECK_EQ_INT(backend_gme.open(&h, &r, 0), 0);
    float buf[2 * 441];
    double e = 0;
    for (int i = 0; i < 100; i++) {
        CHECK_EQ_INT(backend_gme.render(h, buf, 441), 441);
        for (int k = 0; k < 2 * 441; k++) e += (double)buf[k] * buf[k];
    }
    backend_gme.close(h);
    backend_gme_kss_fm(true);                                       /* back to the default */
    return e;
}

static void test_kss_msx_music(void)
{
    CHECK(kss_energy(false) < 1e-6);                                  /* switched off: the FM writes go nowhere, as before */
    CHECK(kss_energy(true) > 1.0);                                    /* with the chip on (the default since OPLL): the note is there */
}

/* emu2413 (components/emu2413, the nodouble fork): a violin note on channel 0 and a piano on channel 1 */
static void opll_notes(OPLL *o)
{
    static const uint8_t w[][2] = { {0x30, 0x10}, {0x10, 0xAC}, {0x20, 0x1C}, {0x31, 0x30}, {0x11, 0x50}, {0x21, 0x1A} };
    for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) OPLL_writeReg(o, w[i][0], w[i][1]);
}

/* The converter advances once per output sample: stereo (L and R) equals the mono path. Before the fix (GME copy) the shared timer
 * moved on every getData call, so L and R read the history at different positions and neither matched OPLL_calc. */
static void test_opll_rate_conv(void)
{
    static const uint32_t rates[] = { 44100, 48000, 3579545 / 72 };
    for (unsigned r = 0; r < 3; r++) {
        OPLL *st = OPLL_new(3579545, rates[r]), *mo = OPLL_new(3579545, rates[r]);
        CHECK(st && mo);
        CHECK((st->conv != NULL) == (r < 2));                          /* no converter at the chip rate */
        opll_notes(st);
        opll_notes(mo);
        double e = 0;
        int same = 1;
        for (int i = 0; i < 20000; i++) {
            int32_t lr[2];
            OPLL_calcStereo(st, lr);
            int16_t m = OPLL_calc(mo);
            if (lr[0] != m || lr[1] != m) same = 0;
            e += (double)m * m;
        }
        CHECK(same);
        CHECK(e / 20000 > 1e4);                                         /* the notes sound */
        OPLL_delete(st);
        OPLL_delete(mo);
    }
}

/* A panned channel leaves the one-mix fast path: channel 1 to the left only, channel 0 stays centred. The left side
 * then equals the centred mix (both channels), the right side has channel 0 alone once the converter history (the
 * centred mix, copied over when the pan changed, as upstream kept it) has run out. */
static void test_opll_pan(void)
{
    OPLL *a = OPLL_new(3579545, 44100), *both = OPLL_new(3579545, 44100), *ch0 = OPLL_new(3579545, 44100);
    CHECK(a && both && ch0);
    opll_notes(a);
    opll_notes(both);
    opll_notes(ch0);
    OPLL_setMask(ch0, OPLL_MASK_CH(1));
    int32_t lr[2];
    for (int i = 0; i < 3000; i++) { OPLL_calcStereo(a, lr); OPLL_calc(both); OPLL_calc(ch0); }   /* mono first: history kept on one side */
    OPLL_setPan(a, 1, 2);
    CHECK(!a->pan_mono);
    int okl = 1, okr = 1;
    double er = 0;
    for (int i = 0; i < 20000; i++) {
        OPLL_calcStereo(a, lr);
        int16_t b = OPLL_calc(both), c = OPLL_calc(ch0);
        if (lr[0] != b) okl = 0;
        if (i >= 16 /* emu2413.c LW */ && lr[1] != c) okr = 0;                    /* the first taps still hold the centred history */
        er += (double)c * c;
    }
    CHECK(okl);
    CHECK(okr);
    CHECK(er / 20000 > 1e3);
    OPLL_setPan(a, 1, 3);
    for (int i = 0; i < 40; i++) OPLL_calcStereo(a, lr);                 /* back to mono after the converter history has settled (pan_settle) */
    CHECK(a->pan_mono);
    OPLL_delete(a);
    OPLL_delete(both);
    OPLL_delete(ch0);
}

/* JC 0011: Ym2413_Emu skips the samples of a chip that cannot sound (every carrier muted, nothing to commit) and advances its clocks as
 * if they had run. Against a chip calculated sample by sample (the same writes, OPLL_calcStereo): the chip state is identical after a long
 * silence (counters, LFO, noise, slot phases, sample clock), and the output is too, except for the feedback history of an unmuted FB
 * modulator (second scenario). The skip did happen: the converter ring saw fewer samples. */
extern void *ym_new(unsigned rate);
extern void ym_free(void *e);
extern void ym_write(void *e, int a, int d);
extern void ym_run(void *e, int n, short *out);
extern const OPLL *ym_chip(void *e);

static void idle_scenario(int fb, double *err_ratio, int *state_same, int *skipped, int *last_diff)
{
    void *y = ym_new(44100);
    OPLL *r = OPLL_new(3579545, 44100);
    CHECK(y && r);
    OPLL_setChipType(r, 0);
    OPLL_resetPatch(r, 0);
    OPLL_reset(r);
    /* user patch: sustained modulator (it freezes unmuted at key-off), fast-release carrier, feedback fb; channel 0 and a rhythm-off chip */
    const uint8_t w[][2] = { {0x00, 0x21}, {0x01, 0x21}, {0x02, 0x10}, {0x03, (uint8_t)fb}, {0x04, 0xF0}, {0x05, 0xF0}, {0x06, 0x00}, {0x07, 0x0F},
                             {0x30, 0x00}, {0x10, 0xAC}, {0x20, 0x1C} };
    const int run_len[] = { 4000, 0, 70001, 0, 6000 };                 /* note, key off, silence, key on, note */
    const uint8_t off[2] = { 0x20, 0x0C }, on[2] = { 0x20, 0x1C };
    short *a = malloc(4 * 90000), *b = malloc(4 * 90000);
    CHECK(a && b);
    for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) { ym_write(y, w[i][0], w[i][1]); OPLL_writeIO(r, 0, w[i][0]); OPLL_writeIO(r, 1, w[i][1]); }
    double ee = 0, es = 0;
    *last_diff = -1;
    for (int ph = 0; ph < 5; ph++) {
        if (ph == 1 || ph == 3) {
            const uint8_t *k = ph == 1 ? off : on;
            ym_write(y, k[0], k[1]); OPLL_writeIO(r, 0, k[0]); OPLL_writeIO(r, 1, k[1]);
            continue;
        }
        int n = run_len[ph];
        ym_run(y, n, a);
        for (int i = 0; i < n; i++) { int32_t lr[2]; OPLL_calcStereo(r, lr); b[2 * i] = (short)lr[0]; b[2 * i + 1] = (short)lr[1]; }
        int last = -1;
        for (int i = 0; i < 2 * n; i++) { double d = (double)a[i] - b[i]; ee += d * d; es += (double)b[i] * b[i]; if (a[i] != b[i]) last = i / 2; }
        if (last > *last_diff) *last_diff = last;                      /* frame of the last differing sample */
    }
    const OPLL *c = ym_chip(y);
    int same = c->out_time == r->out_time && c->eg_counter == r->eg_counter && c->pm_phase == r->pm_phase && c->am_phase == r->am_phase &&
               c->noise == r->noise && c->lfo_am == r->lfo_am;
    for (int i = 0; i < 18; i++) same = same && c->slot[i].pg_phase == r->slot[i].pg_phase && c->slot[i].eg_out == r->slot[i].eg_out;
    *state_same = same;
    *skipped = c->conv->pos[0] != r->conv->pos[0];
    *err_ratio = es > 0 ? ee / es : 1;
    CHECK(es > 1e6);                                                    /* both notes sound */
    free(a);
    free(b);
    ym_free(y);
    OPLL_delete(r);
}

static void test_ym2413_idle_skip(void)
{
    double er;
    int same, skipped, last;
    idle_scenario(0, &er, &same, &skipped, &last);                            /* no feedback anywhere: bit-identical */
    CHECK(er == 0);
    CHECK(last < 0);
    CHECK(same);
    CHECK(skipped);
    idle_scenario(7, &er, &same, &skipped, &last);                     /* feedback 7: the frozen modulator history differs for a moment */
    CHECK(last >= 0 && last < 200);                                    /* only the first 200 frames (4.5 ms) of the note after the silence */
    CHECK(same);                                                       /* the chip state is exact all the same */
    CHECK(skipped);
}

/* wave-6 review (MED 11): a hostile VGM header clock reached emu2413 unbounded (340 chip steps per output sample at 0x3FFFFFFF: the CPU starved) */
static void le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static int vgm_open_rc2(uint32_t clk, uint32_t psg, double *ms_per_block)
{
    uint8_t d[0x40 + 64];
    memset(d, 0, sizeof d);
    memcpy(d, "Vgm ", 4);
    size_t n = 0x40;
    static const uint8_t cmds[] = { 0x51, 0x30, 0x10, 0x51, 0x10, 0xAC, 0x51, 0x20, 0x18 };    /* instrument, F-number, key on */
    memcpy(d + n, cmds, sizeof cmds);
    n += sizeof cmds;
    for (int i = 0; i < 7; i++) { d[n++] = 0x61; d[n++] = 0xff; d[n++] = 0xff; }
    d[n++] = 0x66;
    le32(d + 4, (uint32_t)n - 4);
    le32(d + 8, 0x150);
    le32(d + 0x10, clk);
    le32(d + 0x0C, psg);
    le32(d + 0x18, 7 * 65535);
    le32(d + 0x34, 0x0C);
    pc_reader_t r;
    pc_reader_mem(&r, d, (uint32_t)n);
    void *h;
    int rc = backend_gme.open(&h, &r, 0);
    if (rc) return rc;
    float buf[2 * 441];
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < 20; i++) CHECK(backend_gme.render(h, buf, 441) >= 0);
    clock_gettime(CLOCK_MONOTONIC, &b);
    if (ms_per_block) *ms_per_block = ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6) / 20;
    backend_gme.close(h);
    return 0;
}
static int vgm_open_rc(uint32_t clk, double *ms_per_block) { return vgm_open_rc2(clk, 0, ms_per_block); }
static void test_vgm_ym2413_clock(void)
{
    double ms = 0;
    CHECK_EQ_INT(vgm_open_rc(3579545, &ms), 0);                 /* the usual clock plays */
    CHECK_EQ_INT(vgm_open_rc(1000000, NULL), 0);
    CHECK_EQ_INT(vgm_open_rc(8000000, NULL), 0);
    CHECK_EQ_INT(vgm_open_rc(0x40000000u, NULL), 0);            /* only the dual bit: no chip, silence */
    CHECK(vgm_open_rc(999999, NULL) != 0);
    CHECK(vgm_open_rc(8000001, NULL) != 0);
    CHECK(vgm_open_rc(1, NULL) != 0);
    CHECK(vgm_open_rc(0x3FFFFFFFu, NULL) != 0);                 /* was accepted and rendered at ~50 % of a host core */
    CHECK(vgm_open_rc(0x7FFFFFFFu, NULL) != 0);                 /* dual bit set, same clock */
    for (uint32_t psg = 1; psg < 100000; psg = psg * 3 + 7)     /* a tiny SN76489 clock tripped Dual_Resampler's assert (abort with assertions on): found by the fuzz run */
        CHECK_EQ_INT(vgm_open_rc2(0, psg, NULL), 0);
}

/* FIX7: VGM streams cut by the end of the data (run_commands read up to 11 bytes past it) and a hostile YM2612 clock (the rate tables overflowed
 * into a negative envelope counter: out-of-range ENV_TAB read in ym2612_update_chan). The data is an exact-size heap block, so ASan sees every byte past it. */
static uint32_t g_psg_clk;                                 /* set by a test: the SN76489 clock, feedback and shift width of the next vgm_play_hostile */
static unsigned g_sn_fb, g_sn_width;
static void vgm_play_hostile(const uint8_t *cmd, size_t ncmd, uint32_t ym2612_clk, int blocks)
{
    size_t n = 0x40 + ncmd;
    uint8_t *d = calloc(1, n);
    memcpy(d, "Vgm ", 4);
    le32(d + 4, (uint32_t)n - 4);
    le32(d + 8, 0x150);
    le32(d + 0x2C, ym2612_clk);
    le32(d + 0x34, 0x0C);
    le32(d + 0x0C, g_psg_clk);
    d[0x28] = (uint8_t)g_sn_fb; d[0x29] = (uint8_t)(g_sn_fb >> 8); d[0x2A] = (uint8_t)g_sn_width;
    memcpy(d + 0x40, cmd, ncmd);
    pc_reader_t r;
    pc_reader_mem(&r, d, (uint32_t)n);
    void *h;
    if (backend_gme.open(&h, &r, 0) == 0) {
        float buf[2 * 441];
        for (int i = 0; i < blocks; i++) if (backend_gme.render(h, buf, 441) < 0) break;
        backend_gme.close(h);
    }
    free(d);
}
static void test_vgm_truncated_commands(void)
{
    static const uint8_t ops[] = { 0x4F, 0x50, 0x3F, 0x30, 0x51, 0x52, 0x53, 0x54, 0xA1, 0xA0, 0x61, 0x62, 0x63, 0x66, 0x67, 0x70, 0x80, 0x90, 0xE0, 0xC0, 0xD0, 0xB0, 0x68, 0x40 };
    for (size_t o = 0; o < sizeof ops; o++)
        for (int have = 0; have <= 11; have++) {                       /* the opcode, then `have` payload bytes (every cut of every command) */
            uint8_t c[16];
            memset(c, 0x5A, sizeof c);
            c[0] = ops[o];
            vgm_play_hostile(c, 1 + (size_t)have, 0, 3);
        }
    uint8_t blk[7] = { 0x67, 0x66, 0x00, 0xFF, 0xFF, 0xFF, 0x7F };    /* a data block that claims 2 GiB */
    vgm_play_hostile(blk, sizeof blk, 0, 3);
    uint8_t seek[16] = { 0xE0, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x81, 0x82, 0x66 };   /* PCM seek far outside the data, then PCM reads */
    vgm_play_hostile(seek, sizeof seek, 0, 3);
    uint8_t nopcm[8] = { 0x8F, 0x8F, 0x8F, 0x7F, 0x66 };               /* PCM delay without any data block */
    vgm_play_hostile(nopcm, sizeof nopcm, 0, 3);
}
static void test_vgm_sn76489_shift_width(void)
{
    static const uint8_t c[] = { 0x50, 0xE6, 0x50, 0xF0, 0x50, 0x9F, 0x62, 0x62, 0x66 };   /* noise: white, loudest */
    g_psg_clk = 3579545;
    static const unsigned w[] = { 0, 1, 15, 16, 17, 31, 32, 33, 57, 100, 197, 220, 255 };
    for (size_t i = 0; i < sizeof w / sizeof *w; i++) { g_sn_fb = 9; g_sn_width = w[i]; vgm_play_hostile(c, sizeof c, 0, 5); }
    g_sn_fb = 0; g_sn_width = 220; vgm_play_hostile(c, sizeof c, 0, 5);
    g_psg_clk = 0; g_sn_fb = 0; g_sn_width = 0;
}
static void test_vgm_hostile_ym2612_clock(void)
{
    uint8_t c[200];
    size_t n = 0;
#define W0(a, v) do { c[n++] = 0x52; c[n++] = (a); c[n++] = (v); } while (0)
    for (int s = 0; s < 4; s++) {
        W0(0x30 + 4 * s, 0x01); W0(0x40 + 4 * s, 0x00); W0(0x50 + 4 * s, 0x1F); W0(0x60 + 4 * s, 0x1F);
        W0(0x70 + 4 * s, 0x1F); W0(0x80 + 4 * s, 0xFF); W0(0x90 + 4 * s, 0x08);
    }
    W0(0xB0, 0x07); W0(0xB4, 0xC0); W0(0xA4, 0x2F); W0(0xA0, 0xFF); W0(0x28, 0xF0);
#undef W0
    c[n++] = 0x62; c[n++] = 0x62; c[n++] = 0x62; c[n++] = 0x66;
    static const uint32_t clks[] = { 0xB5750AB6u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x40000001u, 0x1u, 100u, 0x3B9ACA00u, 7670454u };
    for (size_t i = 0; i < sizeof clks / sizeof *clks; i++) vgm_play_hostile(c, n, clks[i], 40);
}

int main(void)
{
    RUN(test_vgm_ym2413_clock);
    RUN(test_vgm_truncated_commands);
    RUN(test_vgm_hostile_ym2612_clock);
    RUN(test_vgm_sn76489_shift_width);
    RUN(test_nsf_square_triangle_and_silence);
    RUN(test_pitch_strike_mute);
    RUN(test_nsf_mmc5_meter);
    RUN(test_kss_msx_music);
    RUN(test_opll_rate_conv);
    RUN(test_opll_pan);
    RUN(test_ym2413_idle_skip);
    RUN(test_real_expansion_meters);
    return 0;
}
