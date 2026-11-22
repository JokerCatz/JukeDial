// SPDX-License-Identifier: LGPL-2.1-or-later
/* AHX / HVL backend: a minimal AHX built in memory (no music file in git), the structure checks (every truncation and random
 * byte flips must fail cleanly or play, under ASan), mute, voices, sub-song and resume. When the main checkout's .local/chiptest
 * has the HVL / AHX sample files they are played too. */
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc/pc_id3.h"
#include "pc/pc_extvoice.h"
#include "pc/pc_vmute.h"
#include <pthread.h>
#include "hvl_replay.h"
#include "pc_test.h"

/* 4 voices x 4 steps at tempo 6 = 24 frames = 480 ms. Voice 0 plays note `note` of instrument 1 on step 0, the others the same. */
static uint8_t *make_ahx(uint32_t *len, int note, int ssn)
{
    uint8_t *b = calloc(1, 400);
    uint32_t o = 0;
    memcpy(b, "THX\x01", 4);
    b[6] = 0x00; b[7] = 1;                         /* 1 position, speed multiplier 1, track 0 is stored */
    b[8] = b[9] = 0;                               /* restart 0 */
    b[10] = 4; b[11] = 0; b[12] = 1; b[13] = (uint8_t)ssn;
    o = 14;
    for (int i = 0; i < ssn; i++) { b[o++] = 0; b[o++] = 0; }
    for (int j = 0; j < 4; j++) { b[o++] = 0; b[o++] = 0; }                   /* track 0, transpose 0 */
    b[o] = (uint8_t)(note << 2); b[o + 1] = 0x10; b[o + 2] = 0; o += 3;       /* step 0: note, instrument 1 (high 2 bits 0, low 4 in byte1) */
    o += 9;                                                                   /* three empty steps */
    uint32_t i0 = o;
    b[i0 + 0] = 64;                                /* volume */
    b[i0 + 1] = 3;                                 /* wave length 3, filter speed 0 */
    b[i0 + 2] = 1; b[i0 + 3] = 64; b[i0 + 4] = 1; b[i0 + 5] = 64; b[i0 + 6] = 100; b[i0 + 7] = 1; b[i0 + 8] = 0;   /* envelope */
    b[i0 + 20] = 1; b[i0 + 21] = 1;                /* plist speed 1, one entry: waveform 1 (triangle) - without one the voice has no wave */
    b[i0 + 23] = 0x80;
    o += 26;
    b[4] = (uint8_t)(o >> 8); b[5] = (uint8_t)o;   /* song name offset */
    memcpy(b + o, "Test\0I\0", 7);
    o += 7;
    *len = o;
    return b;
}

static int play_all(const pc_backend_t *be, void *h, double *energy, int max_blocks)
{
    float out[2 * 441];
    int total = 0;
    *energy = 0;
    for (int blk = 0; blk < max_blocks; blk++) {
        int g = be->render(h, out, 441);
        if (g <= 0) break;
        for (int i = 0; i < 2 * g; i++) *energy += (double)out[i] * out[i];
        total += g;
    }
    return total;
}

static void test_play(void)
{
    uint32_t len;
    uint8_t *d = make_ahx(&len, 24, 0);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_hvl.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_hvl.rate(h), 44100);
    CHECK_EQ_INT(pc_backend_hvl.track_count(h), 1);
    CHECK(pc_backend_hvl.length_ms(h) >= 440 && pc_backend_hvl.length_ms(h) <= 520);
    double e;
    int n = play_all(&pc_backend_hvl, h, &e, 100);
    CHECK(n >= 44100 * 44 / 100 && n <= 44100 * 52 / 100);
    CHECK(e > 1.0);
    pc_voices_t v;
    CHECK_EQ_INT(pc_extvoice_read(&v), 4);
    CHECK(v.flags[0] & PC_VF_PITCH);
    CHECK(v.cents[0] > 2400 && v.cents[0] < 9600);
    CHECK(v.strikes[0] >= 1);
    pc_backend_hvl.close(h);
    CHECK_EQ_INT(pc_extvoice_read(&v), 0);
    free(d);
}

static void test_tags(void)
{
    uint32_t len;
    uint8_t *d = make_ahx(&len, 24, 0);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    pc_id3_t t;
    memset(&t, 0, sizeof t);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "Test");
    pc_id3_free(&t);
    free(d);
}

static void test_mute(void)
{
    uint32_t len;
    uint8_t *d = make_ahx(&len, 24, 0);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_hvl.open(&h, &r, 0), 0);
    float out[2 * 441];
    CHECK(pc_backend_hvl.render(h, out, 441) > 0);        /* the first block takes the voices over: the mask starts at 0 */
    pc_vmute_set(0, PC_VM_SOLO, 4);                        /* only voice 0 */
    pc_vmute_set(1, PC_VM_MUTED, 4);
    double e_solo;
    play_all(&pc_backend_hvl, h, &e_solo, 3);
    pc_vmute_clear(4);
    for (int i = 0; i < 4; i++) pc_vmute_set(i, PC_VM_MUTED, 4);
    double e_none;
    play_all(&pc_backend_hvl, h, &e_none, 3);
    CHECK(e_none == 0.0);
    CHECK(e_solo > 0.0);
    pc_backend_hvl.close(h);
    pc_vmute_reset();
    free(d);
}

static void test_subsongs_and_resume(void)
{
    uint32_t len;
    uint8_t *d = make_ahx(&len, 30, 2);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_hvl.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_hvl.track_count(h), 3);
    CHECK_EQ_INT(pc_backend_hvl.start_track(h, 2), 0);
    CHECK(pc_backend_hvl.start_track(h, 3) != 0);
    double e;
    CHECK(play_all(&pc_backend_hvl, h, &e, 100) > 20000);
    pc_backend_hvl.close(h);
    CHECK_EQ_INT(pc_backend_hvl.open(&h, &r, 7), 0);       /* a token past the end: starts at the beginning */
    CHECK_EQ_INT(pc_backend_hvl.byte_pos(h), 0);
    pc_backend_hvl.close(h);
    free(d);
}

/* ---- wave-6 review fixes (HIGH 4, 5; MED 10; LOW 14, 15, 16) ---- */
/* AHX with `posn` positions (all playing track 0) and one sub-song starting at position `sub0`; the waveform of its single plist entry is `wave` (0..7) */
static uint8_t *make_ahx_posn(uint32_t *len, int posn, int sub0, int wave, int speed_mult)
{
    uint8_t *b = calloc(1, 600);
    uint32_t o = 14;
    memcpy(b, "THX\x01", 4);
    b[6] = (uint8_t)((speed_mult - 1) << 5);
    b[7] = (uint8_t)posn;
    b[10] = 4; b[12] = 1; b[13] = 1;
    b[o++] = (uint8_t)(sub0 >> 8); b[o++] = (uint8_t)sub0;
    o += (uint32_t)posn * 8;                                                  /* every position: track 0 on the four voices */
    b[o] = (uint8_t)(24 << 2); b[o + 1] = 0x10; o += 3 + 9;
    uint32_t i0 = o;
    b[i0 + 0] = 64; b[i0 + 1] = 3;
    b[i0 + 2] = 1; b[i0 + 3] = 64; b[i0 + 4] = 1; b[i0 + 5] = 64; b[i0 + 6] = 100; b[i0 + 7] = 1;
    b[i0 + 20] = 1; b[i0 + 21] = 1;
    b[i0 + 22] = (uint8_t)((wave >> 1) & 3);                                  /* ple_Waveform = ((b0 << 1) & 6) | (b1 >> 7) */
    b[i0 + 23] = (uint8_t)((wave & 1) << 7);
    o += 26;
    b[4] = (uint8_t)(o >> 8); b[5] = (uint8_t)o;
    memcpy(b + o, "Test\0I\0", 7);
    o += 7;
    *len = o;
    return b;
}
/* a one-position HVL (4 channels); sub-song starts `subs`; the plist entry's waveform is the low 3 bits of its second byte */
static uint8_t *make_hvl(uint32_t *len, const uint16_t *subs, int nsubs, int wave)
{
    uint8_t *b = calloc(1, 600);
    uint32_t o = 16;
    memcpy(b, "HVL\x01", 4);
    b[7] = 1; b[10] = 4; b[12] = 1; b[13] = (uint8_t)nsubs; b[14] = 100; b[15] = 2;
    for (int i = 0; i < nsubs; i++) { b[o++] = (uint8_t)(subs[i] >> 8); b[o++] = (uint8_t)subs[i]; }
    o += 8;
    b[o] = 24; b[o + 1] = 1; o += 5;
    for (int k = 0; k < 3; k++) b[o++] = 0x3f;
    uint32_t i0 = o;
    b[i0] = 64; b[i0 + 1] = 3; b[i0 + 2] = 1; b[i0 + 3] = 64; b[i0 + 4] = 1; b[i0 + 5] = 64; b[i0 + 6] = 100; b[i0 + 7] = 1;
    b[i0 + 20] = 1; b[i0 + 21] = 1; b[i0 + 22] = 0; b[i0 + 23] = (uint8_t)wave;
    o += 22 + 5;
    b[4] = (uint8_t)(o >> 8); b[5] = (uint8_t)o;
    memcpy(b + o, "Test\0I\0", 7);
    o += 7;
    *len = o;
    return b;
}
static void open_play_all_subs(const uint8_t *d, uint32_t len)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    if (pc_backend_hvl.open(&h, &r, 0)) return;
    double e;
    for (int t = 0; t < pc_backend_hvl.track_count(h); t++) {
        if (t && pc_backend_hvl.start_track(h, t)) continue;
        play_all(&pc_backend_hvl, h, &e, 60);
    }
    pc_backend_hvl.close(h);
}

static void test_waveforms_and_subsong_starts(void)
{
    uint32_t len;
    for (int w = 0; w < 8; w++) {                         /* waveforms 5..7 indexed an empty table slot (NULL + offset: a SEGV) */
        uint8_t *d = make_ahx_posn(&len, 1, 0, w, 1);
        open_play_all_subs(d, len);
        free(d);
        d = make_hvl(&len, (uint16_t[]){ 0 }, 1, w);
        open_play_all_subs(d, len);
        free(d);
    }
    static const uint16_t bad_subs[][2] = { { 0x7fff, 0 }, { 0x9000, 0x8000 }, { 1, 0xffff } };
    for (unsigned i = 0; i < 3; i++) {                    /* HVL sub-song starts are not clamped by the format: PosNr 32767 / negative read ht_Positions[] out of range */
        uint8_t *d = make_hvl(&len, bad_subs[i], 2, 0);
        open_play_all_subs(d, len);
        free(d);
    }
}

static void test_sub_resume(void)
{
    uint32_t len;
    uint8_t *d = make_ahx_posn(&len, 4, 1, 1, 1);          /* 4 positions, the sub-song starts at position 1 */
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_hvl.open(&h, &r, 3), 0);       /* a resume at position 3, then audio_svc asks for the sub-track: the resume position must survive */
    CHECK_EQ_INT(pc_backend_hvl.start_track(h, 1), 0);
    CHECK_EQ_INT(pc_backend_hvl.byte_pos(h), 3);
    pc_backend_hvl.close(h);
    CHECK_EQ_INT(pc_backend_hvl.open(&h, &r, 0), 0);       /* a deliberate sub-track change later starts at the sub-song's own start */
    float out[2 * 441];
    CHECK(pc_backend_hvl.render(h, out, 441) > 0);
    CHECK_EQ_INT(pc_backend_hvl.start_track(h, 1), 0);
    CHECK(pc_backend_hvl.render(h, out, 441) > 0);
    CHECK_EQ_INT(pc_backend_hvl.byte_pos(h), 1);
    pc_backend_hvl.close(h);
    free(d);
}

/* a crossfade renders two handles in one block; the one that does not hold the voice snapshot must not take it over every block (LOW 13): the
 * mute/solo state was wiped twice a block, the meters flickered */
static void test_crossfade_keeps_snapshot(void)
{
    uint32_t len;
    uint8_t *d = make_ahx(&len, 24, 0);
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *a, *b;
    CHECK_EQ_INT(pc_backend_hvl.open(&a, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_hvl.open(&b, &r, 0), 0);
    float out[2 * 441];
    CHECK(pc_backend_hvl.render(a, out, 441) > 0);          /* a takes the snapshot */
    pc_vmute_sync();
    pc_vmute_set(1, PC_VM_MUTED, 4);
    const uint32_t mask = pc_vmute_mask(), epoch = pc_vmute_epoch();
    CHECK(mask != 0);
    for (int i = 0; i < 10; i++) {                           /* the fade: both render in every block */
        CHECK(pc_backend_hvl.render(a, out, 441) > 0);
        CHECK(pc_backend_hvl.render(b, out, 441) > 0);
    }
    CHECK_EQ_INT(pc_vmute_mask(), mask);
    CHECK_EQ_INT(pc_vmute_epoch(), epoch);
    pc_voices_t v;
    CHECK_EQ_INT(pc_extvoice_read(&v), 4);
    pc_backend_hvl.close(a);                                 /* the old track closes: b can take it over on its next block */
    CHECK(pc_backend_hvl.render(b, out, 441) > 0);
    CHECK_EQ_INT(pc_extvoice_read(&v), 4);
    CHECK(pc_vmute_epoch() != epoch);                        /* a new source: reset */
    pc_backend_hvl.close(b);
    pc_vmute_reset();
    free(d);
}

static void test_speed4_frame(void)
{
    uint32_t len;
    uint8_t *d = make_ahx_posn(&len, 1, 0, 1, 4);          /* speed multiplier 4: 4 x 220 = 880 samples a frame, not 882 (two stale samples, a 100 Hz click) */
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    CHECK_EQ_INT(pc_backend_hvl.open(&h, &r, 0), 0);
    double e;
    int n = play_all(&pc_backend_hvl, h, &e, 200);
    CHECK(n > 0 && n % 880 == 0);
    pc_backend_hvl.close(h);
    free(d);
}

/* the wave tables are shared by every loaded tune and counted: aud_pre, the UI probe and the audio task acquire/release at the same time */
extern int8 *waves;
static void *table_hammer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 3000; i++) {
        CHECK(hvl_TablesAcquire());
        volatile int8_t x = waves[(i * 37) % 4096];
        (void)x;
        hvl_TablesRelease();
    }
    return NULL;
}
static void test_tables_threads(void)
{
    pthread_t t[4];
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, table_hammer, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    CHECK(waves == NULL);                                  /* the last release freed them */
}

static void try_open_play(const uint8_t *d, uint32_t len)
{
    pc_reader_t r;
    pc_reader_mem(&r, d, len);
    void *h;
    if (pc_backend_hvl.open(&h, &r, 0)) return;
    double e;
    play_all(&pc_backend_hvl, h, &e, 40);
    pc_backend_hvl.close(h);
}

static void test_hostile(void)
{
    uint32_t len;
    uint8_t *d = make_ahx(&len, 24, 1);
    for (uint32_t cut = 0; cut < len; cut++) {            /* every truncation: a heap copy of exactly that size, so ASan sees any read past it */
        uint8_t *c = malloc(cut ? cut : 1);
        memcpy(c, d, cut);
        try_open_play(c, cut);
        free(c);
    }
    srand(7);
    for (int k = 0; k < 600; k++) {                       /* random byte flips in the header and body */
        uint8_t *c = malloc(len);
        memcpy(c, d, len);
        int flips = 1 + rand() % 4;
        for (int i = 0; i < flips; i++) c[rand() % (int)(k % 3 ? len : 16)] = (uint8_t)rand();
        try_open_play(c, len);
        free(c);
    }
    free(d);
    pc_vmute_reset();
}

#ifdef CHIPTEST_DIR
static void play_file(const char *rel, int min_blocks_energy)
{
    char path[512];
    snprintf(path, sizeof path, CHIPTEST_DIR "/%s", rel);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("skip %s (not there)\n", rel); return; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)n);
    CHECK(fread(d, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    pc_reader_t r;
    pc_reader_mem(&r, d, (uint32_t)n);
    void *h;
    CHECK_EQ_INT(pc_backend_hvl.open(&h, &r, 0), 0);
    double e;
    int got = play_all(&pc_backend_hvl, h, &e, 300);
    printf("%s: %d frames, length %d ms, energy %.1f\n", rel, got, (int)pc_backend_hvl.length_ms(h), e);
    CHECK(got > 4410);
    CHECK(e > (double)min_blocks_energy);
    pc_backend_hvl.close(h);
    free(d);
}
static void test_files(void) { play_file("hvl/voices.ahx", 1); play_file("hvl/voices.hvl", 1); }
#else
static void test_files(void) {}
#endif

int main(void)
{
    RUN(test_play);
    RUN(test_waveforms_and_subsong_starts);
    RUN(test_sub_resume);
    RUN(test_crossfade_keeps_snapshot);
    RUN(test_speed4_frame);
    RUN(test_tables_threads);
    RUN(test_tags);
    RUN(test_mute);
    RUN(test_subsongs_and_resume);
    RUN(test_hostile);
    RUN(test_files);
    return 0;
}
