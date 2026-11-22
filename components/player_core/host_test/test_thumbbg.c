// SPDX-License-Identifier: LGPL-2.1-or-later
/* hires T8e: the cover pack's background pass - when it runs, yields, stops and resumes; the page's and 現況's texts */
#include "pc/pc_thumbbg.h"
#include <stdint.h>
#include "pc_test.h"

static pc_tbg_in_t base(void)   /* nothing plays, a card, auto, the free space read: runs */
{
    pc_tbg_in_t in = { .card = true, .underrun_age_ms = UINT32_MAX, .space_known = true };
    return in;
}

static void test_runs_stopped_and_playing(void)
{
    pc_tbg_in_t in = base();
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_RUN);
    in.loaded = true; in.src_rate = 44100;                   /* 44.1 kHz plays: the pass runs too */
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_RUN);
    in.src_rate = 48000;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_RUN);
    in.src_rate = 88200;                                     /* above 48 kHz: held, as the hi-res pause */
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_HIRES);
    in.src_rate = 96000;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_HIRES);
}

static void test_holds_and_precedence(void)
{
    pc_tbg_in_t in = base();
    in.opening = true;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_OPENING);
    in.loaded = true; in.src_rate = 96000;                   /* hi-res before opening */
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_HIRES);
    in.scanning = true;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_SCAN);
    in.user_paused = true;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_USER);
    in.manual = true;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_MANUAL);
    in.card = false;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_NO_CARD);
    in = base();
    in.manual = true;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_MANUAL);
    in.requested = true;                                     /* 立即建置 in 手動: runs */
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_RUN);
    in.user_paused = true;                                   /* the page's 暫停 holds a requested pass too */
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_USER);
    in = base();
    in.low_mem = true;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_LOW_MEM);
}

static void test_space_needs_a_stop(void)
{
    pc_tbg_in_t in = base();
    in.space_known = false;                                  /* nothing loaded: the pass reads it itself */
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_RUN);
    in.loaded = true; in.src_rate = 44100;                   /* playing and never read: waits for a stop (its first read can scan the FAT) */
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_STOP_NEEDED);
    in.space_known = true;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_RUN);
}

static void test_underrun_quiet_and_giveup(void)
{
    pc_tbg_in_t in = base();
    in.loaded = true; in.src_rate = 44100;
    in.underrun_age_ms = 0; in.underrun_holds = 1;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_UNDERRUN);
    in.underrun_age_ms = PC_TBG_QUIET_MS - 1;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_UNDERRUN);
    in.underrun_age_ms = PC_TBG_QUIET_MS;                    /* N s without one: resumes */
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_RUN);
    in.underrun_holds = PC_TBG_GIVEUP;                       /* the third hold: playing stays off this boot */
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_UNDERRUN);
    in.underrun_age_ms = UINT32_MAX - 1;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_UNDERRUN);
    in.loaded = false; in.src_rate = 0;                      /* stopped: nothing to protect, it runs */
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_RUN);
    in.loaded = true; in.src_rate = 44100; in.underrun_holds = PC_TBG_GIVEUP - 1; in.underrun_age_ms = UINT32_MAX;
    CHECK_EQ_INT(pc_tbg_rule(&in), PC_TBG_RUN);
}

static void test_breather(void)
{
    CHECK_EQ_INT(pc_tbg_breather_ms(false), PC_TBG_BREATHER_IDLE_MS);
    CHECK_EQ_INT(pc_tbg_breather_ms(true), PC_TBG_BREATHER_PLAY_MS);
    CHECK(PC_TBG_BREATHER_PLAY_MS > PC_TBG_BREATHER_IDLE_MS);
}

/* fix round 1 (review I1): only underruns while a pass step was in flight (or in its breather window) are the pass's */
static void test_under_tracker(void)
{
    pc_tbg_under_t u = { 0 };
    CHECK_EQ_INT(pc_tbg_under_age(&u, 1000), UINT32_MAX);
    CHECK(!pc_tbg_under_check(&u, 5, 1, 5000, 1000));        /* not armed: nothing is charged */
    CHECK(!pc_tbg_under_check(&u, 5, 4, 5000, 2000));        /* a rescan's (or a pause's) underruns while no step ran */
    CHECK_EQ_INT(u.holds, 0);
    pc_tbg_under_arm(&u, 5, 4);                              /* the baseline is taken at the step: the earlier 3 are not the pass's */
    CHECK(!pc_tbg_under_check(&u, 5, 4, 6000, 3000));
    CHECK(pc_tbg_under_check(&u, 5, 5, 6000, 3500));         /* a rise during the step: charged */
    CHECK_EQ_INT(u.holds, 1);
    CHECK_EQ_INT(pc_tbg_under_age(&u, 4000), 500);
    CHECK(pc_tbg_under_check(&u, 5, 6, 7000, 4000));         /* still in the window (armed until disarm): the same hold */
    CHECK_EQ_INT(u.holds, 1);
    pc_tbg_under_disarm(&u);
    CHECK(!pc_tbg_under_check(&u, 5, 9, 8000, 5000));        /* the window is over: not charged, the hold count stays */
    CHECK_EQ_INT(u.holds, 1);
    pc_tbg_under_arm(&u, 5, 9);
    CHECK(!pc_tbg_under_check(&u, 6, 10, 6000, 6000));       /* another source meanwhile: never charged, and the arm is gone */
    CHECK(!pc_tbg_under_check(&u, 6, 11, 6000, 6100));
    pc_tbg_under_arm(&u, 6, 11);
    CHECK(!pc_tbg_under_check(&u, 6, 12, PC_TBG_START_MS - 1, 7000));   /* the source's first second (its own reconfigure) */
    CHECK(pc_tbg_under_check(&u, 6, 13, PC_TBG_START_MS, 4000 + PC_TBG_QUIET_MS + 1));   /* after the quiet period: a new hold */
    CHECK_EQ_INT(u.holds, 2);
    pc_tbg_under_arm(&u, 0, 0);                              /* nothing loaded: arming does nothing */
    CHECK(!u.armed);
    pc_tbg_under_t v = { 0 };
    pc_tbg_under_arm(&v, 7, 0);
    CHECK(pc_tbg_under_check(&v, 7, 1, 2000, 100));
    CHECK_EQ_INT(pc_tbg_under_age(&v, 50), 0);               /* a clock that went back: 0, never a wrap */
}

/* review M4 / I1: a Bluetooth sink's dry block counts only in a steady stream (not while the ring fills at a start or after a pause) */
static void test_sink_block(void)
{
    pc_tbg_sink_t s = { 0 };
    CHECK(!pc_tbg_sink_block(&s, 0, true, 0));                  /* nothing fed yet (stopped, paused) */
    CHECK(!pc_tbg_sink_block(&s, 0, false, 100));               /* the stream starts */
    CHECK(!pc_tbg_sink_block(&s, 0, true, 100 + PC_TBG_SINK_STEADY_MS - 1));   /* dry within the first second: the ring was filling */
    CHECK(!pc_tbg_sink_block(&s, 0, false, 2000));
    CHECK(!pc_tbg_sink_block(&s, 0, false, 2500));
    CHECK(pc_tbg_sink_block(&s, 0, true, 2000 + PC_TBG_SINK_STEADY_MS));       /* dry after a steady second: counted */
    CHECK(!pc_tbg_sink_block(&s, 0, true, 3100));               /* the dry stretch goes on (a pause): once only */
    CHECK(!pc_tbg_sink_block(&s, 0, true, 9000));
    CHECK(!pc_tbg_sink_block(&s, 0, false, 9010));              /* resumed: a new steady second first */
    CHECK(!pc_tbg_sink_block(&s, 0, true, 9500));
}

/* review I2: no table read from the card during hi-res or while a source opens; otherwise only when someone needs the numbers */
/* re-review N1: a pass held by 手動 / scan / opening / user (the gate closed, no work) never charges an underrun; a step that did nothing closes its window */
static void test_held_not_charged(void)
{
    const int held[] = { PC_TBG_MANUAL, PC_TBG_SCAN, PC_TBG_OPENING, PC_TBG_USER, PC_TBG_HIRES };
    pc_tbg_under_t u = { 0 };
    uint32_t count = 0;
    int64_t t = 10000;
    for (int k = 0; k < 20; k++) {                               /* every second for 20 s: the held step, then underruns during its wait */
        int r = held[k % 5];
        pc_tbg_under_step(&u, r, 3, count);
        pc_tbg_under_step_end(&u, false);
        count += 2;
        CHECK(!pc_tbg_under_check(&u, 3, count, 60000, t += 1000));   /* the wake's check */
        pc_tbg_under_disarm(&u);
    }
    CHECK_EQ_INT(u.holds, 0);
    pc_tbg_under_step(&u, PC_TBG_RUN, 3, count);                 /* the gate opens, but the step finds nothing to do: closed before the wait */
    pc_tbg_under_step_end(&u, false);
    CHECK(!pc_tbg_under_check(&u, 3, count + 1, 60000, t += 1000));
    CHECK_EQ_INT(u.holds, 0);
    pc_tbg_under_step(&u, PC_TBG_RUN, 3, count + 1);             /* a step that works: its underrun is charged */
    pc_tbg_under_step_end(&u, true);
    CHECK(pc_tbg_under_check(&u, 3, count + 2, 60000, t += 1000));
    CHECK_EQ_INT(u.holds, 1);
}

/* re-review N2: a new sink session starts unfed */
static void test_sink_session(void)
{
    pc_tbg_sink_t s = { 0 };
    CHECK(!pc_tbg_sink_block(&s, 1, false, 0));
    CHECK(!pc_tbg_sink_block(&s, 1, false, 5000));               /* a steady stream ... */
    CHECK(!pc_tbg_sink_block(&s, 2, true, 6000));                /* ... ends; the next session's first dry block (its ring filling): not counted */
    CHECK(!pc_tbg_sink_block(&s, 2, false, 6100));
    CHECK(pc_tbg_sink_block(&s, 2, true, 7100));                 /* a steady second in the new session: counted */
}

static void test_may_read_table(void)
{
    CHECK(!pc_tbg_may_read_table(true, false, true, true));
    CHECK(!pc_tbg_may_read_table(false, true, true, true));
    CHECK(!pc_tbg_may_read_table(false, false, false, false));   /* 手動 at boot, nobody looking: lazy */
    CHECK(pc_tbg_may_read_table(false, false, true, false));
    CHECK(pc_tbg_may_read_table(false, false, false, true));
}

static void test_state(void)
{
    CHECK_EQ_INT(pc_tbg_state(false, PC_TBG_RUN), PC_TBG_ST_DONE);
    CHECK_EQ_INT(pc_tbg_state(true, PC_TBG_RUN), PC_TBG_ST_BUILDING);
    CHECK_EQ_INT(pc_tbg_state(true, PC_TBG_HIRES), PC_TBG_ST_HELD);
    CHECK_EQ_INT(pc_tbg_state(true, PC_TBG_MANUAL), PC_TBG_ST_MANUAL);
    CHECK_EQ_INT(pc_tbg_state(false, PC_TBG_MANUAL), PC_TBG_ST_DONE);
    CHECK_EQ_INT(pc_tbg_state(false, PC_TBG_FULL), PC_TBG_ST_HELD);   /* writes stopped after the pass: said, not 完成 */
    CHECK_EQ_INT(pc_tbg_pct(0, 0), 100);
    CHECK_EQ_INT(pc_tbg_pct(0, 70), 0);
    CHECK_EQ_INT(pc_tbg_pct(23, 70), 32);
    CHECK_EQ_INT(pc_tbg_pct(69, 70), 98);
    CHECK_EQ_INT(pc_tbg_pct(70, 70), 100);
    CHECK_EQ_INT(pc_tbg_pct(80, 70), 100);
    CHECK_EQ_INT(pc_tbg_pct(4000000000u, 4100000000u), 97);   /* no 32-bit overflow */
}

static void test_names(void)
{
    for (int w = 0; w < PC_TBG_N; w++) {
        CHECK(pc_tbg_why_name(w) && *pc_tbg_why_name(w));
        CHECK(pc_tbg_why_text(w, true) && pc_tbg_why_text(w, false));
        CHECK_EQ_INT(pc_tbg_why_of(pc_tbg_why_name(w)), w);
    }
    CHECK_STR(pc_tbg_why_name(PC_TBG_N), "?");
    CHECK_STR(pc_tbg_why_text(PC_TBG_HIRES, true), "高取樣率");
    CHECK_STR(pc_tbg_why_text(PC_TBG_HIRES, false), "hi-res");
    CHECK_STR(pc_tbg_why_text(PC_TBG_UNDERRUN, true), "播放不順");
    /* thumb_svc's own words */
    CHECK_EQ_INT(pc_tbg_why_of("hires"), PC_TBG_HIRES);
    CHECK_EQ_INT(pc_tbg_why_of("opening"), PC_TBG_OPENING);
    CHECK_EQ_INT(pc_tbg_why_of("playing"), PC_TBG_STOP_NEEDED);
    CHECK_EQ_INT(pc_tbg_why_of("space_unknown"), PC_TBG_STOP_NEEDED);
    CHECK_EQ_INT(pc_tbg_why_of("low_internal"), PC_TBG_LOW_MEM);
    CHECK_EQ_INT(pc_tbg_why_of("library_scan"), PC_TBG_SCAN);
    CHECK_EQ_INT(pc_tbg_why_of("no_card"), PC_TBG_NO_CARD);
    CHECK_EQ_INT(pc_tbg_why_of("space"), PC_TBG_FULL);
    CHECK_EQ_INT(pc_tbg_why_of("full"), PC_TBG_FULL);
    CHECK_EQ_INT(pc_tbg_why_of("table_full"), PC_TBG_TABLE_FULL);
    CHECK_EQ_INT(pc_tbg_why_of("write"), PC_TBG_WRITE);
    CHECK_EQ_INT(pc_tbg_why_of("cap"), PC_TBG_WRITE);
    CHECK_EQ_INT(pc_tbg_why_of("open_failed"), PC_TBG_WRITE);
    CHECK_EQ_INT(pc_tbg_why_of(NULL), PC_TBG_RUN);
    CHECK_EQ_INT(pc_tbg_why_of("nosuch"), PC_TBG_WRITE);
}

static void test_texts(void)
{
    char b[96];
    pc_tbg_prog_t p = { .state = PC_TBG_ST_BUILDING, .done = 23, .total = 70 };
    pc_tbg_state_text(&p, true, b, sizeof b); CHECK_STR(b, "建置中");
    pc_tbg_state_text(&p, false, b, sizeof b); CHECK_STR(b, "Building");
    pc_tbg_count_text(&p, true, b, sizeof b); CHECK_STR(b, "23 / 70・32%・剩 47 首");
    pc_tbg_count_text(&p, false, b, sizeof b); CHECK_STR(b, "23 / 70 · 32% · 47 left");
    p.state = PC_TBG_ST_HELD; p.why = PC_TBG_HIRES;
    pc_tbg_state_text(&p, true, b, sizeof b); CHECK_STR(b, "暫停：高取樣率");
    pc_tbg_state_text(&p, false, b, sizeof b); CHECK_STR(b, "Paused: hi-res");
    p.why = PC_TBG_USER;
    pc_tbg_state_text(&p, true, b, sizeof b); CHECK_STR(b, "暫停：使用者暫停");
    p.state = PC_TBG_ST_DONE; p.done = 70;
    pc_tbg_state_text(&p, true, b, sizeof b); CHECK_STR(b, "完成");
    pc_tbg_count_text(&p, true, b, sizeof b); CHECK_STR(b, "70 / 70・100%・剩 0 首");
    p.state = PC_TBG_ST_MANUAL;
    pc_tbg_state_text(&p, true, b, sizeof b); CHECK_STR(b, "手動");
    pc_tbg_state_text(&p, false, b, sizeof b); CHECK_STR(b, "Manual");
    p.done = p.total = 0;
    pc_tbg_count_text(&p, true, b, sizeof b); CHECK_STR(b, "沒有封面");
    pc_tbg_count_text(&p, false, b, sizeof b); CHECK_STR(b, "No covers");
    p.done = 3; p.total = 2;                                 /* more done than total (a track lost its picture meanwhile): 0 left */
    pc_tbg_count_text(&p, true, b, sizeof b); CHECK_STR(b, "3 / 2・100%・剩 0 首");
    char s[8];
    pc_tbg_prog_t q = { .state = PC_TBG_ST_HELD, .why = PC_TBG_UNDERRUN };
    pc_tbg_state_text(&q, true, s, sizeof s);                /* cut whole: never inside a UTF-8 sequence */
    CHECK_STR(s, "暫停");
}

/* fix round 0: unchecked songs apart (C5), a pending compaction said (C4), the 現況 row only with something to see (C6) */
static void test_fix0(void)
{
    char b[96];
    pc_tbg_prog_t p = { .state = PC_TBG_ST_BUILDING, .done = 23, .total = 70, .unchecked = 5 };
    pc_tbg_count_text(&p, true, b, sizeof b); CHECK_STR(b, "23 / 70・32%・剩 47 首・未檢查 5 首");
    pc_tbg_count_text(&p, false, b, sizeof b); CHECK_STR(b, "23 / 70 · 32% · 47 left · 5 not checked");
    p.done = p.total = 0;
    pc_tbg_count_text(&p, true, b, sizeof b); CHECK_STR(b, "沒有封面・未檢查 5 首");
    p = (pc_tbg_prog_t){ .state = PC_TBG_ST_DONE, .done = 70, .total = 70, .tidy = true };
    pc_tbg_state_text(&p, true, b, sizeof b); CHECK_STR(b, "完成・整理待停止");
    pc_tbg_state_text(&p, false, b, sizeof b); CHECK_STR(b, "Done · tidy-up waits for a stop");
    CHECK(pc_tbg_status_row(&p));
    p.tidy = false;
    CHECK(!pc_tbg_status_row(&p));                            /* 完成, nothing pending: no row */
    p.state = PC_TBG_ST_BUILDING; CHECK(pc_tbg_status_row(&p));
    p.state = PC_TBG_ST_HELD; CHECK(pc_tbg_status_row(&p));
    p.state = PC_TBG_ST_MANUAL; p.done = 70; CHECK(!pc_tbg_status_row(&p));   /* 手動 with nothing left */
    p.done = 69; CHECK(pc_tbg_status_row(&p));
    p.done = 70; p.unchecked = 1; CHECK(pc_tbg_status_row(&p));
}

int main(void)
{
    RUN(test_runs_stopped_and_playing);
    RUN(test_holds_and_precedence);
    RUN(test_space_needs_a_stop);
    RUN(test_underrun_quiet_and_giveup);
    RUN(test_breather);
    RUN(test_under_tracker);
    RUN(test_sink_block);
    RUN(test_may_read_table);
    RUN(test_held_not_charged);
    RUN(test_sink_session);
    RUN(test_state);
    RUN(test_names);
    RUN(test_texts);
    RUN(test_fix0);
    return 0;
}
