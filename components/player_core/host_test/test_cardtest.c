// SPDX-License-Identifier: LGPL-2.1-or-later
#include <string.h>
#include "pc/pc_cardtest.h"
#include "pc_test.h"

/* cardtest: MB/s arithmetic, the verdict table and its thresholds, the write test's pattern */

static int idx(const char *label)
{
    for (int i = 0; i < pc_ct_format_count(); i++) if (!strcmp(pc_ct_format(i)->label, label)) return i;
    return -1;
}

static void test_arith(void)
{
    CHECK_EQ_INT((int)pc_ct_x100(3500000, 1000000), 350);
    CHECK_EQ_INT((int)pc_ct_x100(2097152, 1000000), 209);
    CHECK_EQ_INT((int)pc_ct_x100(100, 0), 0);
}

static void test_table(void)
{
    CHECK_EQ_INT(pc_ct_format_count(), 7);
    CHECK(pc_ct_format(-1) == NULL && pc_ct_format(7) == NULL);
    /* the stream rates: 320 kbit/s; WAV 44.1/16 = 176400; FLAC 96/24 = 576000 x 0.75; 192/24 = 1152000; 384/24 FLAC = 2304000 x 0.75; 384/32 WAV = 3072000 */
    CHECK_EQ_INT((int)pc_ct_stream_bps(pc_ct_format(idx("MP3 / AAC / Opus 320k"))), 40000);
    CHECK_EQ_INT((int)pc_ct_stream_bps(pc_ct_format(idx("FLAC / WAV 44.1k/16"))), 176400);
    CHECK_EQ_INT((int)pc_ct_stream_bps(pc_ct_format(idx("FLAC 96k/24"))), 432000);
    CHECK_EQ_INT((int)pc_ct_stream_bps(pc_ct_format(idx("FLAC 192k/24"))), 864000);
    CHECK_EQ_INT((int)pc_ct_stream_bps(pc_ct_format(idx("WAV 192k/24"))), 1152000);
    CHECK_EQ_INT((int)pc_ct_stream_bps(pc_ct_format(idx("FLAC 384k/24"))), 1728000);
    CHECK_EQ_INT((int)pc_ct_stream_bps(pc_ct_format(idx("WAV 384k/32"))), 3072000);
    /* needed = 1.5 x, in MB/s x 100 rounded up */
    CHECK_EQ_INT((int)pc_ct_need_x100(pc_ct_format(idx("FLAC 384k/24"))), 260);   /* 2.592 */
    CHECK_EQ_INT((int)pc_ct_need_x100(pc_ct_format(idx("WAV 384k/32"))), 461);    /* 4.608 */
    CHECK_EQ_INT((int)pc_ct_need_x100(pc_ct_format(idx("MP3 / AAC / Opus 320k"))), 6);
}

static void test_grades(void)
{
    const pc_ct_fmt_t *f384 = pc_ct_format(idx("FLAC 384k/24"));
    CHECK_EQ_INT(pc_ct_grade(350, f384), PC_CT_YES);          /* the FakePod's card: 3.5 MB/s */
    CHECK_EQ_INT(pc_ct_grade(260, f384), PC_CT_YES);          /* exactly the need */
    CHECK_EQ_INT(pc_ct_grade(259, f384), PC_CT_MARGINAL);
    CHECK_EQ_INT(pc_ct_grade(195, f384), PC_CT_MARGINAL);     /* 75 % of 2.60 */
    CHECK_EQ_INT(pc_ct_grade(194, f384), PC_CT_NO);
    const pc_ct_fmt_t *w = pc_ct_format(idx("WAV 384k/32"));
    CHECK_EQ_INT(pc_ct_grade(350, w), PC_CT_MARGINAL);        /* 3.5 < 4.61 but >= 3.46 */
    CHECK_EQ_INT(pc_ct_grade(346, w), PC_CT_MARGINAL);
    CHECK_EQ_INT(pc_ct_grade(345, w), PC_CT_NO);
    CHECK_EQ_INT(pc_ct_grade(0, pc_ct_format(0)), PC_CT_NO);
    CHECK_EQ_INT(pc_ct_grade(100, pc_ct_format(0)), PC_CT_YES);
    for (int i = 1; i < pc_ct_format_count(); i++) {          /* a faster card never grades worse */
        for (uint32_t s = 0; s < 800; s += 7) CHECK(pc_ct_grade(s + 7, pc_ct_format(i)) >= pc_ct_grade(s, pc_ct_format(i)));
    }
}

static void test_pattern(void)
{
    static uint8_t a[4096], b[4096];
    pc_ct_fill(a, sizeof a, 0);
    for (uint32_t off = 0; off < sizeof a; off += 1024) pc_ct_fill(b + off, 1024, off);      /* another chunking, same bytes */
    CHECK(!memcmp(a, b, sizeof a));
    pc_ct_fill(b, 17, 1001);                                                                 /* an odd offset */
    CHECK(!memcmp(b, a + 1001, 17));
    uint64_t bad = 0;
    CHECK(pc_ct_check(a, sizeof a, 0, &bad));
    a[3000] ^= 0x10;
    CHECK(!pc_ct_check(a, sizeof a, 0, &bad));
    CHECK_EQ_INT((int)bad, 3000);
    CHECK(!pc_ct_check(a + 2048, 2048, 4096 * 5 + 2048, &bad));                              /* the wrong offset */
    int ones = 0;
    pc_ct_fill(b, sizeof b, 0);
    for (int i = 0; i < 4096; i++) ones += __builtin_popcount(b[i]);
    CHECK(ones > 4096 * 4 - 800 && ones < 4096 * 4 + 800);                                   /* looks random */
}

static int marker_at(const pc_ct_rung_t *r, int n, int which) { for (int i = 0; i < n; i++) if (r[i].fmt == which) return i; return -1; }

static void test_ladder(void)
{
    pc_ct_rung_t r[16];
    CHECK_EQ_INT(pc_ct_ladder(350, 0, r, 8), 0);                           /* no room: 7 formats + 2 markers */
    int n = pc_ct_ladder(350, 0, r, 16);
    CHECK_EQ_INT(n, 9);
    for (int i = 0; i + 1 < n; i++) if (r[i].fmt >= 0 && r[i + 1].fmt >= 0) CHECK(r[i].need_x100 >= r[i + 1].need_x100);   /* heaviest first */
    CHECK_STR(pc_ct_format(r[0].fmt)->label, "WAV 384k/32");
    CHECK_EQ_INT(r[0].grade, PC_CT_MARGINAL);                              /* 3.5 against 4.61 */
    CHECK_EQ_INT(r[1].fmt, PC_CT_RUNG_RAW);                                /* the raw marker: 384k FLAC (2.60) and everything lighter are below it */
    CHECK_EQ_INT((int)r[1].need_x100, 350);
    CHECK_STR(pc_ct_format(r[2].fmt)->label, "FLAC 384k/24");
    CHECK_EQ_INT(r[2].grade, PC_CT_YES);
    CHECK_EQ_INT(r[8].fmt, PC_CT_RUNG_FILE);                               /* no file result: its marker is last, speed 0 */
    CHECK_EQ_INT((int)r[8].need_x100, 0);
    /* a slow raw read (1.5): the marker under WAV 192/24 (1.73), which is marginal; 384k FLAC and WAV are too slow */
    n = pc_ct_ladder(150, 0, r, 16);
    CHECK_EQ_INT(marker_at(r, n, PC_CT_RUNG_RAW), 3);
    CHECK_EQ_INT(r[0].grade, PC_CT_NO);
    CHECK_EQ_INT(r[1].grade, PC_CT_NO);
    CHECK_EQ_INT(r[2].grade, PC_CT_MARGINAL);
    CHECK_EQ_INT(r[4].grade, PC_CT_YES);
    /* both results: two markers, each at its own place; the grades follow the FILE one (raw 3.50, file 1.50) */
    n = pc_ct_ladder(350, 150, r, 16);
    CHECK_EQ_INT(n, 9);
    CHECK_EQ_INT(marker_at(r, n, PC_CT_RUNG_RAW), 1);
    CHECK_EQ_INT(marker_at(r, n, PC_CT_RUNG_FILE), 4);                     /* WAV384/32, raw, FLAC384/24, WAV192/24 above it; FLAC 192/24 (1.30) and lighter below */
    CHECK_EQ_INT(r[3].grade, PC_CT_MARGINAL);                              /* WAV 192/24 (1.73) against the file speed 1.50 */
    CHECK_EQ_INT(r[2].grade, PC_CT_NO);                                    /* FLAC 384/24 against 1.50, though the raw read would pass it */
    /* the file read faster than the raw one: still the file one decides */
    n = pc_ct_ladder(150, 350, r, 16);
    CHECK(marker_at(r, n, PC_CT_RUNG_FILE) < marker_at(r, n, PC_CT_RUNG_RAW));
    CHECK_EQ_INT(r[2].grade, PC_CT_YES);                                   /* 384k FLAC at 3.5 */
    /* equal speeds: raw first, then file */
    n = pc_ct_ladder(300, 300, r, 16);
    CHECK_EQ_INT(marker_at(r, n, PC_CT_RUNG_FILE), marker_at(r, n, PC_CT_RUNG_RAW) + 1);
    /* no result at all: nothing graded, both markers at the bottom */
    n = pc_ct_ladder(0, 0, r, 16);
    CHECK_EQ_INT(r[n - 2].fmt, PC_CT_RUNG_RAW);
    CHECK_EQ_INT(r[n - 1].fmt, PC_CT_RUNG_FILE);
    n = pc_ct_ladder(10000, 0, r, 16);                                     /* a very fast card: the raw marker on top */
    CHECK_EQ_INT(r[0].fmt, PC_CT_RUNG_RAW);
    bool f;
    CHECK_EQ_INT((int)pc_ct_follow(350, 150, &f), 150);
    CHECK(f);
    CHECK_EQ_INT((int)pc_ct_follow(350, 0, &f), 350);
    CHECK(!f);
    CHECK_EQ_INT((int)pc_ct_follow(0, 0, &f), 0);
}

static void test_file(void)
{
    pc_ct_rec_t a = { .serial = 0x5D01, .size = 31914983424ull, .v = { 350, 320, 800, 4681 } };
    strcpy(a.when, "2026-11-20T12:30");
    char line[200], text[600];
    int n = pc_ct_rec_format(PC_CT_RAW, &a, line, sizeof line);
    CHECK(n > 0);
    CHECK_STR(line, "raw serial=00005D01 size=31914983424 time=2026-11-20T12:30 avg16=350 slow16=320 max64=800 worst_us=4681\n");
    int m;
    pc_ct_rec_t b;
    CHECK(pc_ct_rec_parse(line, &m, &b));
    CHECK_EQ_INT(m, PC_CT_RAW);
    CHECK(b.serial == a.serial && b.size == a.size && !memcmp(b.v, a.v, sizeof a.v));
    CHECK_STR(b.when, "2026-11-20T12:30");
    CHECK(pc_ct_rec_format(PC_CT_RAW, &a, line, 20) == 0);                 /* does not fit */
    pc_ct_rec_t f = { .serial = 0x5D01, .size = 31914983424ull, .v = { 400, 380, 1, 0 } };
    strcpy(f.when, "-");
    CHECK(pc_ct_rec_format(PC_CT_FILE, &f, line, sizeof line) > 0);
    CHECK_STR(line, "file serial=00005D01 size=31914983424 time=- write=400 read=380 verify=1\n");
    CHECK(pc_ct_rec_parse(line, &m, &b) && m == PC_CT_FILE && b.v[1] == 380 && !strcmp(b.when, "-"));
    CHECK(!pc_ct_rec_parse("raw serial=00005D01 size=1 time=- avg16=1 slow16=2 max64=3", &m, &b));   /* a missing field */
    CHECK(!pc_ct_rec_parse("raw serial=zz size=1 time=- avg16=1 slow16=2 max64=3 worst_us=4", &m, &b));
    CHECK(!pc_ct_rec_parse("junk", &m, &b));
    CHECK(!pc_ct_rec_parse("rawx serial=1", &m, &b));
    /* update: replace one mode, keep the other of the same card, drop the other of another card */
    size_t len = pc_ct_file_update(NULL, PC_CT_RAW, &a, text, sizeof text);
    CHECK(len > 0 && strlen(text) == len);
    char t2[600];
    len = pc_ct_file_update(text, PC_CT_FILE, &f, t2, sizeof t2);
    CHECK(len > 0);
    CHECK(strstr(t2, "raw serial=00005D01") && strstr(t2, "file serial=00005D01"));
    a.v[0] = 999;
    len = pc_ct_file_update(t2, PC_CT_RAW, &a, text, sizeof text);          /* the newer raw replaces the old one: still two lines */
    CHECK(len > 0 && strstr(text, "avg16=999") && !strstr(text, "avg16=350"));
    int lines = 0;
    for (const char *p = text; *p; p++) lines += *p == '\n';
    CHECK_EQ_INT(lines, 2);
    {   /* the three region speeds: written only when measured, read back, an older line (without them) still parses */
        pc_ct_rec_t g = { .serial = 0x5D01, .size = 1, .v = { 350, 300, 800, 4681, 300, 900, 1100 } };
        strcpy(g.when, "-");
        CHECK(pc_ct_rec_format(PC_CT_RAW, &g, line, sizeof line) > 0);
        CHECK_STR(line, "raw serial=00005D01 size=1 time=- avg16=350 slow16=300 max64=800 worst_us=4681 reg0=300 reg1=900 reg2=1100\n");
        CHECK(pc_ct_rec_parse(line, &m, &b) && b.v[4] == 300 && b.v[5] == 900 && b.v[6] == 1100);
        CHECK(pc_ct_rec_parse("raw serial=00000001 size=5 time=- avg16=1 slow16=2 max64=3 worst_us=4", &m, &b) && b.v[4] == 0 && b.v[6] == 0);
        CHECK(!pc_ct_rec_parse("raw serial=00000001 size=5 time=- avg16=1 slow16=2 max64=3 worst_us=4 reg0=x", &m, &b));
        uint32_t reg[3] = { 300, 900, 1100 };
        CHECK_EQ_INT(pc_ct_slowest(reg, 3), 300);
        reg[0] = 0;
        CHECK_EQ_INT(pc_ct_slowest(reg, 3), 900);                            /* not measured is skipped */
        reg[1] = reg[2] = 0;
        CHECK_EQ_INT(pc_ct_slowest(reg, 3), 0);
        uint32_t m3[3] = { 1100, 300, 900 };
        CHECK_EQ_INT(pc_ct_slowest(m3, 3), 300);                              /* the verdict stays the slowest region, wherever it is */
    }
    pc_ct_rec_t other = f;
    other.serial = 0x5D02;
    len = pc_ct_file_update(text, PC_CT_FILE, &other, t2, sizeof t2);        /* another card: the old card's raw line goes */
    CHECK(len > 0 && !strstr(t2, "raw ") && strstr(t2, "5D02"));
    CHECK(pc_ct_file_find(text, PC_CT_RAW, 0x5D01, 31914983424ull, &b) && b.v[0] == 999);
    CHECK(!pc_ct_file_find(text, PC_CT_RAW, 0x5D02, 31914983424ull, &b));    /* another serial */
    CHECK(!pc_ct_file_find(text, PC_CT_RAW, 0x5D01, 1, &b));                 /* another size */
    CHECK(!pc_ct_file_find(NULL, PC_CT_RAW, 1, 1, &b));
    CHECK(pc_ct_file_find("garbage\nraw serial=00000001 size=5 time=- avg16=1 slow16=2 max64=3 worst_us=4\nmore", PC_CT_RAW, 1, 5, &b));   /* a garbled line is skipped, the good one found */
}

static void test_passes(void)
{
    /* 3 regions x 3 passes: warm 16 KB, measured 16 KB (same sectors), 64 KB (4 MB later) */
    CHECK_EQ_INT(pc_ct_pass_role(0), PC_CT_PASS_WARM16); CHECK_EQ_INT(pc_ct_pass_role(1), PC_CT_PASS_MEAS16); CHECK_EQ_INT(pc_ct_pass_role(2), PC_CT_PASS_MEAS64);
    CHECK_EQ_INT(pc_ct_pass_role(3), PC_CT_PASS_WARM16); CHECK_EQ_INT(pc_ct_pass_role(8), PC_CT_PASS_MEAS64);
    CHECK_EQ_INT(pc_ct_pass_region(0), 0); CHECK_EQ_INT(pc_ct_pass_region(2), 0); CHECK_EQ_INT(pc_ct_pass_region(3), 1); CHECK_EQ_INT(pc_ct_pass_region(8), 2);
    CHECK_EQ_INT((int)pc_ct_pass_sector(1000, PC_CT_PASS_WARM16, 8192), 1000);
    CHECK_EQ_INT((int)pc_ct_pass_sector(1000, PC_CT_PASS_MEAS16, 8192), 1000);             /* the second pass reads where the warm one did */
    CHECK_EQ_INT((int)pc_ct_pass_sector(1000, PC_CT_PASS_MEAS64, 8192), 1000 + 8192);
    CHECK_STR(PC_CT_RAW_METHOD, "second_pass");
    /* the verdict follows the second pass: the three runs of the night (start region warm 2.76 / 3.36 / 4.44) no longer decide on a cold first read */
    const uint32_t second[3] = { 1040, 1100, 1060 };
    CHECK_EQ_INT((int)pc_ct_slowest(second, 3), 1040);
}

int main(void)
{
    RUN(test_passes);
    RUN(test_arith);
    RUN(test_table);
    RUN(test_grades);
    RUN(test_pattern);
    RUN(test_ladder);
    RUN(test_file);
    return 0;
}
