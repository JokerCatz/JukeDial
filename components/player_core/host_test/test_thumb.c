// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_thumb.h"
#include "pc_test.h"

#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F

static void test_crop_center(void)
{
    int w = 640, h = 360;
    uint16_t *src = malloc((size_t)w * h * 2), *dst = malloc(240 * 240 * 2);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) src[y * w + x] = x < 140 ? RED : x >= 500 ? BLUE : GREEN;   /* centre square is x 140..499 */
    pc_thumb_crop_scale(src, w, h, dst, 240);
    CHECK_EQ_INT(dst[0], GREEN);
    CHECK_EQ_INT(dst[239], GREEN);
    CHECK_EQ_INT(dst[239 * 240], GREEN);
    CHECK_EQ_INT(dst[240 * 240 - 1], GREEN);
    free(src);
    free(dst);
}

static void test_crop_portrait(void)
{
    int w = 100, h = 300;   /* tall: crop rows, keep all columns */
    uint16_t *src = malloc((size_t)w * h * 2), dst[50 * 50];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) src[y * w + x] = y < 100 ? RED : y >= 200 ? BLUE : GREEN;
    pc_thumb_crop_scale(src, w, h, dst, 50);
    for (int i = 0; i < 50 * 50; i++) CHECK_EQ_INT(dst[i], GREEN);
    free(src);
}

static void test_thumb_hash(void)
{
    CHECK_EQ_INT(pc_thumb_hash("abc"), pc_thumb_hash("abc"));
    CHECK(pc_thumb_hash("abc") != pc_thumb_hash("abd"));
    CHECK_EQ_INT(pc_thumb_hash(""), 2166136261u);
}

/* hires T8c: a pack entry's key. Embedded picture: path + APIC offset + length (a re-tagged file is a new key). Image file: its path, size and
 * mtime (one entry for every track that resolves to the same folder image). The two never collide by construction (separate domains). */
static void test_thumb_key(void)
{
    uint64_t k = pc_thumb_key("/sdcard/export/A/1.mp3", 100, 2000);
    CHECK(k == pc_thumb_key("/sdcard/export/A/1.mp3", 100, 2000));
    CHECK(k != pc_thumb_key("/sdcard/export/A/2.mp3", 100, 2000));
    CHECK(k != pc_thumb_key("/sdcard/export/A/1.mp3", 101, 2000));
    CHECK(k != pc_thumb_key("/sdcard/export/A/1.mp3", 100, 2001));
    CHECK(pc_thumb_key("ab", 0x63, 0) != pc_thumb_key("abc", 0, 0));   /* off and len are hashed as 4 fixed bytes each, after the path */
    CHECK(k != 0);
    uint64_t i = pc_thumb_image_key("/sdcard/export/A/cover.jpg", 5000, 1700000000);
    CHECK(i == pc_thumb_image_key("/sdcard/export/A/cover.jpg", 5000, 1700000000));
    CHECK(i != pc_thumb_image_key("/sdcard/export/A/cover.jpg", 5001, 1700000000));
    CHECK(i != pc_thumb_image_key("/sdcard/export/A/cover.jpg", 5000, 1700000001));
    CHECK(i != pc_thumb_image_key("/sdcard/export/B/cover.jpg", 5000, 1700000000));
    CHECK(pc_thumb_image_key("/a", 0, 0) != pc_thumb_key("/a", 0, 0));
}

/* the cover of a track without an embedded picture: <track>.jpg/.jpeg(/.png) next to it, else cover / folder .jpg/.jpeg(/.png) in its folder,
 * any case (FAT names); PNG only when the build decodes PNG */
static void test_thumb_pick_image(void)
{
    const char *names[] = { "Folder.JPG", "02 Song.jpeg", "notes.txt", "01 Song.png", "COVER.jpg", "03 x.jpg" };
    bool shared = true;
    CHECK_EQ_INT(pc_thumb_pick_image("02 Song.flac", names, 6, true, &shared), 1);
    CHECK(!shared);
    CHECK_EQ_INT(pc_thumb_pick_image("01 Song.mp3", names, 6, true, &shared), 3);
    CHECK(!shared);
    CHECK_EQ_INT(pc_thumb_pick_image("01 Song.mp3", names, 6, false, &shared), 4);   /* no PNG decoder: the folder's cover */
    CHECK(shared);
    CHECK_EQ_INT(pc_thumb_pick_image("04 Other.mp3", names, 6, true, &shared), 4);   /* cover before folder */
    CHECK_EQ_INT(pc_thumb_pick_image("04 Other.mp3", names, 4, true, &shared), 0);   /* only Folder.JPG */
    CHECK(shared);
    CHECK_EQ_INT(pc_thumb_pick_image("04 Other.mp3", names + 2, 2, false, &shared), -1);
    CHECK_EQ_INT(pc_thumb_pick_image("03 x.mp3", names, 6, true, &shared), 5);
    CHECK_EQ_INT(pc_thumb_pick_image("03 x.mp3.bak", names, 6, true, &shared), 4);   /* the base is everything before the last dot */
    CHECK_EQ_INT(pc_thumb_pick_image("noext", names, 0, true, &shared), -1);
    CHECK(pc_thumb_is_image("a.JPEG", false) && pc_thumb_is_image("b.jpg", false) && !pc_thumb_is_image("c.png", false) && pc_thumb_is_image("c.PNG", true));
    CHECK(!pc_thumb_is_image(".jpg", true) && !pc_thumb_is_image("x.jpgx", true));
}

/* PNG decodes to RGB888: the same centre square, nearest neighbour, into RGB565 */
static void test_crop_rgb888(void)
{
    int w = 6, h = 4;                                         /* centre square: x 1..4 */
    uint8_t src[6 * 4 * 3];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t *p = src + (y * w + x) * 3;
            p[0] = x == 0 || x == 5 ? 255 : 0; p[1] = x >= 1 && x <= 4 ? 255 : 0; p[2] = 0;
        }
    uint16_t dst[2 * 2];
    pc_thumb_crop_scale_rgb888(src, w, h, dst, 2);
    for (int i = 0; i < 4; i++) CHECK_EQ_INT(dst[i], GREEN);
    uint8_t one[3] = { 0x12, 0x34, 0x56 };
    uint16_t d1;
    pc_thumb_crop_scale_rgb888(one, 1, 1, &d1, 1);
    CHECK_EQ_INT(d1, ((0x12 >> 3) << 11) | ((0x34 >> 2) << 5) | (0x56 >> 3));
}

/* fix round 1 (dedupe): an embedded picture's entry is keyed by its bytes: tracks with the same picture share it */
static void test_thumb_content_key(void)
{
    uint8_t a[300], b[300];
    for (int i = 0; i < 300; i++) a[i] = b[i] = (uint8_t)(i * 7);
    uint64_t k = pc_thumb_content_key(a, 300);
    CHECK(k == pc_thumb_content_key(b, 300));
    b[299] ^= 1;
    CHECK(k != pc_thumb_content_key(b, 300));
    CHECK(k != pc_thumb_content_key(a, 299));
    CHECK(k != 0 && pc_thumb_content_key(a, 0) != 0);
    CHECK(k != pc_thumb_key("x", 0, 300) && k != pc_thumb_image_key("x", 300, 0));
}

/* review I2: the pack is written only while the card keeps max(64 MB, 5 % of the card) free after the write */
static void test_thumb_space(void)
{
    const uint64_t MB = 1024 * 1024;
    CHECK(pc_thumb_space_margin(8000 * MB) == 400 * MB);
    CHECK(pc_thumb_space_margin(512 * MB) == 64 * MB);
    CHECK(pc_thumb_space_ok(1000 * MB, 200 * MB, 115232));
    CHECK(pc_thumb_space_ok(1000 * MB, 64 * MB + 115232, 115232));
    CHECK(!pc_thumb_space_ok(1000 * MB, 64 * MB + 115231, 115232));
    CHECK(!pc_thumb_space_ok(64000 * MB, 3000 * MB, 115232));   /* 5 % of 64 GB = 3200 MB */
    CHECK(!pc_thumb_space_ok(1000 * MB, 10 * MB, 0));
    CHECK(!pc_thumb_space_ok(1000 * MB, 100 * MB, UINT64_MAX));  /* no wrap */
}

/* review I4 (ruling): the background pass and compaction run only when nothing plays, or after 10 s of pause */
static void test_thumb_idle(void)
{
    CHECK(pc_thumb_bg_allowed(false, false, 0));
    CHECK(!pc_thumb_bg_allowed(true, false, 0));
    CHECK(!pc_thumb_bg_allowed(false, true, 9999));
    CHECK(pc_thumb_bg_allowed(false, true, PC_THUMB_IDLE_PAUSE_MS));
    CHECK(!pc_thumb_bg_allowed(true, true, 60000));            /* playing wins */
    /* re-review m1: between two tracks (ENDED), after a fade or an error: like a pause (10 s), not idle at once */
    CHECK(pc_thumb_paused_like(PC_THUMB_AUD_ENDED) && pc_thumb_paused_like(PC_THUMB_AUD_FADED) && pc_thumb_paused_like(PC_THUMB_AUD_ERROR)
          && pc_thumb_paused_like(PC_THUMB_AUD_PAUSED) && !pc_thumb_paused_like(PC_THUMB_AUD_IDLE) && !pc_thumb_paused_like(PC_THUMB_AUD_PLAYING));
}

/* fix round 2 (re-review I-1 / I-4): every card write asks this at the moment of the write. No write while a source above 48 kHz is loaded or a
 * newer play is still opening (its rate is not known yet); background work also needs idle; nothing before the free space was read once */
static void test_thumb_write_gate(void)
{
    CHECK(pc_thumb_write_gate(false, false, false, false, true) == NULL);          /* the playing track's cover at <= 48 kHz */
    CHECK(pc_thumb_write_gate(false, false, true, true, true) == NULL);            /* background, idle */
    CHECK_STR(pc_thumb_write_gate(true, false, false, true, true), "hires");
    CHECK_STR(pc_thumb_write_gate(false, true, false, true, true), "opening");
    CHECK_STR(pc_thumb_write_gate(false, false, true, false, true), "playing");
    CHECK_STR(pc_thumb_write_gate(false, false, false, true, false), "space_unknown");
    CHECK_STR(pc_thumb_write_gate(true, true, true, false, false), "hires");       /* the first reason */
}

/* fix round 3 (re-review 2 I-A): a source is opening while the audio service's live generation (set by a play / mixer / queued next, 0 after a
 * stop or a fade) differs from the generation it has published; after a stop the published one is 0 too - not "opening" */
static void test_thumb_opening(void)
{
    CHECK(!pc_thumb_opening(0, 0));          /* stopped: nothing live */
    CHECK(!pc_thumb_opening(0, 7));          /* a stop was sent, the old source still published */
    CHECK(!pc_thumb_opening(7, 7));          /* playing what is live */
    CHECK(pc_thumb_opening(8, 7));           /* a newer play queued, not opened yet */
    CHECK(pc_thumb_opening(8, 0));           /* a play after a stop, not opened yet */
}

/* covers are not decoded (on demand or at scan) while a source above 48 kHz plays: the source's own rate, not the pipeline's */
static void test_thumb_hires_pause(void)
{
    CHECK(!pc_thumb_hires_pause(0));
    CHECK(!pc_thumb_hires_pause(44100));
    CHECK(!pc_thumb_hires_pause(48000));
    CHECK(pc_thumb_hires_pause(48001));
    CHECK(pc_thumb_hires_pause(88200));
    CHECK(pc_thumb_hires_pause(96000));
    CHECK(pc_thumb_hires_pause(384000));
}

#define RS 16
static uint16_t rsrc[RS * RS], rdst[RS * RS];
static void fill_index(void) { for (int i = 0; i < RS * RS; i++) rsrc[i] = (uint16_t)(i + 1); }

static void test_rotate_zero_is_disc(void)
{
    fill_index();
    pc_thumb_rotate(rsrc, rdst, RS, 0, 0xBEEF);
    CHECK_EQ_INT(rdst[0], 0xBEEF);                               /* corners: outside the disc */
    CHECK_EQ_INT(rdst[RS * RS - 1], 0xBEEF);
    for (int y = 0; y < RS; y++)
        for (int x = 0; x < RS; x++) {
            float dx = x + 0.5f - RS / 2.0f, dy = y + 0.5f - RS / 2.0f;
            if (dx * dx + dy * dy <= (RS / 2.0f - 1) * (RS / 2.0f - 1)) CHECK_EQ_INT(rdst[y * RS + x], rsrc[y * RS + x]);
        }
}

static uint16_t rbig[4 * RS * RS];
static void test_rotate_scaled(void)
{
    fill_index();
    pc_thumb_rotate_scaled(rsrc, RS, rdst, RS, 900, 0, false);        /* same size: the same picture as the plain rotation (interior) */
    static uint16_t ref[RS * RS];
    pc_thumb_rotate(rsrc, ref, RS, 900, 0);
    CHECK_EQ_INT(rdst[(RS / 2) * RS + (RS - 2)], ref[(RS / 2) * RS + (RS - 2)]);
    pc_thumb_rotate_scaled(rsrc, RS, rbig, 2 * RS, 0, 0xBEEF, false);  /* double size: a source pixel covers 2 x 2, the corners are outside the disc */
    CHECK_EQ_INT(rbig[0], 0xBEEF);
    CHECK_EQ_INT(rbig[(RS) * 2 * RS + RS], rsrc[(RS / 2) * RS + RS / 2]);
    pc_thumb_rotate_scaled(rsrc, RS, rbig, 2 * RS, 900, 0xBEEF, true);
    CHECK_EQ_INT(rbig[2 * RS * 2 * RS - 1], 0xBEEF);
}

static void test_rotate_90_clockwise(void)
{
    fill_index();
    pc_thumb_rotate(rsrc, rdst, RS, 900, 0);
    /* top-centre of the source ends up at the right-centre */
    CHECK_EQ_INT(rdst[(RS / 2) * RS + (RS - 2)], rsrc[1 * RS + RS / 2]);
    pc_thumb_rotate(rsrc, rdst, RS, 1800, 0);
    CHECK_EQ_INT(rdst[3 * RS + 5], rsrc[(RS - 1 - 3) * RS + (RS - 1 - 5)]);
}

static void test_default_record(void)
{
    static uint16_t px[240 * 240];
    pc_thumb_default(px, 240, 1, 2, 3, 4, 5);
    CHECK_EQ_INT(px[120 * 240 + 120], 5);                 /* spindle hole */
    CHECK_EQ_INT(px[120 * 240 + 120 + 30], 4);            /* label */
    int seen[6] = { 0 };
    for (int r = 45; r < 115; r++) seen[px[(120 + r) * 240 + 120]]++;   /* straight down (outside the sheen): grooves */
    CHECK(seen[1] > 0 && seen[2] > 0);
    int sheen = 0;
    for (int i = 0; i < 240 * 240; i++) sheen += px[i] == 3;
    CHECK(sheen > 1000);                                   /* the turning is visible */
}

static uint16_t g565(int g) { return (uint16_t)((g >> 3) << 11 | (g >> 2) << 5 | (g >> 3)); }
static void test_rotate_smooth(void)
{
    for (int i = 0; i < RS * RS; i++) rsrc[i] = g565((i % RS) * 16);    /* horizontal grey ramp */
    pc_thumb_rotate_smooth(rsrc, rdst, RS, 0, 0);
    CHECK_EQ_INT(rdst[(RS / 2) * RS + RS / 2], rsrc[(RS / 2) * RS + RS / 2]);   /* angle 0: the picture itself */
    CHECK_EQ_INT(rdst[0], 0);                                                    /* corner: bg */
    pc_thumb_rotate_smooth(rsrc, rdst, RS, 50, 0);                               /* 5 deg: a blend, not a copy */
    int between = 0;
    for (int x = 4; x < 12; x++) {
        uint16_t v = rdst[(RS / 2) * RS + x];
        int g = (v >> 5) & 63;
        int a = (rsrc[(RS / 2) * RS + x - 1] >> 5) & 63, b = (rsrc[(RS / 2) * RS + x + 1] >> 5) & 63;
        between += g >= a && g <= b;
    }
    CHECK(between >= 6);
    int rim = 0;                                                                 /* the rim is blended into bg */
    pc_thumb_rotate_smooth(rsrc, rdst, RS, 0, 0);
    for (int x = 0; x < RS; x++) { int g = (rdst[(RS / 2 - 6) * RS + x] >> 5) & 63, s = (rsrc[(RS / 2 - 6) * RS + x] >> 5) & 63; rim += g > 0 && g < s; }
    CHECK(rim >= 1);
}

static void test_rotate_soft_rim(void)        /* the cheap rotation also blends the rim (2026-11-09) */
{
    for (int i = 0; i < RS * RS; i++) rsrc[i] = g565(250);
    pc_thumb_rotate(rsrc, rdst, RS, 0, 0);
    int rim = 0;
    for (int x = 0; x < RS; x++) { int g = (rdst[(RS / 2 - 6) * RS + x] >> 5) & 63; rim += g > 0 && g < 62; }
    CHECK(rim >= 1);
    CHECK_EQ_INT(rdst[(RS / 2) * RS + RS / 2], rsrc[0]);
}

int main(void)
{
    RUN(test_rotate_soft_rim);
    RUN(test_rotate_smooth);
    RUN(test_default_record);
    RUN(test_rotate_zero_is_disc);
    RUN(test_rotate_90_clockwise);
    RUN(test_rotate_scaled);
    RUN(test_crop_center);
    RUN(test_crop_portrait);
    RUN(test_thumb_hash);
    RUN(test_thumb_key);
    RUN(test_thumb_pick_image);
    RUN(test_thumb_content_key);
    RUN(test_thumb_space);
    RUN(test_thumb_idle);
    RUN(test_thumb_write_gate);
    RUN(test_thumb_opening);
    RUN(test_crop_rgb888);
    RUN(test_thumb_hires_pause);
    return 0;
}
