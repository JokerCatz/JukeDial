// SPDX-License-Identifier: LGPL-2.1-or-later
/* Crash safety net: the RTC note (cur / next, sealed), what a boot takes from it, and the persisted marks. */
#include <unistd.h>
#include "pc/pc_crashnet.h"
#include "pc_test.h"

static void test_ids(void)
{
    CHECK(pc_cn_id("/sdcard/export/a.mp3") != pc_cn_id("/sdcard/export/b.mp3"));
    CHECK_EQ_INT(pc_cn_id("/x") == pc_cn_id("/x"), 1);
    CHECK(pc_cn_id("") != 0);                                  /* never 0: 0 means "none" */
}

static void test_rtc_take(void)
{
    pc_cn_rtc_t r;
    uint32_t out[2];
    memset(&r, 0xA5, sizeof r);                                /* power-on garbage */
    CHECK_EQ_INT(pc_cn_rtc_take(&r, true, out), 0);            /* not intact: nothing, even after a crash */
    pc_cn_rtc_set_cur(&r, 11);
    CHECK_EQ_INT(pc_cn_rtc_take(&r, false, out), 0);           /* a clean restart: nothing, and the note is cleared */
    CHECK_EQ_INT(pc_cn_rtc_take(&r, true, out), 0);
    pc_cn_rtc_set_cur(&r, 11);
    CHECK_EQ_INT(pc_cn_rtc_take(&r, true, out), 1);            /* a crash: the track that was playing */
    CHECK_EQ_INT((int)out[0], 11);
    CHECK_EQ_INT(pc_cn_rtc_take(&r, true, out), 0);            /* taken once */
    pc_cn_rtc_set_cur(&r, 11);
    pc_cn_rtc_set_next(&r, 22);                                /* the gapless open in flight: both are named */
    CHECK_EQ_INT(pc_cn_rtc_take(&r, true, out), 2);
    CHECK(out[0] == 11 && out[1] == 22);
    pc_cn_rtc_set_cur(&r, 11);
    pc_cn_rtc_set_next(&r, 22);
    pc_cn_rtc_set_cur(&r, 22);                                 /* the join happened: the queued one is the current and nothing is queued */
    CHECK_EQ_INT(pc_cn_rtc_take(&r, true, out), 1);
    CHECK_EQ_INT((int)out[0], 22);
    pc_cn_rtc_set_cur(&r, 33);
    pc_cn_rtc_set_cur(&r, 0);                                  /* stopped: nothing in flight */
    CHECK_EQ_INT(pc_cn_rtc_take(&r, true, out), 0);
    pc_cn_rtc_set_cur(&r, 44);
    r.cur ^= 1;                                                /* a torn write: the seal does not match */
    CHECK_EQ_INT(pc_cn_rtc_take(&r, true, out), 0);
}

static void test_marks(void)
{
    pc_cn_marks_t m = { .n = 0 };
    CHECK(pc_cn_marks_add(&m, 5));
    CHECK(!pc_cn_marks_add(&m, 5));                            /* once */
    CHECK(!pc_cn_marks_add(&m, 0));
    CHECK(pc_cn_marks_has(&m, 5) && !pc_cn_marks_has(&m, 6));
    CHECK(pc_cn_marks_del(&m, 5) && !pc_cn_marks_del(&m, 5));
    CHECK(!pc_cn_marks_has(&m, 5));
    for (uint32_t i = 1; i <= PC_CN_MAX + 3; i++) pc_cn_marks_add(&m, i);
    CHECK_EQ_INT(m.n, PC_CN_MAX);                              /* full: the oldest go */
    CHECK(!pc_cn_marks_has(&m, 1) && !pc_cn_marks_has(&m, 3) && pc_cn_marks_has(&m, 4) && pc_cn_marks_has(&m, PC_CN_MAX + 3));
}

static void test_file(void)
{
    char path[64];
    strcpy(path, "/tmp/pc_cn_XXXXXX");
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    pc_cn_marks_t m = { .n = 0 }, n;
    pc_cn_marks_add(&m, 0xDEADBEEFu);
    pc_cn_marks_add(&m, 7);
    CHECK_EQ_INT(pc_cn_marks_save(&m, path), 0);
    CHECK_EQ_INT(pc_cn_marks_load(&n, path), 0);
    CHECK_EQ_INT(n.n, 2);
    CHECK(pc_cn_marks_has(&n, 0xDEADBEEFu) && pc_cn_marks_has(&n, 7));
    remove(path);
    CHECK_EQ_INT(pc_cn_marks_load(&n, path), 0);               /* no file = no marks */
    CHECK_EQ_INT(n.n, 0);
    FILE *f = fopen(path, "w");
    fputs("zz\n00000000\n0000000a\ngarbage 12\n", f);          /* junk lines and zero are dropped */
    fclose(f);
    CHECK_EQ_INT(pc_cn_marks_load(&n, path), 0);
    CHECK(n.n == 1 && pc_cn_marks_has(&n, 0xA));
    remove(path);
}

int main(void)
{
    RUN(test_ids);
    RUN(test_rtc_take);
    RUN(test_marks);
    RUN(test_file);
    return 0;
}
