// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_mp4.h"
#include "pc_test.h"

static uint8_t *load(const char *name, uint32_t *len)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", FIXTURES, name);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    *len = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    CHECK(fread(b, 1, *len, f) == *len);
    fclose(f);
    return b;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
/* first chunk offset of the first stco in a buffer */
static uint32_t first_chunk(const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 4; i + 16 <= n; i++)
        if (!memcmp(b + i, "stco", 4)) { CHECK(be32(b + i + 8) >= 1); return be32(b + i + 12); }
    CHECK(0);
    return 0;
}
static uint8_t *read_all(const pc_reader_t *r)
{
    uint8_t *v = malloc(r->size);
    uint32_t got = 0;
    while (got < r->size) {                      /* odd-sized reads cross the segment borders */
        uint32_t k = r->size - got < 97 ? r->size - got : 97;
        CHECK_EQ_INT(r->read(r->u, got, v + got, k), k);
        got += k;
    }
    return v;
}

static void test_moov_after_mdat_is_moved_first(void)
{
    uint32_t len;
    uint8_t *d = load("tone_aac.m4a", &len);        /* ffmpeg's default: mdat, then moov */
    pc_reader_t src, v;
    pc_reader_mem(&src, d, len);
    pc_mp4_fs_t *st = NULL;
    CHECK_EQ_INT(pc_mp4_faststart(&src, &v, &st), 1);
    CHECK_EQ_INT(v.size, len);
    uint8_t *b = read_all(&v);
    CHECK(!memcmp(b + 4, "ftyp", 4));
    uint32_t after_ftyp = be32(b);
    CHECK(!memcmp(b + after_ftyp + 4, "moov", 4));
    uint32_t o_old = first_chunk(d, len), o_new = first_chunk(b, len);
    CHECK(o_new != o_old);
    CHECK(!memcmp(b + o_new, d + o_old, 64));          /* the patched offset still points at the same audio */
    free(b);
    pc_mp4_faststart_free(st);
    free(d);
}

static void test_faststart_file_untouched(void)
{
    uint32_t len;
    uint8_t *d = load("tone_aac_fast.m4a", &len);
    pc_reader_t src, v;
    pc_reader_mem(&src, d, len);
    pc_mp4_fs_t *st = (pc_mp4_fs_t *)1;
    CHECK_EQ_INT(pc_mp4_faststart(&src, &v, &st), 0);
    CHECK(st == NULL);
    free(d);
}

static void test_not_mp4(void)
{
    static const uint8_t junk[64] = "fLaC not an mp4 at all";
    pc_reader_t src, v;
    pc_reader_mem(&src, junk, sizeof junk);
    pc_mp4_fs_t *st = NULL;
    CHECK_EQ_INT(pc_mp4_faststart(&src, &v, &st), 0);
}

int main(void)
{
    RUN(test_moov_after_mdat_is_moved_first);
    RUN(test_faststart_file_untouched);
    RUN(test_not_mp4);
    return 0;
}
