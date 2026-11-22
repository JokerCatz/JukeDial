// SPDX-License-Identifier: LGPL-2.1-or-later
#include "id3_fixture.h"
#include "pc/pc_id3.h"
#include "pc/pc_io.h"
#include "pc_test.h"

static id3b_t T;

static pc_id3_t parse(const id3b_t *t)
{
    pc_reader_t r;
    pc_reader_mem(&r, t->b, (uint32_t)t->n);
    pc_id3_t out;
    CHECK_EQ_INT(pc_id3_parse(&r, &out), 0);
    return out;
}

static void test_id3_v23_utf16(void)
{
    id3b_begin(&T, 3);
    id3b_text16(&T, "TIT2", "这首港风粤语");
    id3b_text16(&T, "TPE1", "早稻叽");
    id3b_text16(&T, "TALB", "專輯 One");
    id3b_text0(&T, "TYER", "2024");
    id3b_text0(&T, "TDAT", "1503");
    id3b_text0(&T, "TRCK", "3/12");
    id3b_text16(&T, "TCOP", "© someone");
    id3b_end(&T, 32, 100);
    pc_id3_t t = parse(&T);
    CHECK_STR(t.title, "这首港风粤语");
    CHECK_STR(t.artist, "早稻叽");
    CHECK_STR(t.album, "專輯 One");
    CHECK_STR(t.copyright, "© someone");
    CHECK_EQ_INT(t.date, 20240315);
    CHECK_EQ_INT(t.track, 3);
    CHECK_EQ_INT(t.audio_off, T.n - 100);
    pc_id3_free(&t);
}

static void test_id3_year_only(void)
{
    id3b_begin(&T, 3);
    id3b_text0(&T, "TYER", "2024");
    id3b_end(&T, 0, 10);
    pc_id3_t t = parse(&T);
    CHECK_EQ_INT(t.date, 20240101);
    pc_id3_free(&t);
}

static void test_id3_v24_tdrc(void)
{
    id3b_begin(&T, 4);
    id3b_text0(&T, "TDRC", "2023-07-09");
    char big[300];                              /* > 127 bytes: syncsafe size differs from plain */
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    id3b_text0(&T, "TIT2", big);
    id3b_end(&T, 0, 10);
    pc_id3_t t = parse(&T);
    CHECK_EQ_INT(t.date, 20230709);
    CHECK_EQ_INT(strlen(t.title), 127);
    pc_id3_free(&t);
}

static void test_id3_txxx(void)
{
    id3b_begin(&T, 3);
    id3b_txxx(&T, "purl", "https://www.youtube.com/watch?v=abc");
    id3b_txxx(&T, "description", "第一行\n第二行");
    id3b_txxx(&T, "other", "ignored");
    id3b_end(&T, 0, 10);
    pc_id3_t t = parse(&T);
    CHECK_STR(t.purl, "https://www.youtube.com/watch?v=abc");
    CHECK(t.desc != NULL);
    CHECK_STR(t.desc, "第一行\n第二行");
    pc_id3_free(&t);
    CHECK(t.desc == NULL);
}

static void test_id3_apic_offset(void)
{
    id3b_begin(&T, 3);
    id3b_text0(&T, "TIT2", "x");
    size_t img = id3b_apic(&T, 500);
    id3b_end(&T, 0, 10);
    pc_id3_t t = parse(&T);
    CHECK_EQ_INT(t.apic_off, img);
    CHECK_EQ_INT(t.apic_len, 500);
    CHECK_EQ_INT(T.b[t.apic_off], 0xFF);
    CHECK_EQ_INT(T.b[t.apic_off + 1], 0xD8);
    pc_id3_free(&t);
}

static void test_id3_none(void)
{
    uint8_t buf[64];
    memset(buf, 0xFF, sizeof buf);
    pc_reader_t r;
    pc_reader_mem(&r, buf, sizeof buf);
    pc_id3_t t;
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "");
    CHECK_EQ_INT(t.audio_off, 0);
    CHECK_EQ_INT(t.date, 0);
    pc_id3_free(&t);
}

static void test_id3_garbage(void)
{
    /* tag size larger than the file */
    id3b_begin(&T, 3);
    id3b_text0(&T, "TIT2", "kept");
    id3b_end(&T, 0, 0);
    put_size(T.b + 6, 0x0FFFFFFF, 1);
    pc_id3_t t = parse(&T);
    CHECK_STR(t.title, "kept");
    CHECK(t.audio_off <= T.n);
    pc_id3_free(&t);

    /* a frame whose size is absurd: parsing stops, earlier fields survive */
    id3b_begin(&T, 3);
    id3b_text0(&T, "TPE1", "artist");
    uint8_t h[10] = { 'T', 'I', 'T', '2', 0xFF, 0xFF, 0xFF, 0xF0, 0, 0 };
    id3b_raw(&T, h, 10);
    id3b_raw(&T, "\0abc", 4);
    id3b_end(&T, 0, 10);
    t = parse(&T);
    CHECK_STR(t.artist, "artist");
    CHECK_STR(t.title, "");
    pc_id3_free(&t);

    /* UTF-16 without BOM is read as little-endian */
    id3b_begin(&T, 3);
    uint8_t body[] = { 1, 'a', 0, 'b', 0 };
    id3b_frame(&T, "TIT2", body, sizeof body);
    id3b_end(&T, 0, 10);
    t = parse(&T);
    CHECK_STR(t.title, "ab");
    pc_id3_free(&t);

    /* truncated in the middle of a frame header */
    id3b_begin(&T, 3);
    id3b_text0(&T, "TIT2", "ok");
    id3b_end(&T, 100, 0);
    pc_reader_t r;
    pc_reader_mem(&r, T.b, 10 + 9 + 5);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    pc_id3_free(&t);
}

static void test_id3_truncate_utf8(void)
{
    char s[3 * 300 + 1];
    for (int i = 0; i < 300; i++) memcpy(s + 3 * i, "港", 3);
    s[900] = 0;
    id3b_begin(&T, 3);
    id3b_text16(&T, "TIT2", s);
    id3b_end(&T, 0, 10);
    pc_id3_t t = parse(&T);
    CHECK(strlen(t.title) < 128);
    CHECK_EQ_INT(strlen(t.title) % 3, 0);
    CHECK(strncmp(t.title, s, strlen(t.title)) == 0);
    pc_id3_free(&t);
}

static void test_id3_truncate_utf8_enc3(void)
{
    /* v2.4 UTF-8 title of 300 CJK chars: the clip must land on a character boundary */
    uint8_t body[1 + 900];
    body[0] = 3;
    for (int i = 0; i < 300; i++) memcpy(body + 1 + 3 * i, "港", 3);
    id3b_begin(&T, 4);
    id3b_frame(&T, "TIT2", body, sizeof body);
    id3b_end(&T, 0, 10);
    pc_id3_t t = parse(&T);
    CHECK(strlen(t.title) > 0 && strlen(t.title) < 128);
    CHECK_EQ_INT(strlen(t.title) % 3, 0);
    pc_id3_free(&t);
}

/* ReplayGain (ideas #2): TXXX descriptions in any case, "-6.53 dB" / "+1.2 dB" / peaks as plain numbers */
static void test_id3_replaygain(void)
{
    id3b_begin(&T, 3);
    id3b_txxx(&T, "REPLAYGAIN_TRACK_GAIN", "-6.53 dB");
    id3b_txxx(&T, "replaygain_track_peak", "0.988739");
    id3b_txxx(&T, "ReplayGain_Album_Gain", "+1.20 dB");
    id3b_txxx(&T, "REPLAYGAIN_ALBUM_PEAK", "1.034");
    id3b_end(&T, 0, 10);
    pc_id3_t t = parse(&T);
    CHECK_EQ_INT(t.rg.track_cdb, -653);
    CHECK_EQ_INT(t.rg.track_peak, 9887);
    CHECK_EQ_INT(t.rg.album_cdb, 120);
    CHECK_EQ_INT(t.rg.album_peak, 10340);
    pc_id3_free(&t);
}

static void test_id3_replaygain_absent_or_bad(void)
{
    id3b_begin(&T, 4);
    id3b_txxx(&T, "replaygain_track_gain", "loud");     /* not a number: ignored */
    id3b_text0(&T, "TIT2", "x");
    id3b_end(&T, 0, 10);
    pc_id3_t t = parse(&T);
    CHECK_EQ_INT(t.rg.track_cdb, PC_RG_NONE);
    CHECK_EQ_INT(t.rg.album_cdb, PC_RG_NONE);
    CHECK_EQ_INT(t.rg.track_peak, 0);
    CHECK_EQ_INT(t.rg.album_peak, 0);
    pc_id3_free(&t);
    /* a file without any tag also reports "no gain" */
    uint8_t raw[64] = { 0xFF, 0xFB };
    pc_reader_t r;
    pc_reader_mem(&r, raw, sizeof raw);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_EQ_INT(t.rg.track_cdb, PC_RG_NONE);
    pc_id3_free(&t);
}

/* FLAC: Vorbis comments (the same parser serves Ogg Vorbis / Opus) */
static size_t vc_put(uint8_t *b, size_t n, const char *s)
{
    uint32_t l = (uint32_t)strlen(s);
    b[n] = l & 0xff; b[n + 1] = (l >> 8) & 0xff; b[n + 2] = (l >> 16) & 0xff; b[n + 3] = l >> 24;
    memcpy(b + n + 4, s, l);
    return n + 4 + l;
}
static void test_flac_replaygain(void)
{
    static uint8_t f[1024];
    size_t n = 0;
    memcpy(f, "fLaC", 4); n = 4;
    size_t hdr = n; n += 4;                               /* block header: last, type 4 */
    n = vc_put(f, n, "test vendor");
    const char *c[] = { "TITLE=t", "replaygain_track_gain=-3.10 dB", "REPLAYGAIN_TRACK_PEAK=0.5", "Replaygain_Album_Gain=-4.00 dB",
                        "REPLAYGAIN_ALBUM_PEAK=0.75" };
    uint32_t cnt = 5;
    f[n] = (uint8_t)cnt; f[n + 1] = f[n + 2] = f[n + 3] = 0; n += 4;
    for (int i = 0; i < 5; i++) n = vc_put(f, n, c[i]);
    uint32_t len = (uint32_t)(n - hdr - 4);
    f[hdr] = 0x84; f[hdr + 1] = (uint8_t)(len >> 16); f[hdr + 2] = (uint8_t)(len >> 8); f[hdr + 3] = (uint8_t)len;
    pc_reader_t r;
    pc_reader_mem(&r, f, (uint32_t)n);
    pc_id3_t t;
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "t");
    CHECK_EQ_INT(t.rg.track_cdb, -310);
    CHECK_EQ_INT(t.rg.track_peak, 5000);
    CHECK_EQ_INT(t.rg.album_cdb, -400);
    CHECK_EQ_INT(t.rg.album_peak, 7500);
    pc_id3_free(&t);
}

/* MP4: '----' freeform items (mean com.apple.iTunes, name replaygain_*, data UTF-8) */
static size_t box(uint8_t *b, size_t n, const char *type, const void *body, size_t bl)
{
    uint32_t sz = (uint32_t)(8 + bl);
    b[n] = sz >> 24; b[n + 1] = sz >> 16; b[n + 2] = sz >> 8; b[n + 3] = sz;
    memcpy(b + n + 4, type, 4);
    if (bl) memmove(b + n + 8, body, bl);
    return n + 8 + bl;
}
static size_t freeform(uint8_t *b, size_t n, const char *name, const char *val)
{
    uint8_t in[256], t[128];
    size_t k = 0, l;
    memset(t, 0, 4); memcpy(t + 4, "com.apple.iTunes", 16);
    k = box(in, k, "mean", t, 20);
    memset(t, 0, 4); l = strlen(name); memcpy(t + 4, name, l);
    k = box(in, k, "name", t, 4 + l);
    uint8_t d[64] = { 0, 0, 0, 1, 0, 0, 0, 0 };
    l = strlen(val); memcpy(d + 8, val, l);
    k = box(in, k, "data", d, 8 + l);
    return box(b, n, "----", in, k);
}
static void test_mp4_replaygain(void)
{
    static uint8_t ilst[1024], meta[1100], udta[1200], moov[1300], f[1400];
    size_t il = 0;
    il = freeform(ilst, il, "replaygain_track_gain", "-7.25 dB");
    il = freeform(ilst, il, "replaygain_track_peak", "0.9");
    il = freeform(ilst, il, "REPLAYGAIN_ALBUM_GAIN", "-8.00 dB");
    il = freeform(ilst, il, "other", "1");
    size_t me = 4; memset(meta, 0, 4);                    /* full box: version + flags */
    me = box(meta, me, "ilst", ilst, il);
    size_t ud = box(udta, 0, "meta", meta, me);
    size_t mv = box(moov, 0, "udta", udta, ud);
    uint8_t ft[8] = { 'M', '4', 'A', ' ', 0, 0, 0, 0 };
    size_t n = box(f, 0, "ftyp", ft, 8);
    n = box(f, n, "moov", moov, mv);
    pc_reader_t r;
    pc_reader_mem(&r, f, (uint32_t)n);
    pc_id3_t t;
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_EQ_INT(t.rg.track_cdb, -725);
    CHECK_EQ_INT(t.rg.track_peak, 9000);
    CHECK_EQ_INT(t.rg.album_cdb, -800);
    CHECK_EQ_INT(t.rg.album_peak, 0);
    pc_id3_free(&t);
}


/* APEv2 at the end of a Musepack / TTA file (before an ID3v1), and an AIFF with NAME / AUTH chunks or an ID3 chunk */
static size_t ape_item(uint8_t *d, const char *key, const char *val)
{
    uint32_t vl = (uint32_t)strlen(val);
    memcpy(d, &vl, 4);
    memset(d + 4, 0, 4);
    size_t n = 8;
    memcpy(d + n, key, strlen(key) + 1);
    n += strlen(key) + 1;
    memcpy(d + n, val, vl);
    return n + vl;
}
static void test_ape_tag(void)
{
    uint8_t f[1024] = "MP+\x07";
    size_t n = 200, items = 0;
    uint8_t body[512];
    size_t b = 0;
    b += ape_item(body + b, "Title", "Ape Title"); items++;
    b += ape_item(body + b, "Artist", "Ape Artist"); items++;
    b += ape_item(body + b, "Year", "2011"); items++;
    b += ape_item(body + b, "Track", "4/9"); items++;
    b += ape_item(body + b, "REPLAYGAIN_TRACK_GAIN", "-6.50 dB"); items++;
    uint8_t foot[32] = "APETAGEX";
    uint32_t v2 = 2000, size = (uint32_t)b + 32, cnt = (uint32_t)items;
    memcpy(foot + 8, &v2, 4); memcpy(foot + 12, &size, 4); memcpy(foot + 16, &cnt, 4);
    memcpy(f + n, body, b);
    memcpy(f + n + b, foot, 32);
    n += b + 32;
    pc_reader_t r;
    pc_reader_mem(&r, f, (uint32_t)n);
    pc_id3_t t;
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "Ape Title");
    CHECK_STR(t.artist, "Ape Artist");
    CHECK_EQ_INT(t.date, 20110101);
    CHECK_EQ_INT(t.track, 4);
    CHECK_EQ_INT(t.rg.track_cdb, -650);
    pc_id3_free(&t);
    /* an ID3v1 after the APE tag moves the footer 128 bytes in from the end */
    uint8_t v1[128] = "TAG";
    memcpy(f + n, v1, 128);
    pc_reader_mem(&r, f, (uint32_t)n + 128);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "Ape Title");
    pc_id3_free(&t);
    size = 100000;                                      /* a lying size: no tag, no crash */
    memcpy(f + n - 32 + 12, &size, 4);
    pc_reader_mem(&r, f, (uint32_t)n);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "");
    pc_id3_free(&t);
}

static void be_put(uint8_t *d, uint32_t v) { d[0] = (uint8_t)(v >> 24); d[1] = (uint8_t)(v >> 16); d[2] = (uint8_t)(v >> 8); d[3] = (uint8_t)v; }
static size_t chunk(uint8_t *d, const char *id, const void *body, uint32_t n)
{
    memcpy(d, id, 4);
    be_put(d + 4, n);
    memcpy(d + 8, body, n);
    size_t t = 8 + n;
    if (n & 1) d[t++] = 0;
    return t;
}
static void test_aiff_tags(void)
{
    uint8_t f[4096];
    size_t n = 12;
    memcpy(f, "FORM", 4);
    memcpy(f + 8, "AIFF", 4);
    uint8_t comm[18] = { 0 };
    n += chunk(f + n, "COMM", comm, 18);
    n += chunk(f + n, "NAME", "Plain Name", 10);
    n += chunk(f + n, "AUTH", "Plain Author", 12);
    be_put(f + 4, (uint32_t)n - 8);
    pc_reader_t r;
    pc_reader_mem(&r, f, (uint32_t)n);
    pc_id3_t t;
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "Plain Name");
    CHECK_STR(t.artist, "Plain Author");
    pc_id3_free(&t);
    id3b_begin(&T, 3);                                  /* an "ID3 " chunk wins, and its offsets are file offsets */
    id3b_text0(&T, "TIT2", "Id3 Title");
    id3b_text0(&T, "TPE1", "Id3 Artist");
    id3b_end(&T, 0, 0);
    n += chunk(f + n, "ID3 ", T.b, (uint32_t)T.n);
    be_put(f + 4, (uint32_t)n - 8);
    pc_reader_mem(&r, f, (uint32_t)n);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    CHECK_STR(t.title, "Id3 Title");
    CHECK_STR(t.artist, "Id3 Artist");
    pc_id3_free(&t);
}

/* wave-6 review (HIGH 1): an "ID3 " chunk whose window starts with FORM...AIFF re-entered the AIFF parser with no limit: 20 bytes of file per
 * level, a pc_id3_t on the stack of every level. 30000 levels = 600 KB of file overflowed the tag task's stack. */
static void test_aiff_id3_nesting(void)
{
    const int levels = 30000;
    uint32_t cap = 64 + (uint32_t)levels * 20;
    uint8_t *a = calloc(1, cap), *b = calloc(1, cap);
    CHECK(a && b);
    uint32_t n = 16;                                    /* the innermost window: 16 bytes of nothing */
    memcpy(a, "FORM", 4);
    memcpy(a + 8, "AIFF", 4);
    for (int i = 0; i < levels; i++) {                  /* wrap: FORM <size> AIFF + chunk("ID3 ", previous) */
        memcpy(b, "FORM", 4);
        memcpy(b + 8, "AIFF", 4);
        size_t m = 12 + chunk(b + 12, "ID3 ", a, n);
        be_put(b + 4, (uint32_t)m - 8);
        uint8_t *t = a; a = b; b = t;
        n = (uint32_t)m;
    }
    pc_reader_t r;
    pc_reader_mem(&r, a, n);
    pc_id3_t t;
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);              /* returns, no recursion, no title */
    CHECK_EQ_INT(t.title[0], 0);
    pc_id3_free(&t);
    free(a);
    free(b);
    /* a chunk size near 2^32 must not wrap the "does it fit" check (32-bit body + size) */
    uint8_t f[64];
    memset(f, 0, sizeof f);
    memcpy(f, "FORM", 4);
    memcpy(f + 8, "AIFF", 4);
    memcpy(f + 12, "ID3 ", 4);
    be_put(f + 16, 0xFFFFFFF8u);
    pc_reader_mem(&r, f, sizeof f);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    pc_id3_free(&t);
    memcpy(f + 12, "NAME", 4);
    pc_reader_mem(&r, f, sizeof f);
    CHECK_EQ_INT(pc_id3_parse(&r, &t), 0);
    pc_id3_free(&t);
}

int main(void)
{
    RUN(test_aiff_id3_nesting);
    RUN(test_ape_tag);
    RUN(test_aiff_tags);
    RUN(test_id3_replaygain);
    RUN(test_id3_replaygain_absent_or_bad);
    RUN(test_flac_replaygain);
    RUN(test_mp4_replaygain);
    RUN(test_id3_truncate_utf8_enc3);
    RUN(test_id3_v23_utf16);
    RUN(test_id3_year_only);
    RUN(test_id3_v24_tdrc);
    RUN(test_id3_txxx);
    RUN(test_id3_apic_offset);
    RUN(test_id3_none);
    RUN(test_id3_garbage);
    RUN(test_id3_truncate_utf8);
    return 0;
}
