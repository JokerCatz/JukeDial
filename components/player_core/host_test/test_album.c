// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdint.h>
#include "pc/pc_album.h"
#include "pc_test.h"

static void test_photo_names(void)
{
    CHECK(pc_album_is_photo("a.jpg"));
    CHECK(pc_album_is_photo("IMG_0001.JPG"));
    CHECK(pc_album_is_photo("x.Jpeg"));
    CHECK(!pc_album_is_photo("a.png"));
    CHECK(!pc_album_is_photo("jpg"));
    CHECK(!pc_album_is_photo("._a.jpg"));          /* macOS resource forks */
    CHECK(!pc_album_is_photo(".hidden.jpg"));
    CHECK(!pc_album_is_photo(NULL));
}

static void test_key_and_name(void)
{
    uint64_t k = pc_album_key("/sdcard/photos/a.jpg", 1234, 1700000000);
    CHECK(k == pc_album_key("/sdcard/photos/a.jpg", 1234, 1700000000));
    CHECK(k != pc_album_key("/sdcard/photos/b.jpg", 1234, 1700000000));
    CHECK(k != pc_album_key("/sdcard/photos/a.jpg", 1235, 1700000000));
    CHECK(k != pc_album_key("/sdcard/photos/a.jpg", 1234, 1700000001));
    char n[PC_ALBUM_NAME_MAX];
    pc_album_cache_name(0x0123456789abcdefULL, n);
    CHECK_STR(n, "0123456789abcdef.565");
    uint64_t back = 0;
    CHECK(pc_album_parse_name(n, &back));
    CHECK(back == 0x0123456789abcdefULL);
    CHECK(!pc_album_parse_name("0123456789abcdef.tmp", &back));
    CHECK(!pc_album_parse_name("0123.565", &back));
    CHECK(!pc_album_parse_name("0123456789abcdeg.565", &back));
}

static void test_stale(void)
{
    uint64_t keys[3] = { 30, 10, 20 };
    pc_album_sort_keys(keys, 3);
    CHECK(keys[0] == 10 && keys[2] == 30);
    char n[PC_ALBUM_NAME_MAX];
    pc_album_cache_name(20, n);
    CHECK(!pc_album_stale(n, keys, 3));
    pc_album_cache_name(21, n);
    CHECK(pc_album_stale(n, keys, 3));            /* its photo is gone (or changed) */
    CHECK(pc_album_stale("junk.565", keys, 3));
    CHECK(pc_album_stale("0000000000000014.tmp", keys, 3));   /* a half-written file */
    pc_album_cache_name(20, n);
    CHECK(pc_album_stale(n, keys, 0));
}

/* JPEG with an APP1 Exif block holding one IFD0 entry: Orientation = o */
static size_t exif_jpeg(uint8_t *b, int o, int big_endian)
{
    size_t i = 0;
    b[i++] = 0xFF; b[i++] = 0xD8;
    b[i++] = 0xFF; b[i++] = 0xE0; b[i++] = 0; b[i++] = 4; b[i++] = 'J'; b[i++] = 'F';   /* an APP0 to skip first */
    b[i++] = 0xFF; b[i++] = 0xE1;
    size_t len_at = i;
    i += 2;
    memcpy(b + i, "Exif\0\0", 6); i += 6;
    size_t t = i;
    if (big_endian) { b[i++] = 'M'; b[i++] = 'M'; b[i++] = 0; b[i++] = 42; b[i++] = 0; b[i++] = 0; b[i++] = 0; b[i++] = 8; }
    else { b[i++] = 'I'; b[i++] = 'I'; b[i++] = 42; b[i++] = 0; b[i++] = 8; b[i++] = 0; b[i++] = 0; b[i++] = 0; }
    (void)t;
    uint8_t e[] = { 0, 2, 0x01, 0x0F, 0, 2, 0, 0, 0, 2, 'X', 0, 0, 0,                 /* count 2: tag 0x010F Make (ASCII) */
                    0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, (uint8_t)o, 0, 0 };              /* Orientation, SHORT, 1 */
    if (!big_endian) {                                    /* little-endian: swap every 16/32-bit field */
        uint8_t le[] = { 2, 0, 0x0F, 0x01, 2, 0, 2, 0, 0, 0, 'X', 0, 0, 0,
                         0x12, 0x01, 3, 0, 1, 0, 0, 0, (uint8_t)o, 0, 0, 0 };
        memcpy(e, le, sizeof e);
    }
    memcpy(b + i, e, sizeof e); i += sizeof e;
    b[i++] = 0; b[i++] = 0; b[i++] = 0; b[i++] = 0;      /* next IFD */
    size_t len = i - len_at;
    b[len_at] = (uint8_t)(len >> 8); b[len_at + 1] = (uint8_t)len;
    b[i++] = 0xFF; b[i++] = 0xDA;                        /* start of scan */
    return i;
}

static void test_exif(void)
{
    uint8_t b[128];
    for (int o = 1; o <= 8; o++) {
        CHECK_EQ_INT(pc_exif_orientation(b, exif_jpeg(b, o, 1)), o);
        CHECK_EQ_INT(pc_exif_orientation(b, exif_jpeg(b, o, 0)), o);
    }
    CHECK_EQ_INT(pc_exif_orientation(b, exif_jpeg(b, 9, 1)), 1);     /* nonsense -> as stored */
    size_t n = exif_jpeg(b, 6, 1);
    for (size_t cut = 0; cut < n - 2; cut++) {                        /* truncated: never reads past, 1 or 6 */
        int o = pc_exif_orientation(b, cut);
        CHECK(o == 1 || o == 6);
    }
    const uint8_t plain[] = { 0xFF, 0xD8, 0xFF, 0xDB, 0, 4, 1, 2, 0xFF, 0xDA };
    CHECK_EQ_INT(pc_exif_orientation(plain, sizeof plain), 1);
    CHECK_EQ_INT(pc_exif_orientation(NULL, 0), 1);
}

static void test_jpeg_size(void)
{
    uint8_t b[160];
    size_t n = exif_jpeg(b, 1, 1) - 2;                   /* drop the SOS, put a SOF0 there */
    const uint8_t sof[] = { 0xFF, 0xC4, 0, 4, 0, 0,      /* a DHT (C4 is not a SOF) first */
                            0xFF, 0xC0, 0, 17, 8, 0x0B, 0xB8, 0x0F, 0xA0, 3, 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1, 0xFF, 0xDA };
    memcpy(b + n, sof, sizeof sof);
    n += sizeof sof;
    int w = 0, h = 0;
    CHECK(pc_jpeg_size(b, n, &w, &h));
    CHECK_EQ_INT(w, 4000); CHECK_EQ_INT(h, 3000);
    CHECK(!pc_jpeg_size(b, 20, &w, &h));
    const uint8_t png[] = { 0x89, 'P', 'N', 'G' };
    CHECK(!pc_jpeg_size(png, sizeof png, &w, &h));
    CHECK_EQ_INT(b[n - 20], 0xC0);
    b[n - 20] = 0xC2;                                    /* progressive: the size is still read */
    CHECK(pc_jpeg_size(b, n, &w, &h));
}

static void test_decode_size(void)
{
    int w, h;
    pc_album_decode_size(1920, 1080, 360, &w, &h);
    CHECK_EQ_INT(w, 640); CHECK_EQ_INT(h, 360);
    pc_album_decode_size(4000, 3000, 360, &w, &h);       /* more than 8x: the decoder stops at 1/8 */
    CHECK_EQ_INT(w, 504); CHECK_EQ_INT(h, 376);
    pc_album_decode_size(300, 200, 360, &w, &h);         /* small: as it is (8-aligned), scaled up later */
    CHECK_EQ_INT(w, 296); CHECK_EQ_INT(h, 200);
    pc_album_decode_size(3024, 4032, 360, &w, &h);       /* portrait phone photo */
    CHECK(w >= 360 && h >= 360 && w % 8 == 0 && h % 8 == 0 && w <= 3024 && h <= 4032);
}

static void test_oriented_size_and_cover(void)
{
    int w, h;
    pc_album_oriented_size(1, 400, 300, &w, &h); CHECK_EQ_INT(w, 400); CHECK_EQ_INT(h, 300);
    pc_album_oriented_size(6, 400, 300, &w, &h); CHECK_EQ_INT(w, 300); CHECK_EQ_INT(h, 400);
    pc_album_oriented_size(8, 400, 300, &w, &h); CHECK_EQ_INT(w, 300); CHECK_EQ_INT(h, 400);
    pc_album_rect_t r;
    pc_album_cover(400, 300, &r);
    CHECK_EQ_INT(r.x, 50); CHECK_EQ_INT(r.y, 0); CHECK_EQ_INT(r.w, 300); CHECK_EQ_INT(r.h, 300);
    pc_album_cover(300, 500, &r);
    CHECK_EQ_INT(r.x, 0); CHECK_EQ_INT(r.y, 100); CHECK_EQ_INT(r.w, 300);
}

/* 6 x 4 source, pixel = y * 6 + x; rendered at 4 x 4 (the centred square, no scaling) */
static uint16_t at(const uint16_t *d, int out, int x, int y) { return d[y * out + x]; }
static void test_render_orientations(void)
{
    uint16_t src[24], dst[16];
    for (int i = 0; i < 24; i++) src[i] = (uint16_t)i;
    pc_album_render(src, 6, 4, 1, dst, 4);               /* crop x 1..4 */
    CHECK_EQ_INT(at(dst, 4, 0, 0), 1);  CHECK_EQ_INT(at(dst, 4, 3, 0), 4);  CHECK_EQ_INT(at(dst, 4, 0, 3), 19);
    pc_album_render(src, 6, 4, 3, dst, 4);               /* 180 */
    CHECK_EQ_INT(at(dst, 4, 0, 0), 22); CHECK_EQ_INT(at(dst, 4, 3, 3), 1);
    pc_album_render(src, 6, 4, 2, dst, 4);               /* mirrored */
    CHECK_EQ_INT(at(dst, 4, 0, 0), 4);  CHECK_EQ_INT(at(dst, 4, 3, 0), 1);
    pc_album_render(src, 6, 4, 4, dst, 4);               /* flipped */
    CHECK_EQ_INT(at(dst, 4, 0, 0), 19);
    /* 6: displayed 4 wide x 6 tall, crop rows 1..4 of it; displayed (u, v) = stored (v, H-1-u) */
    pc_album_render(src, 6, 4, 6, dst, 4);
    CHECK_EQ_INT(at(dst, 4, 0, 0), 3 * 6 + 1);            /* u=0, v=1 -> x=1, y=3 */
    CHECK_EQ_INT(at(dst, 4, 3, 0), 0 * 6 + 1);            /* u=3, v=1 -> x=1, y=0 */
    pc_album_render(src, 6, 4, 8, dst, 4);               /* displayed (u, v) = stored (W-1-v, u) */
    CHECK_EQ_INT(at(dst, 4, 0, 0), 0 * 6 + 4);            /* u=0, v=1 -> x=4, y=0 */
    CHECK_EQ_INT(at(dst, 4, 0, 3), 0 * 6 + 1);            /* v=4 -> x=1 */
    pc_album_render(src, 6, 4, 5, dst, 4);               /* transpose: (v, u) */
    CHECK_EQ_INT(at(dst, 4, 2, 0), 2 * 6 + 1);
    pc_album_render(src, 6, 4, 7, dst, 4);               /* transverse: (W-1-v, H-1-u) */
    CHECK_EQ_INT(at(dst, 4, 0, 0), 3 * 6 + 4);
}

static void test_render_scales_flat_colour(void)
{
    static uint16_t src[100 * 60], dst[36 * 36];
    for (int i = 0; i < 100 * 60; i++) src[i] = 0xF800;  /* red stays red when scaled (both ways) */
    pc_album_render(src, 100, 60, 1, dst, 36);
    for (int i = 0; i < 36 * 36; i++) CHECK_EQ_INT(dst[i], 0xF800);
    static uint16_t big[36 * 36];
    pc_album_render(src, 20, 12, 1, big, 36);
    for (int i = 0; i < 36 * 36; i++) CHECK_EQ_INT(big[i], 0xF800);
}

static void test_seq_in_order(void)
{
    uint16_t buf[5];
    pc_album_seq_t s;
    pc_album_seq_init(&s, buf, 5, false, 1);
    for (int round = 0; round < 2; round++)
        for (int i = 0; i < 5; i++) CHECK_EQ_INT(pc_album_seq_next(&s), i);
    pc_album_seq_init(&s, buf, 0, false, 1);
    CHECK_EQ_INT(pc_album_seq_next(&s), -1);
    pc_album_seq_init(&s, buf, 1, true, 1);
    CHECK_EQ_INT(pc_album_seq_next(&s), 0);
    CHECK_EQ_INT(pc_album_seq_next(&s), 0);
}

static void test_seq_shuffle(void)
{
    uint16_t buf[20];
    pc_album_seq_t s;
    for (uint32_t seed = 1; seed < 50; seed++) {
        pc_album_seq_init(&s, buf, 20, true, seed);
        int prev = -1, inorder = 1;
        for (int round = 0; round < 4; round++) {
            int seen[20] = { 0 };
            for (int i = 0; i < 20; i++) {
                int k = pc_album_seq_next(&s);
                CHECK(k >= 0 && k < 20);
                CHECK(k != prev);                       /* never the same photo twice in a row, even across rounds */
                seen[k]++;
                if (k != i) inorder = 0;
                prev = k;
            }
            for (int i = 0; i < 20; i++) CHECK_EQ_INT(seen[i], 1);   /* every photo once per round */
        }
        CHECK(!inorder);
    }
}

int main(void)
{
    RUN(test_photo_names);
    RUN(test_key_and_name);
    RUN(test_stale);
    RUN(test_exif);
    RUN(test_jpeg_size);
    RUN(test_decode_size);
    RUN(test_oriented_size_and_cover);
    RUN(test_render_orientations);
    RUN(test_render_scales_flat_colour);
    RUN(test_seq_in_order);
    RUN(test_seq_shuffle);
    return 0;
}
