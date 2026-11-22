// SPDX-License-Identifier: LGPL-2.1-or-later
/* PICO-8 cart backend: carts are built in memory (text, and a PNG whose pixels carry the cart in their low bits, with stored-deflate
 * blocks), so no cart is committed. Sequencing, pitch, effects, hostile input under ASan; the carts of .local/chiptest are played too when present. */
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc/pc_id3.h"
#include "pc/pc_extvoice.h"
#include "pc/pc_vmute.h"
#include "pc_test.h"

static char *mk_text(int speed, int key, int inst, int fx, const char *music)
{
    char *s = calloc(1, 8192);
    int o = sprintf(s, "pico-8 cartridge // http://www.pico-8.com\nversion 41\n__lua__\nfunction _init() while true do end end\n__sfx__\n");
    o += sprintf(s + o, "00%02x0000", speed);                          /* filters 00, speed, loop 0 .. 0 */
    for (int k = 0; k < 32; k++) o += sprintf(s + o, "%02x%x5%x", key, inst, fx);
    o += sprintf(s + o, "\n__music__\n%s\n", music);
    return s;
}

static double play_all(const pc_backend_t *be, void *h, int *frames, int max_blocks)
{
    float out[2 * 441];
    double e = 0;
    *frames = 0;
    for (int b = 0; b < max_blocks; b++) {
        int g = be->render(h, out, 441);
        if (g <= 0) break;
        for (int i = 0; i < 2 * g; i++) { CHECK(out[i] >= -1.0f && out[i] <= 1.0f); e += (double)out[i] * out[i]; }
        *frames += g;
    }
    return e;
}

static void test_text(void)
{
    char *s = mk_text(4, 36, 0, 0, "04 00404040");              /* one pattern, channel 0 plays sfx 0, stop flag */
    pc_reader_t r;
    pc_reader_mem(&r, s, (uint32_t)strlen(s));
    void *h;
    CHECK_EQ_INT(pc_backend_p8.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_p8.rate(h), 44100);
    CHECK_EQ_INT(pc_backend_p8.track_count(h), 1);
    int want = 32 * 4 * 183 * 1000 / 22050;                      /* 1062 ms */
    CHECK(abs((int)pc_backend_p8.length_ms(h) - want) <= 2);
    float out[2 * 441];
    CHECK(pc_backend_p8.render(h, out, 441) > 0);
    pc_voices_t v;
    CHECK_EQ_INT(pc_extvoice_read(&v), 4);
    CHECK(v.flags[0] & PC_VF_PITCH);
    CHECK_NEAR(v.cents[0], 7200, 60);                            /* key 36 = 523 Hz = C5 */
    CHECK(v.level[0] > 0);
    CHECK_EQ_INT(v.cents[1], PC_VOICE_NOPITCH);
    int frames;
    double e = play_all(&pc_backend_p8, h, &frames, 1000);
    CHECK(e > 10.0);
    CHECK(abs(frames + 441 - want * 441 / 10) < 1500);           /* the song ends when the pattern does */
    CHECK(pc_extvoice_read(&v) == 4 && v.strikes[0] >= 25);      /* a strike for every note */
    pc_backend_p8.close(h);
    CHECK_EQ_INT(pc_extvoice_read(&v), 0);
    free(s);
}

static void test_tags(void)
{
    char *s = mk_text(4, 36, 0, 0, "04 00404040");
    char *lua = strstr(s, "function _init");
    memmove(lua + 26, lua, strlen(lua) + 1);
    memcpy(lua, "-- Night Drive\n-- by Ada\n", 25);
    lua[25] = '\n';
    pc_reader_t r;
    pc_reader_mem(&r, s, (uint32_t)strlen(s));
    pc_id3_t t;
    memset(&t, 0, sizeof t);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "Night Drive");
    CHECK_STR(t.artist, "Ada");
    pc_id3_free(&t);
    free(s);
}

static void test_mute_and_effects(void)
{
    for (int fx = 0; fx < 8; fx++) {                             /* every effect renders without leaving [-1, 1] */
        char *s = mk_text(2, 30, fx == 5 ? 6 : fx % 8, fx, "04 00404040");
        pc_reader_t r;
        pc_reader_mem(&r, s, (uint32_t)strlen(s));
        void *h;
        CHECK_EQ_INT(pc_backend_p8.open(&h, &r, 0), 0);
        int frames;
        CHECK(play_all(&pc_backend_p8, h, &frames, 1000) > 0.0);
        pc_backend_p8.close(h);
        free(s);
    }
    char *s = mk_text(2, 30, 3, 0, "04 00404040");
    pc_reader_t r;
    pc_reader_mem(&r, s, (uint32_t)strlen(s));
    void *h;
    CHECK_EQ_INT(pc_backend_p8.open(&h, &r, 0), 0);
    float out[2 * 441];
    CHECK(pc_backend_p8.render(h, out, 441) > 0);
    pc_vmute_sync();
    pc_vmute_set(0, PC_VM_MUTED, 4);
    CHECK(pc_backend_p8.render(h, out, 441) > 0);            /* the block the mask lands in (and the interpolation's last sample) */
    int frames;
    CHECK(play_all(&pc_backend_p8, h, &frames, 1000) == 0.0);
    CHECK(frames > 1000);
    pc_backend_p8.close(h);
    pc_vmute_reset();
    free(s);
}

static void test_sequence_and_subtracks(void)
{
    /* patterns: 0,1 play (1 stops); 2 empty; 3 plays and loops back (the cart's loop) -> two sequences, both end */
    char *s = mk_text(1, 40, 3, 0, "00 00404040\n04 00404040\n00 40404040\n02 00404040");
    pc_reader_t r;
    pc_reader_mem(&r, s, (uint32_t)strlen(s));
    void *h;
    CHECK_EQ_INT(pc_backend_p8.open(&h, &r, 0), 0);
    CHECK_EQ_INT(pc_backend_p8.track_count(h), 2);
    int per = 32 * 183 * 1000 / 22050;                           /* one pattern: 265 ms */
    CHECK(abs((int)pc_backend_p8.length_ms(h) - 2 * per) <= 3);
    int frames;
    play_all(&pc_backend_p8, h, &frames, 1000);
    CHECK(abs(frames - 2 * per * 44) < 2500);
    CHECK_EQ_INT(pc_backend_p8.start_track(h, 1), 0);
    CHECK(abs((int)pc_backend_p8.length_ms(h) - per) <= 3);
    play_all(&pc_backend_p8, h, &frames, 1000);
    CHECK(abs(frames - per * 44) < 2500);
    CHECK(pc_backend_p8.start_track(h, 2) != 0);
    pc_backend_p8.close(h);
    CHECK_EQ_INT(pc_backend_p8.open(&h, &r, 3), 0);              /* a resume token = a pattern */
    CHECK_EQ_INT(pc_backend_p8.byte_pos(h), 3);
    pc_backend_p8.close(h);
    free(s);
}

/* a PNG with the cart bytes in the low bits (stored deflate blocks, filter 0) */
static uint8_t *mk_png(const uint8_t *cart, uint32_t cart_len, uint32_t *len)
{
    const int W = 160, H = 205, stride = W * 4 + 1;
    uint8_t *raw = calloc(1, (size_t)H * stride);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        uint32_t i = (uint32_t)(y * W + x);
        uint8_t b = i < cart_len ? cart[i] : 0;
        uint8_t *px = raw + (size_t)y * stride + 1 + x * 4;
        px[0] = (uint8_t)(0x40 | (b >> 4 & 3)); px[1] = (uint8_t)(0x80 | (b >> 2 & 3)); px[2] = (uint8_t)(0xC0 | (b & 3)); px[3] = (uint8_t)(0xFC | (b >> 6 & 3));
    }
    size_t rl = (size_t)H * stride, zl = 2 + (rl / 65535 + 1) * 5 + rl + 4;
    uint8_t *z = calloc(1, zl);
    size_t zo = 0, ro = 0;
    z[zo++] = 0x78; z[zo++] = 0x01;
    while (ro < rl) {
        size_t n = rl - ro > 65535 ? 65535 : rl - ro;
        z[zo++] = (ro + n == rl); z[zo++] = (uint8_t)n; z[zo++] = (uint8_t)(n >> 8); z[zo++] = (uint8_t)~n; z[zo++] = (uint8_t)(~n >> 8);
        memcpy(z + zo, raw + ro, n); zo += n; ro += n;
    }
    zo += 4;                                                     /* Adler-32 (not checked) */
    uint8_t *f = calloc(1, zo + 100);
    size_t o = 0;
    memcpy(f, "\x89PNG\r\n\x1a\n", 8); o = 8;
    uint8_t ih[13] = { 0, 0, 0, W, 0, 0, 0, H, 8, 6, 0, 0, 0 };
    #define CHUNK(tag, data, n) do { uint32_t _n = (uint32_t)(n); f[o++] = (uint8_t)(_n >> 24); f[o++] = (uint8_t)(_n >> 16); f[o++] = (uint8_t)(_n >> 8); f[o++] = (uint8_t)_n; \
        memcpy(f + o, tag, 4); o += 4; memcpy(f + o, data, _n); o += _n; o += 4; } while (0)
    CHUNK("IHDR", ih, 13);
    CHUNK("IDAT", z, zo);
    CHUNK("IEND", "", 0);
    free(raw); free(z);
    *len = (uint32_t)o;
    return f;
}

static void test_png(void)
{
    /* the cart memory 0x3100..0x42ff: one pattern (channel 0 -> sfx 0, stop), sfx 0 = 32 notes of key 36, instrument 3, volume 5, speed 2 */
    uint8_t cart[0x4300];
    memset(cart, 0, sizeof cart);
    for (int i = 0; i < 64; i++) memset(cart + 0x3100 + 4 * i, 0x40, 4);
    cart[0x3100] = 0x00 | 0x80 * 0; cart[0x3102] |= 0x80;      /* pattern 0: channel 0 = sfx 0 (enabled), stop flag on d[2] */
    cart[0x3101] = cart[0x3102] = cart[0x3103] = 0x40; cart[0x3102] |= 0x80;
    for (int k = 0; k < 32; k++) { unsigned w = 36u | 3u << 6 | 5u << 9; cart[0x3200 + 2 * k] = (uint8_t)w; cart[0x3200 + 2 * k + 1] = (uint8_t)(w >> 8); }
    cart[0x3200 + 65] = 2;
    uint32_t len;
    uint8_t *png = mk_png(cart, sizeof cart, &len);
    pc_reader_t r;
    pc_reader_mem(&r, png, len);
    void *h;
    CHECK_EQ_INT(pc_backend_p8.open(&h, &r, 0), 0);
    CHECK(abs((int)pc_backend_p8.length_ms(h) - 32 * 2 * 183 * 1000 / 22050) <= 2);
    int frames;
    CHECK(play_all(&pc_backend_p8, h, &frames, 1000) > 5.0);
    pc_backend_p8.close(h);
    for (uint32_t cut = 0; cut < len; cut += 97) {               /* truncations */
        uint8_t *c = malloc(cut ? cut : 1);
        memcpy(c, png, cut);
        pc_reader_mem(&r, c, cut);
        if (!pc_backend_p8.open(&h, &r, 0)) { play_all(&pc_backend_p8, h, &frames, 20); pc_backend_p8.close(h); }
        free(c);
    }
    pc_reader_mem(&r, png, len);
    uint8_t *c = malloc(len);                                    /* a PNG of another size or colour type is refused */
    memcpy(c, png, len);
    c[8 + 8 + 3] = 161;
    pc_reader_mem(&r, c, len);
    CHECK(pc_backend_p8.open(&h, &r, 0) != 0);
    free(c);
    free(png);
}

static void test_hostile(void)
{
    char *s = mk_text(3, 40, 6, 2, "00 00404040\n04 01404040");
    uint32_t n = (uint32_t)strlen(s);
    srand(11);
    for (int k = 0; k < 400; k++) {
        uint8_t *c = malloc(n);
        memcpy(c, s, n);
        int flips = 1 + rand() % 5;
        for (int i = 0; i < flips; i++) c[rand() % (int)n] = (uint8_t)rand();
        pc_reader_t r;
        pc_reader_mem(&r, c, n - (uint32_t)(k % 7 == 0 ? rand() % 100 : 0));
        void *h;
        if (!pc_backend_p8.open(&h, &r, (uint32_t)rand())) { int fr; play_all(&pc_backend_p8, h, &fr, 60); pc_backend_p8.close(h); }
        free(c);
    }
    free(s);
    pc_vmute_reset();
}

#ifdef CHIPTEST_DIR
static uint8_t *slurp(const char *rel, uint32_t *n)
{
    char path[512];
    snprintf(path, sizeof path, CHIPTEST_DIR "/%s", rel);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)sz);
    CHECK(fread(d, 1, (size_t)sz, f) == (size_t)sz);
    fclose(f);
    *n = (uint32_t)sz;
    return d;
}
static double play_file(const char *rel, int *frames)
{
    uint32_t n;
    uint8_t *d = slurp(rel, &n);
    if (!d) { printf("skip %s\n", rel); *frames = -1; return 0; }
    pc_reader_t r;
    pc_reader_mem(&r, d, n);
    void *h;
    int rc = pc_backend_p8.open(&h, &r, 0);
    double e = 0;
    *frames = 0;
    if (!rc) {
        e = play_all(&pc_backend_p8, h, frames, 6000);
        printf("%s: %d tracks, length %d ms, %d frames, energy %.1f\n", rel, pc_backend_p8.track_count(h), (int)pc_backend_p8.length_ms(h), *frames, e);
        pc_backend_p8.close(h);
    } else printf("%s: refused\n", rel);
    free(d);
    return e;
}
static void test_files(void)
{
    int f1, f2;
    double e1 = play_file("pico8/songs.p8", &f1), e2 = play_file("pico8/songs.p8.png", &f2);
    if (f1 >= 0 && f2 >= 0) { CHECK_EQ_INT(f1, f2); CHECK_NEAR(e1, e2, 1e-6 * (1 + e1)); }   /* the PNG carries the same cart */
    play_file("pico8/sfx.p8", &f1);
    static const char *const carts[] = { "Bas", "Cattle Crisis", "Cryomancer", "Exterra_stage", "Fuz", "Neath 1.01", "Outvain1", "Pizza Panda", "Praxis Fighter X" };
    for (size_t i = 0; i < sizeof carts / sizeof *carts; i++) {
        char rel[128];
        snprintf(rel, sizeof rel, "pico8/carts/%s.p8.png", carts[i]);
        play_file(rel, &f1);
    }
}
#else
static void test_files(void) {}
#endif

int main(void)
{
    RUN(test_text);
    RUN(test_tags);
    RUN(test_mute_and_effects);
    RUN(test_sequence_and_subtracks);
    RUN(test_png);
    RUN(test_hostile);
    RUN(test_files);
    return 0;
}
