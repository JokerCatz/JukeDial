// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_fmtguard.h"
#include "pc_test.h"

/* cardfmt: the format guard (entry rule, the steps in order, the Konami matcher, the 30 s timeout) and the plan (fs by size, clusters, MBR) */

static const int KONAMI[10] = { PC_FG_UP, PC_FG_UP, PC_FG_DOWN, PC_FG_DOWN, PC_FG_LEFT, PC_FG_RIGHT, PC_FG_LEFT, PC_FG_RIGHT, PC_FG_B, PC_FG_A };

static void test_entry_rule(void)
{
    CHECK(pc_fmt_entry_allowed("gpt", false, false));
    CHECK(pc_fmt_entry_allowed("nofs", false, false));
    CHECK(!pc_fmt_entry_allowed("nocard", false, false));        /* nothing answered: never */
    CHECK(!pc_fmt_entry_allowed("err", false, false));
    CHECK(!pc_fmt_entry_allowed(NULL, false, false));            /* a good mount (or none yet) */
    CHECK(!pc_fmt_entry_allowed("", false, false));
    CHECK(!pc_fmt_entry_allowed("GPT", false, false));
    CHECK(!pc_fmt_entry_allowed("gptx", false, false));
    CHECK(!pc_fmt_entry_allowed("gpt", true, false));            /* mounted: never */
    CHECK(!pc_fmt_entry_allowed("nofs", true, false));
    CHECK(!pc_fmt_entry_allowed("gpt", false, true));            /* USB drive mode: never */
    CHECK(!pc_fmt_entry_allowed("nofs", false, true));
    pc_fmtguard_t g = { .step = PC_FG_ARM };
    CHECK(!pc_fmtguard_begin(&g, "nocard", false, false));
    CHECK_EQ_INT(g.step, PC_FG_OFF);
    CHECK(!pc_fmtguard_begin(&g, "gpt", true, false));
    CHECK_EQ_INT(g.step, PC_FG_OFF);
    CHECK(!pc_fmtguard_begin(&g, "gpt", false, true));
    CHECK_EQ_INT(g.step, PC_FG_OFF);
    CHECK(pc_fmtguard_begin(&g, "nofs", false, false));
    CHECK_EQ_INT(g.step, PC_FG_WARN);
    CHECK_EQ_INT(g.got, 0);
}

static int enter(pc_fmtguard_t *g, const int *keys, int n, uint32_t t0, uint32_t dt)
{
    int r = PC_FG_KEY_IGNORED;
    for (int i = 0; i < n; i++) r = pc_fmtguard_key(g, keys[i], t0 + (uint32_t)i * dt);
    return r;
}

static void test_steps_in_order(void)
{
    pc_fmtguard_t g;
    pc_fmtguard_begin(&g, "gpt", false, false);
    CHECK(!pc_fmtguard_armed(&g));                               /* WARN: no skipping */
    CHECK(!pc_fmtguard_take(&g));
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_UP, 0), PC_FG_KEY_IGNORED);
    CHECK_EQ_INT(g.step, PC_FG_WARN);
    CHECK(pc_fmtguard_continue(&g, 1000));
    CHECK_EQ_INT(g.step, PC_FG_CODE);
    CHECK(!pc_fmtguard_continue(&g, 1000));                      /* only from WARN */
    CHECK(!pc_fmtguard_armed(&g));
    CHECK(!pc_fmtguard_take(&g));
    CHECK_EQ_INT(enter(&g, KONAMI, 9, 1100, 100), PC_FG_KEY_OK);
    CHECK_EQ_INT(g.got, 9);
    CHECK(!pc_fmtguard_armed(&g));                               /* nine of ten: still CODE */
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_A, 2100), PC_FG_KEY_DONE);
    CHECK_EQ_INT(g.step, PC_FG_ARM);
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_A, 2200), PC_FG_KEY_IGNORED);
    CHECK(!pc_fmtguard_take(&g));                                /* ARM: the armed tap first */
    CHECK(pc_fmtguard_armed(&g));
    CHECK_EQ_INT(g.step, PC_FG_GO);
    CHECK(!pc_fmtguard_armed(&g));
    CHECK(pc_fmtguard_take(&g));
    CHECK_EQ_INT(g.step, PC_FG_OFF);
    CHECK(!pc_fmtguard_take(&g));                                /* once */
    CHECK(!pc_fmtguard_continue(&g, 3000));                      /* OFF: a new session needs begin */
    CHECK(!pc_fmtguard_armed(&g));
}

static void test_back_every_step(void)
{
    for (int stop = PC_FG_WARN; stop <= PC_FG_GO; stop++) {
        pc_fmtguard_t g;
        pc_fmtguard_begin(&g, "nofs", false, false);
        if (stop >= PC_FG_CODE) pc_fmtguard_continue(&g, 0);
        if (stop >= PC_FG_ARM) CHECK_EQ_INT(enter(&g, KONAMI, 10, 10, 10), PC_FG_KEY_DONE);
        if (stop >= PC_FG_GO) CHECK(pc_fmtguard_armed(&g));
        CHECK_EQ_INT(g.step, stop);
        pc_fmtguard_back(&g);
        CHECK_EQ_INT(g.step, PC_FG_OFF);
        CHECK(!pc_fmtguard_take(&g));
        CHECK(!pc_fmtguard_armed(&g));
        CHECK(!pc_fmtguard_continue(&g, 500));
    }
}

static void test_wrong_tap_resets(void)
{
    pc_fmtguard_t g;
    pc_fmtguard_begin(&g, "gpt", false, false);
    pc_fmtguard_continue(&g, 0);
    CHECK_EQ_INT(enter(&g, KONAMI, 5, 100, 100), PC_FG_KEY_OK);
    CHECK_EQ_INT(g.got, 5);
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_LEFT, 700), PC_FG_KEY_WRONG);   /* RIGHT expected */
    CHECK_EQ_INT(g.got, 0);
    CHECK_EQ_INT(g.step, PC_FG_CODE);
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_UP, 800), PC_FG_KEY_OK);        /* the entry starts again from the first key */
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_UP, 900), PC_FG_KEY_OK);
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_UP, 1000), PC_FG_KEY_WRONG);    /* a third UP: wrong, cleared (not "one UP") */
    CHECK_EQ_INT(g.got, 0);
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_A, 1100), PC_FG_KEY_WRONG);
    CHECK_EQ_INT(pc_fmtguard_key(&g, -1, 1200), PC_FG_KEY_WRONG);           /* no such key */
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_NKEYS, 1300), PC_FG_KEY_WRONG);
    CHECK_EQ_INT(g.got, 0);
    static const int ab[10] = { PC_FG_UP, PC_FG_UP, PC_FG_DOWN, PC_FG_DOWN, PC_FG_LEFT, PC_FG_RIGHT, PC_FG_LEFT, PC_FG_RIGHT, PC_FG_A, PC_FG_B };
    CHECK_EQ_INT(enter(&g, ab, 10, 2000, 100), PC_FG_KEY_WRONG);            /* A B instead of B A */
    CHECK_EQ_INT(g.step, PC_FG_CODE);
    CHECK_EQ_INT(enter(&g, KONAMI, 10, 3000, 100), PC_FG_KEY_DONE);         /* after a wrong one, the whole code again */
    CHECK_EQ_INT(g.step, PC_FG_ARM);
    for (int i = 0; i < 10; i++) CHECK_EQ_INT(PC_FG_KONAMI[i], KONAMI[i]);
}

static void test_timeout(void)
{
    pc_fmtguard_t g;
    pc_fmtguard_begin(&g, "gpt", false, false);
    pc_fmtguard_continue(&g, 1000);
    CHECK(!pc_fmtguard_tick(&g, 1000 + PC_FG_TIMEOUT_MS - 1));
    CHECK_EQ_INT(g.step, PC_FG_CODE);
    CHECK(pc_fmtguard_tick(&g, 1000 + PC_FG_TIMEOUT_MS));                    /* 30 s from CODE's start without a tap: back to the warning */
    CHECK_EQ_INT(g.step, PC_FG_WARN);
    CHECK(!pc_fmtguard_tick(&g, 999999));                                    /* only in CODE */
    pc_fmtguard_continue(&g, 50000);
    CHECK_EQ_INT(enter(&g, KONAMI, 4, 51000, 1000), PC_FG_KEY_OK);           /* last tap at 54000 */
    CHECK(!pc_fmtguard_tick(&g, 54000 + PC_FG_TIMEOUT_MS - 1));              /* from the last tap, not from the start */
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_LEFT, 54000 + PC_FG_TIMEOUT_MS), PC_FG_KEY_TIMEOUT);   /* the late tap is not counted */
    CHECK_EQ_INT(g.step, PC_FG_WARN);
    CHECK_EQ_INT(g.got, 0);
    pc_fmtguard_continue(&g, 0xFFFFF000u);                                   /* the millisecond clock wraps */
    CHECK_EQ_INT(pc_fmtguard_key(&g, PC_FG_UP, 0x00000100u), PC_FG_KEY_OK);  /* 0x1100 ms later */
    CHECK(!pc_fmtguard_tick(&g, 0x00000100u + PC_FG_TIMEOUT_MS - 1));
    CHECK(pc_fmtguard_tick(&g, 0x00000100u + PC_FG_TIMEOUT_MS));
    /* restart: the page was covered or the screen went dark */
    pc_fmtguard_continue(&g, 0);
    enter(&g, KONAMI, 10, 0, 10);
    CHECK_EQ_INT(g.step, PC_FG_ARM);
    pc_fmtguard_restart(&g);
    CHECK_EQ_INT(g.step, PC_FG_WARN);
    CHECK_EQ_INT(g.got, 0);
    pc_fmtguard_back(&g);
    pc_fmtguard_restart(&g);                                                 /* OFF stays OFF */
    CHECK_EQ_INT(g.step, PC_FG_OFF);
    CHECK_STR(pc_fmtguard_step_name(PC_FG_CODE), "code");
    CHECK_STR(pc_fmtguard_step_name(99), "?");
}

#define MiB (1024ull * 1024)
#define GiB (1024ull * MiB)

static void plan(uint64_t bytes, int fs, uint32_t cluster, uint32_t bu, uint32_t align)
{
    pc_fmt_plan_t p;
    CHECK(pc_fmt_plan(bytes, 512, &p));
    CHECK_EQ_INT(p.fs, fs);
    CHECK_EQ_INT(p.cluster_bytes, cluster);
    CHECK_EQ_INT(p.bu_sectors, bu);
    CHECK_EQ_INT(p.align, align);
    CHECK_EQ_INT(p.total_sectors, bytes / 512);
    CHECK_EQ_INT(p.part_start, bu);
    CHECK_EQ_INT(p.part_start + p.part_sectors, p.total_sectors);
    CHECK_EQ_INT(p.n_fat, fs == PC_FMT_EXFAT ? 1 : 2);
    CHECK_EQ_INT(p.mbr_type, fs == PC_FMT_EXFAT ? 0x07 : fs == PC_FMT_FAT32 ? 0x0C : 0x06);
    uint64_t clusters = (uint64_t)p.part_sectors * 512 / p.cluster_bytes;
    if (fs == PC_FMT_FAT32) CHECK(clusters > 65525);                   /* FatFs refuses FAT32 with fewer */
    if (fs == PC_FMT_FAT16) CHECK(clusters <= 65525);
}

static void test_plan_table(void)
{
    plan(60 * MiB, PC_FMT_FAT16, 16384, 32, 32);                          /* SDSC: SD Association table for FAT12/16 */
    plan(250000000ull, PC_FMT_FAT16, 16384, 64, 64);
    plan(1000000000ull, PC_FMT_FAT16, 16384, 128, 128);
    plan(1 * GiB, PC_FMT_FAT16, 32768, 128, 128);                         /* 16 KB clusters would be 65536: one size up */
    plan(1990000000ull, PC_FMT_FAT16, 32768, 128, 128);
    plan(2 * GiB, PC_FMT_FAT16, 65536, 128, 128);                         /* 32 KB clusters would be 65534 (> 65525): 64 KB, the FAT16 top */
    plan(2 * GiB + 512 * 8192, PC_FMT_FAT32, 16384, 8192, 8192);         /* just above 2 GiB: 32 KB clusters would be too few for FAT32 */
    plan(3965190144ull, PC_FMT_FAT32, 32768, 8192, 8192);                /* a "4 GB" SDHC card */
    plan(31914983424ull, PC_FMT_FAT32, 32768, 8192, 8192);               /* a "32 GB" SDHC card (29.7 GiB) */
    plan(32 * GiB, PC_FMT_FAT32, 32768, 8192, 8192);                     /* the FAT32 / exFAT line: 32 GiB still FAT32 */
    plan(32 * GiB + 512, PC_FMT_EXFAT, 131072, 32768, 32768);
    plan(63864569856ull, PC_FMT_EXFAT, 131072, 32768, 32768);            /* "64 GB" SDXC */
    plan(128 * GiB, PC_FMT_EXFAT, 131072, 32768, 32768);
    plan(255869321216ull, PC_FMT_EXFAT, 262144, 65536, PC_FMT_ALIGN_MAX); /* "256 GB": BU 32 MiB, f_mkfs alignment capped at 16 MiB */
    plan(1024ull * 1000000000, PC_FMT_EXFAT, 524288, 131072, PC_FMT_ALIGN_MAX);   /* "1 TB" */
    pc_fmt_plan_t p;
    CHECK(!pc_fmt_plan(32 * MiB - 512, 512, &p));                         /* too small */
    CHECK(pc_fmt_plan(32 * MiB, 512, &p));
    CHECK(!pc_fmt_plan(0x100000000ull * 512, 512, &p));                   /* 2 TiB: more than 2^32 - 1 sectors */
    CHECK(pc_fmt_plan(0xFFFFFFFFull * 512, 512, &p));
    CHECK_EQ_INT(p.fs, PC_FMT_EXFAT);
    CHECK(!pc_fmt_plan(32 * GiB, 4096, &p));                              /* SD cards have 512-byte sectors */
    CHECK(!pc_fmt_plan(32 * GiB, 0, &p));
    CHECK_STR(pc_fmt_fs_name(PC_FMT_FAT32), "FAT32");
    CHECK_STR(pc_fmt_fs_name(PC_FMT_EXFAT), "exFAT");
    CHECK_STR(pc_fmt_fs_name(PC_FMT_FAT16), "FAT16");
    CHECK_STR(pc_fmt_fs_name(0), "?");
}

static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

static void test_mbr(void)
{
    pc_fmt_plan_t p;
    CHECK(pc_fmt_plan(63864569856ull, 512, &p));
    uint8_t s[512];
    memset(s, 0xA5, sizeof s);
    pc_fmt_mbr(&p, 0x12345678u, s);
    for (int i = 0; i < 440; i++) CHECK_EQ_INT(s[i], 0);                  /* no boot code */
    CHECK_EQ_INT(le32(s + 440), 0x12345678u);
    CHECK_EQ_INT(s[444], 0);
    CHECK_EQ_INT(s[445], 0);
    const uint8_t *e = s + 446;
    CHECK_EQ_INT(e[0], 0x00);                                             /* not active */
    CHECK(e[1] == 0xFE && e[2] == 0xFF && e[3] == 0xFF);                  /* CHS: LBA only */
    CHECK_EQ_INT(e[4], 0x07);
    CHECK(e[5] == 0xFE && e[6] == 0xFF && e[7] == 0xFF);
    CHECK_EQ_INT(le32(e + 8), 32768);
    CHECK_EQ_INT(le32(e + 12), p.total_sectors - 32768);
    for (int i = 446 + 16; i < 510; i++) CHECK_EQ_INT(s[i], 0);           /* three empty entries: no 0xEE anywhere */
    CHECK_EQ_INT(s[510], 0x55);
    CHECK_EQ_INT(s[511], 0xAA);
    CHECK(pc_fmt_plan(31914983424ull, 512, &p));
    pc_fmt_mbr(&p, 1, s);
    CHECK_EQ_INT(s[446 + 4], 0x0C);
    CHECK_EQ_INT(le32(s + 446 + 8), 8192);
}

static void label(uint64_t bytes, const char *want)
{
    char b[16];
    pc_fmt_size_label(bytes, b, sizeof b);
    CHECK_STR(b, want);
}

static void test_size_label(void)
{
    label(962000000ull, "962 MB");
    label(3965190144ull, "4.0 GB");
    label(3900000000ull, "3.9 GB");
    label(31914983424ull, "31.9 GB");
    label(63864569856ull, "63.9 GB");
    label(99940000000ull, "99.9 GB");
    label(99960000000ull, "100 GB");
    label(125069950976ull, "125 GB");
    label(0, "0 MB");
    char tiny[4];
    pc_fmt_size_label(31914983424ull, tiny, sizeof tiny);                 /* truncated, terminated */
    CHECK_EQ_INT(strlen(tiny), 3);
}

static void test_same_card(void)
{
    const pc_fmt_card_t a = { 62333952, 512, 0x5D01 };
    pc_fmt_card_t b = a;
    CHECK(pc_fmt_same_card(&a, &b));
    b.serial = 0x5D02;
    CHECK(!pc_fmt_same_card(&a, &b));                                     /* another card of the same size */
    b = a; b.sectors = 62333951;
    CHECK(!pc_fmt_same_card(&a, &b));                                     /* same serial, another size */
    b = a; b.sector_size = 4096;
    CHECK(!pc_fmt_same_card(&a, &b));
    const pc_fmt_card_t z = { 0, 512, 0 };
    CHECK(!pc_fmt_same_card(&z, &z));                                     /* nothing was read: never "the same" */
    CHECK(!pc_fmt_same_card(NULL, &a));
    CHECK(!pc_fmt_same_card(&a, NULL));
}

static void test_outcome(void)
{
    CHECK_EQ_INT(pc_fmt_outcome(PC_FMT_STAGE_MOUNT, true), PC_FMT_OUT_DONE);
    CHECK_EQ_INT(pc_fmt_outcome(PC_FMT_STAGE_BEFORE, false), PC_FMT_OUT_UNTOUCHED);   /* refused before any write: the data is as it was */
    CHECK_EQ_INT(pc_fmt_outcome(PC_FMT_STAGE_WRITE, false), PC_FMT_OUT_BROKEN);       /* the first sector may be written: never "untouched" */
    CHECK_EQ_INT(pc_fmt_outcome(PC_FMT_STAGE_MKFS, false), PC_FMT_OUT_BROKEN);
    CHECK_EQ_INT(pc_fmt_outcome(PC_FMT_STAGE_MOUNT, false), PC_FMT_OUT_UNMOUNTED);    /* formatted, the mount after it failed */
    CHECK_EQ_INT(pc_fmt_outcome(PC_FMT_STAGE_BEFORE, true), PC_FMT_OUT_BROKEN);       /* "ok" without the mount stage: not trusted as done */
    CHECK_EQ_INT(pc_fmt_outcome(PC_FMT_STAGE_MKFS, true), PC_FMT_OUT_BROKEN);
    CHECK_EQ_INT(pc_fmt_outcome(-1, false), PC_FMT_OUT_BROKEN);                       /* unknown stage: the safe word */
    CHECK_EQ_INT(pc_fmt_outcome(9, false), PC_FMT_OUT_BROKEN);
    CHECK_STR(pc_fmt_stage_name(PC_FMT_STAGE_BEFORE), "before");
    CHECK_STR(pc_fmt_stage_name(PC_FMT_STAGE_WRITE), "write");
    CHECK_STR(pc_fmt_stage_name(PC_FMT_STAGE_MKFS), "mkfs");
    CHECK_STR(pc_fmt_stage_name(PC_FMT_STAGE_MOUNT), "mount");
    CHECK_STR(pc_fmt_stage_name(7), "?");
    CHECK_STR(pc_fmt_outcome_name(PC_FMT_OUT_UNTOUCHED), "untouched");
    CHECK_STR(pc_fmt_outcome_name(PC_FMT_OUT_UNMOUNTED), "unmounted");
    CHECK_STR(pc_fmt_outcome_name(5), "?");
}

int main(void)
{
    RUN(test_same_card);
    RUN(test_outcome);
    RUN(test_entry_rule);
    RUN(test_steps_in_order);
    RUN(test_back_every_step);
    RUN(test_wrong_tap_resets);
    RUN(test_timeout);
    RUN(test_plan_table);
    RUN(test_mbr);
    RUN(test_size_label);
    return 0;
}
