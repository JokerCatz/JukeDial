// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_timers.h"
#include "pc_test.h"

static void test_sw_basic(void)
{
    pc_sw_t s;
    pc_sw_reset(&s);
    CHECK_EQ_INT(pc_sw_elapsed(&s, 5000), 0);
    CHECK(!pc_sw_lap(&s, 100));                          /* not running: no lap */
    pc_sw_start(&s, 1000);
    CHECK_EQ_INT(pc_sw_elapsed(&s, 3500), 2500);
    pc_sw_pause(&s, 4000);
    CHECK_EQ_INT(pc_sw_elapsed(&s, 90000), 3000);        /* paused: holds */
    pc_sw_start(&s, 10000);
    CHECK_EQ_INT(pc_sw_elapsed(&s, 11000), 4000);        /* counts on */
    pc_sw_start(&s, 11000);                              /* a second start changes nothing */
    CHECK_EQ_INT(pc_sw_elapsed(&s, 12000), 5000);
    pc_sw_reset(&s);
    CHECK_EQ_INT(pc_sw_elapsed(&s, 12000), 0);
}
static void test_sw_laps(void)
{
    pc_sw_t s;
    pc_sw_reset(&s);
    pc_sw_start(&s, 0);
    CHECK(pc_sw_lap(&s, 2000));
    CHECK(pc_sw_lap(&s, 3500));
    CHECK(pc_sw_lap(&s, 7500));
    int no; int64_t sp, tot;
    CHECK(pc_sw_lap_get(&s, 0, &no, &sp, &tot));
    CHECK_EQ_INT(no, 3); CHECK_EQ_INT(sp, 4000); CHECK_EQ_INT(tot, 7500);
    CHECK(pc_sw_lap_get(&s, 2, &no, &sp, &tot));
    CHECK_EQ_INT(no, 1); CHECK_EQ_INT(sp, 2000);
    CHECK(!pc_sw_lap_get(&s, 3, NULL, NULL, NULL));
    CHECK_EQ_INT(pc_sw_lap_best(&s), 1);                 /* lap 2 (1500) */
    for (int i = 0; i < 30; i++) pc_sw_lap(&s, 8000 + i * 1000);
    CHECK_EQ_INT(s.nlap, 33);
    CHECK(pc_sw_lap_get(&s, 0, &no, &sp, NULL));
    CHECK_EQ_INT(no, 33); CHECK_EQ_INT(sp, 1000);
    CHECK(pc_sw_lap_get(&s, PC_SW_LAPS - 1, &no, &sp, NULL));
    CHECK_EQ_INT(no, 14); CHECK_EQ_INT(sp, 0);           /* the lap before it is gone */
    CHECK(pc_sw_lap_best(&s) >= 0 && pc_sw_lap_best(&s) < PC_SW_LAPS - 1);
    pc_sw_pause(&s, 99999);
    CHECK(!pc_sw_lap(&s, 100000));
}
static void test_fmt(void)
{
    char b[16];
    pc_sw_fmt(0, b, sizeof b); CHECK_STR(b, "00:00.00");
    pc_sw_fmt(83450, b, sizeof b); CHECK_STR(b, "01:23.45");
    pc_sw_fmt(3600000 + 61000, b, sizeof b); CHECK_STR(b, "1:01:01");
    pc_cd_fmt(10000, b, sizeof b); CHECK_STR(b, "00:10");
    pc_cd_fmt(9001, b, sizeof b); CHECK_STR(b, "00:10");
    pc_cd_fmt(1, b, sizeof b); CHECK_STR(b, "00:01");
    pc_cd_fmt(0, b, sizeof b); CHECK_STR(b, "00:00");
    pc_cd_fmt(3600000, b, sizeof b); CHECK_STR(b, "1:00:00");
}
static void test_cd(void)
{
    pc_cd_t c;
    pc_cd_init(&c, 0);
    CHECK_EQ_INT(c.set_s, 1);
    pc_cd_init(&c, 999999);
    CHECK_EQ_INT(c.set_s, PC_CD_MAX_S);
    pc_cd_init(&c, 30);
    CHECK_EQ_INT(pc_cd_left_ms(&c, 7), 30000);
    CHECK(!pc_cd_tick(&c, 1000000));                     /* idle never fires */
    pc_cd_start(&c, 1000);
    CHECK_EQ_INT(pc_cd_left_ms(&c, 11000), 20000);
    pc_cd_set(&c, 99);
    CHECK_EQ_INT(c.set_s, 30);                           /* not while running */
    CHECK(!pc_cd_warn(&c, 11000));
    CHECK(pc_cd_warn(&c, 21000));                        /* 10 s left */
    CHECK(!pc_cd_warn(&c, 20999 - 0 - 1000));
    pc_cd_pause(&c, 25000);
    CHECK_EQ_INT(pc_cd_left_ms(&c, 99999), 6000);
    CHECK(!pc_cd_warn(&c, 99999));                       /* paused: no warning */
    pc_cd_start(&c, 100000);
    CHECK_EQ_INT(pc_cd_left_ms(&c, 103000), 3000);
    CHECK(!pc_cd_tick(&c, 105999));
    CHECK(pc_cd_tick(&c, 106000));
    CHECK(!pc_cd_tick(&c, 106001));                      /* once */
    CHECK_EQ_INT(c.state, PC_CD_DONE);
    CHECK_EQ_INT(pc_cd_left_ms(&c, 200000), 0);
    pc_cd_set(&c, 45);
    CHECK_EQ_INT(c.state, PC_CD_IDLE);
    CHECK_EQ_INT(c.set_s, 45);
    pc_cd_start(&c, 0);
    pc_cd_reset(&c);
    CHECK_EQ_INT(pc_cd_left_ms(&c, 5), 45000);
    pc_cd_start(&c, 0);
    CHECK(pc_cd_blink(&c, 40000 - 400) != pc_cd_blink(&c, 40000 - 900));   /* the blink alternates inside a second */
}
int main(void) { RUN(test_sw_basic); RUN(test_sw_laps); RUN(test_fmt); RUN(test_cd); return 0; }
