// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdint.h>
#include <string.h>
#include "pc/pc_fontpak.h"
#include "pc_test.h"

static uint8_t s_img[4096];
static size_t s_len;

static void load_fixture(void)      /* built by tools/gen/make_fontpak.py: ui.ttf "font one\n", ui2.ttf "second font file, longer\n" */
{
    FILE *f = fopen(FIXTURES "/fontpak_small.bin", "rb");
    CHECK(f);
    s_len = fread(s_img, 1, sizeof s_img, f);
    fclose(f);
    CHECK(s_len == 153);
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void fix_dir_crc(int count) { put32(s_img + 8, pc_crc32(0, s_img + 12, pc_fontpak_dir_len(count) - 12)); }

static void test_crc32_matches_zlib(void)
{
    CHECK_EQ_INT(pc_crc32(0, "123456789", 9), 0xCBF43926u);
    CHECK_EQ_INT(pc_crc32(pc_crc32(0, "1234", 4), "56789", 5), 0xCBF43926u);   /* incremental */
    CHECK_EQ_INT(pc_crc32(0, "", 0), 0);
}

static void test_parse_python_image(void)
{
    load_fixture();
    pc_fontpak_t p;
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_OK);
    CHECK_EQ_INT(p.count, 2);
    CHECK_EQ_INT(p.image_size, 153);
    const pc_fontpak_entry_t *e = pc_fontpak_find(&p, "ui2.ttf");
    CHECK(e);
    CHECK_EQ_INT(e->size, 25);
    CHECK(!memcmp(s_img + e->offset, "second font file, longer\n", 25));
    CHECK(pc_fontpak_check(e, s_img + e->offset));
    e = pc_fontpak_find(&p, "ui.ttf");
    CHECK(e && e->offset % 16 == 0 && pc_fontpak_check(e, s_img + e->offset));
    CHECK(!pc_fontpak_find(&p, "ui3.ttf"));
    CHECK(!pc_fontpak_find(&p, "ui"));            /* whole names only */
    CHECK(!pc_fontpak_find(NULL, "ui.ttf"));
}

static void test_corrupt_file_data_fails_check(void)
{
    load_fixture();
    pc_fontpak_t p;
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_OK);
    const pc_fontpak_entry_t *e = pc_fontpak_find(&p, "ui.ttf");
    s_img[e->offset + 3] ^= 1;
    CHECK(!pc_fontpak_check(e, s_img + e->offset));
}

static void test_erased_or_short_partition(void)
{
    pc_fontpak_t p;
    uint8_t ff[64];
    memset(ff, 0xff, sizeof ff);
    CHECK_EQ_INT(pc_fontpak_parse(ff, sizeof ff, 0x1000, &p), PC_FONTPAK_EMAGIC);
    load_fixture();
    CHECK_EQ_INT(pc_fontpak_parse(s_img, 10, 0x1000, &p), PC_FONTPAK_ESHORT);
    CHECK_EQ_INT(pc_fontpak_parse(s_img, 40, 0x1000, &p), PC_FONTPAK_ESHORT);   /* header ok, directory cut */
    CHECK_EQ_INT(pc_fontpak_parse(NULL, 0, 0x1000, &p), PC_FONTPAK_ESHORT);
}

static void test_bad_header_fields(void)
{
    pc_fontpak_t p;
    load_fixture();
    s_img[4] = 2;                                   /* version 2 */
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_EVERSION);
    load_fixture();
    s_img[6] = 0;                                   /* 0 files */
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_ECOUNT);
    load_fixture();
    s_img[6] = PC_FONTPAK_MAX + 1;
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_ECOUNT);
    load_fixture();
    s_img[0x30] ^= 0x40;                            /* an offset changed, CRC not updated */
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_ECRC);
}

static void test_ranges(void)
{
    pc_fontpak_t p;
    load_fixture();
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 152, &p), PC_FONTPAK_ERANGE);   /* image larger than the partition */
    load_fixture();
    put32(s_img + 0x34, 0xfffffff0u);              /* ui.ttf size: offset + size wraps around */
    fix_dir_crc(2);
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_ERANGE);
    load_fixture();
    put32(s_img + 0x34, 200);                       /* ui.ttf beyond image_size */
    fix_dir_crc(2);
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_ERANGE);
    load_fixture();
    put32(s_img + 0x30, 0x20);                      /* ui.ttf inside the directory */
    fix_dir_crc(2);
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_ERANGE);
}

static void test_names(void)
{
    pc_fontpak_t p;
    load_fixture();
    memset(s_img + 0x10, 'a', PC_FONTPAK_NAME);    /* no NUL */
    fix_dir_crc(2);
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_ENAME);
    load_fixture();
    s_img[0x10] = 0;                                /* empty name */
    fix_dir_crc(2);
    CHECK_EQ_INT(pc_fontpak_parse(s_img, s_len, 0x1000, &p), PC_FONTPAK_ENAME);
}

/* a minimal sfnt head: version, numTables, 3 x u16, then 16-byte records */
static size_t sfnt(uint8_t *b, uint32_t ver, int ntab, uint8_t fill)
{
    memset(b, 0, 512);
    b[0] = ver >> 24; b[1] = ver >> 16; b[2] = ver >> 8; b[3] = ver;
    b[4] = 0; b[5] = (uint8_t)ntab;
    for (int i = 0; i < ntab * 16; i++) b[12 + i] = (uint8_t)(fill + i);
    return 12 + 16 * (size_t)ntab;
}

static void test_sfnt_dir_len(void)
{
    uint8_t b[512];
    CHECK_EQ_INT(pc_sfnt_dir_len(b, sfnt(b, 0x00010000, 17, 1)), 12 + 16 * 17);
    sfnt(b, 0x4F54544F, 3, 1);                      /* 'OTTO' */
    CHECK_EQ_INT(pc_sfnt_dir_len(b, 512), 60);
    sfnt(b, 0x74727565, 3, 1);                      /* 'true' */
    CHECK_EQ_INT(pc_sfnt_dir_len(b, 512), 60);
    sfnt(b, 0x12345678, 3, 1);
    CHECK_EQ_INT(pc_sfnt_dir_len(b, 512), -1);      /* not a font */
    sfnt(b, 0x00010000, 0, 1);
    CHECK_EQ_INT(pc_sfnt_dir_len(b, 512), -1);      /* no tables */
    sfnt(b, 0x00010000, 31, 1);
    CHECK_EQ_INT(pc_sfnt_dir_len(b, 508), 508);     /* exactly fits */
    CHECK_EQ_INT(pc_sfnt_dir_len(b, 512), 508);
    CHECK_EQ_INT(pc_sfnt_dir_len(b, 507), -1);
    CHECK_EQ_INT(pc_sfnt_dir_len(b, 4), -1);
}

static void test_sfnt_same(void)
{
    uint8_t a[512], b[512];
    sfnt(a, 0x00010000, 10, 7);
    sfnt(b, 0x00010000, 10, 7);
    b[300] = 0x55;                                  /* after the directory: not compared */
    CHECK(pc_sfnt_same(a, 1000, b, 1000, 512));
    CHECK(!pc_sfnt_same(a, 1000, b, 1001, 512));    /* sizes differ */
    b[12 + 8] ^= 1;                                 /* a table checksum differs */
    CHECK(!pc_sfnt_same(a, 1000, b, 1000, 512));
    sfnt(b, 0x00010000, 11, 7);
    CHECK(!pc_sfnt_same(a, 1000, b, 1000, 512));    /* table count differs */
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    CHECK(!pc_sfnt_same(a, 1000, b, 1000, 512));    /* not fonts: never "same" */
}

static void test_pick(void)
{
    /* auto */
    CHECK_EQ_INT(pc_font_pick(PC_FONT_PREF_AUTO, true, true, true), PC_FONT_FLASH);
    CHECK_EQ_INT(pc_font_pick(PC_FONT_PREF_AUTO, true, true, false), PC_FONT_TF);
    CHECK_EQ_INT(pc_font_pick(PC_FONT_PREF_AUTO, true, false, false), PC_FONT_TF);
    CHECK_EQ_INT(pc_font_pick(PC_FONT_PREF_AUTO, false, true, false), PC_FONT_FLASH);
    CHECK_EQ_INT(pc_font_pick(PC_FONT_PREF_AUTO, false, false, false), PC_FONT_NONE);
    CHECK_EQ_INT(pc_font_pick(PC_FONT_PREF_AUTO, false, true, true), PC_FONT_FLASH);   /* "same" without a card copy: ignored */
    /* only flash / only card */
    CHECK_EQ_INT(pc_font_pick(PC_FONT_PREF_FLASH, true, true, false), PC_FONT_FLASH);
    CHECK_EQ_INT(pc_font_pick(PC_FONT_PREF_FLASH, true, false, false), PC_FONT_NONE);
    CHECK_EQ_INT(pc_font_pick(PC_FONT_PREF_TF, true, true, true), PC_FONT_TF);
    CHECK_EQ_INT(pc_font_pick(PC_FONT_PREF_TF, false, true, true), PC_FONT_NONE);
    /* unknown values behave as auto */
    CHECK_EQ_INT(pc_font_pick(7, true, true, true), PC_FONT_FLASH);
    CHECK_EQ_INT(pc_font_pick(-1, true, false, false), PC_FONT_TF);
    CHECK_STR(pc_font_src_name(PC_FONT_FLASH), "flash");
    CHECK_STR(pc_font_src_name(PC_FONT_TF), "tf");
    CHECK_STR(pc_font_src_name(PC_FONT_NONE), "none");
}

int main(void)
{
    RUN(test_crc32_matches_zlib);
    RUN(test_parse_python_image);
    RUN(test_corrupt_file_data_fails_check);
    RUN(test_erased_or_short_partition);
    RUN(test_bad_header_fields);
    RUN(test_ranges);
    RUN(test_names);
    RUN(test_sfnt_dir_len);
    RUN(test_sfnt_same);
    RUN(test_pick);
    return 0;
}
