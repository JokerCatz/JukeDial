// SPDX-License-Identifier: LGPL-2.1-or-later
/* 電台調頻尋寶 / Radio hunt (G3): stations, signal, lock, rules, golden bonus, the sound schedule. */
#include "pc/pc_game_radio.h"
#include "pc_test.h"

static bool has(const pc_radio_t *g, int ev) { for (int i = 0; i < g->n_ev; i++) if (g->ev[i] == ev) return true; return false; }
static void goto_pos(pc_radio_t *g, int to) { pc_radio_tune(g, to - g->pos); }

static void test_stations(void)
{
    for (int rule = 0; rule < PC_RADIO_RULES; rule++)
        for (uint32_t seed = 1; seed < 300; seed++) {
            pc_radio_t g;
            pc_radio_init(&g, rule, seed);
            CHECK_EQ_INT(g.n, rule == PC_RADIO_TIMED ? 1 : 3);
            CHECK_EQ_INT(g.pos, 0);
            for (int i = 0; i < g.n; i++) {
                CHECK(g.st[i].pos >= 6 && g.st[i].pos <= PC_RADIO_RANGE - 6);
                CHECK(g.st[i].pos >= PC_RADIO_APART);                 /* never on the start */
                CHECK(g.st[i].tune < 4);
                for (int j = 0; j < i; j++) CHECK(g.st[i].pos - g.st[j].pos >= PC_RADIO_APART || g.st[j].pos - g.st[i].pos >= PC_RADIO_APART);
            }
        }
    pc_radio_t a, b;
    pc_radio_init(&a, 0, 77);
    pc_radio_init(&b, 0, 77);
    for (int i = 0; i < 3; i++) CHECK_EQ_INT(a.st[i].pos, b.st[i].pos);   /* the seed decides */
    pc_radio_init(&b, 0, 78);
    CHECK(a.st[0].pos != b.st[0].pos || a.st[1].pos != b.st[1].pos);
    int gold = 0;                                                       /* about one station in ten is golden */
    for (uint32_t seed = 1; seed <= 400; seed++) { pc_radio_init(&a, 0, seed); for (int i = 0; i < 3; i++) gold += a.st[i].gold; }
    CHECK(gold > 60 && gold < 180);
}
static void test_signal(void)
{
    CHECK_EQ_INT(pc_radio_signal_of(0, 10), 100);
    CHECK_EQ_INT(pc_radio_signal_of(2, 10), 80);
    CHECK_EQ_INT(pc_radio_signal_of(10, 10), 0);
    CHECK_EQ_INT(pc_radio_signal_of(40, 10), 0);
    CHECK_EQ_INT(pc_radio_signal_of(0, 0), 0);
    CHECK(pc_radio_reach(PC_RADIO_WEAK, false) < pc_radio_reach(PC_RADIO_CLASSIC, false));
    CHECK(pc_radio_reach(PC_RADIO_CLASSIC, true) < pc_radio_reach(PC_RADIO_CLASSIC, false));   /* the golden one is a narrower signal */
    pc_radio_t g;
    pc_radio_init(&g, PC_RADIO_CLASSIC, 5);
    for (int i = 0; i < 3; i++) g.st[i].gold = false;
    const int p = g.st[0].pos, r = pc_radio_reach(PC_RADIO_CLASSIC, false);
    goto_pos(&g, p);
    CHECK_EQ_INT(pc_radio_signal(&g), 100);
    CHECK(pc_radio_can_lock(&g));
    goto_pos(&g, p + r);
    CHECK(pc_radio_signal(&g) < 100);
    goto_pos(&g, 0);
    CHECK_EQ_INT(pc_radio_signal(&g), 0);
    CHECK(!pc_radio_can_lock(&g));
    goto_pos(&g, p + 2);                                                /* the lock window: signal >= 80 */
    CHECK(pc_radio_can_lock(&g));
    goto_pos(&g, p + 3);
    CHECK(!pc_radio_can_lock(&g));
}
static void test_lock_and_win(void)
{
    pc_radio_t g;
    pc_radio_init(&g, PC_RADIO_CLASSIC, 9);
    for (int i = 0; i < 3; i++) g.st[i].gold = false;
    goto_pos(&g, 0);
    pc_radio_lock(&g);
    CHECK(has(&g, PC_RADIO_EV_MISS));
    CHECK_EQ_INT(g.attempts, 1);
    CHECK_EQ_INT(g.misses, 1);
    CHECK(g.started);
    goto_pos(&g, g.st[0].pos);
    pc_radio_lock(&g);
    CHECK(has(&g, PC_RADIO_EV_LOCK) && !has(&g, PC_RADIO_EV_WON));
    CHECK_EQ_INT(g.found, 1);
    CHECK(g.st[0].locked);
    pc_radio_lock(&g);                                                  /* the locked one no longer counts: a miss */
    CHECK(has(&g, PC_RADIO_EV_MISS));
    CHECK_EQ_INT(pc_radio_nearest(&g) == 0, 0);                          /* the nearest still to find is another */
    for (int i = 1; i < 3; i++) { goto_pos(&g, g.st[i].pos); pc_radio_lock(&g); }
    CHECK(has(&g, PC_RADIO_EV_WON));
    CHECK_EQ_INT(g.state, PC_RADIO_WON);
    CHECK_EQ_INT(g.found, 3);
    CHECK_EQ_INT(pc_radio_nearest(&g), -1);
    CHECK_EQ_INT(pc_radio_signal(&g), 0);
    CHECK(!pc_radio_running(&g));
    const int pos = g.pos;                                              /* after the end nothing moves */
    pc_radio_tune(&g, 5);
    CHECK_EQ_INT(g.pos, pos);
}
static void test_clamp_and_clock(void)
{
    pc_radio_t g;
    pc_radio_init(&g, PC_RADIO_CLASSIC, 3);
    pc_radio_tune(&g, -5);
    CHECK_EQ_INT(g.pos, 0);                                             /* a gauge: no way round */
    pc_radio_tune(&g, 500);
    CHECK_EQ_INT(g.pos, PC_RADIO_RANGE - 1);
    pc_radio_tick(&g, 100);
    CHECK_EQ_INT(g.ms, 100);
    pc_radio_init(&g, PC_RADIO_CLASSIC, 3);
    CHECK(!pc_radio_running(&g));
    pc_radio_tick(&g, 100);
    CHECK_EQ_INT(g.ms, 0);                                              /* the clock starts at the first tune */
    pc_radio_tune(&g, 1);
    CHECK(pc_radio_running(&g));
    pc_radio_tick(&g, 1000);
    CHECK_EQ_INT(g.ms, 1000);
    CHECK_EQ_INT(pc_radio_time_ms(&g), 1000);
    CHECK_EQ_INT(pc_radio_left_ms(&g), 0);                              /* not timed */
    CHECK_EQ_INT(pc_radio_hub10(0), 800);
    CHECK_EQ_INT(pc_radio_hub10(99), 998);
}
static void test_timed(void)
{
    pc_radio_t g;
    pc_radio_init(&g, PC_RADIO_TIMED, 11);
    g.st[0].gold = false;
    CHECK_EQ_INT(pc_radio_left_ms(&g), PC_RADIO_TIME_MS);
    goto_pos(&g, g.st[0].pos);
    pc_radio_lock(&g);
    CHECK(has(&g, PC_RADIO_EV_LOCK) && !has(&g, PC_RADIO_EV_WON));
    CHECK_EQ_INT(g.found, 1);
    CHECK_EQ_INT(g.state, PC_RADIO_PLAY);                               /* never won: it goes on */
    CHECK(!g.st[0].locked);                                             /* a new station took its place, one at a time */
    CHECK(g.st[0].pos >= 6 && g.st[0].pos <= PC_RADIO_RANGE - 6);
    int d = g.st[0].pos - g.pos; if (d < 0) d = -d;
    CHECK(d >= PC_RADIO_APART);
    for (int k = 0; k < 20; k++) { g.st[0].gold = false; goto_pos(&g, g.st[0].pos); pc_radio_lock(&g); }
    CHECK_EQ_INT(g.found, 21);
    pc_radio_tick(&g, PC_RADIO_TIME_MS - 100);
    CHECK_EQ_INT(g.state, PC_RADIO_PLAY);
    CHECK_EQ_INT(pc_radio_left_ms(&g), 100 - 0);
    pc_radio_tick(&g, 100);
    CHECK(has(&g, PC_RADIO_EV_TIMEUP));
    CHECK_EQ_INT(g.state, PC_RADIO_TIMEUP);
    CHECK(!pc_radio_running(&g));
    CHECK_EQ_INT(pc_radio_left_ms(&g), 0);
}
static void test_gold_bonus(void)
{
    pc_radio_t g;
    pc_radio_init(&g, PC_RADIO_CLASSIC, 21);
    for (int i = 0; i < 3; i++) g.st[i].gold = i == 1;
    goto_pos(&g, g.st[1].pos);
    CHECK_EQ_INT(pc_radio_signal(&g), 100);
    pc_radio_tick(&g, 0);
    pc_radio_tune(&g, 0);
    g.started = true;
    pc_radio_tick(&g, 25000);
    pc_radio_lock(&g);
    CHECK(has(&g, PC_RADIO_EV_GOLD) && has(&g, PC_RADIO_EV_LOCK));
    CHECK(g.gold_found);
    CHECK_EQ_INT(pc_radio_time_ms(&g), 25000 - PC_RADIO_BONUS_MS);
    pc_radio_t t;
    pc_radio_init(&t, PC_RADIO_TIMED, 21);
    t.st[0].gold = true;
    goto_pos(&t, t.st[0].pos);
    pc_radio_tick(&t, 20000);
    pc_radio_lock(&t);
    CHECK(has(&t, PC_RADIO_EV_GOLD));
    CHECK_EQ_INT(pc_radio_left_ms(&t), PC_RADIO_TIME_MS + PC_RADIO_BONUS_MS - 20000);   /* timed: adds time */
    pc_radio_init(&g, PC_RADIO_CLASSIC, 21);                            /* the bonus never makes the time negative */
    g.st[0].gold = true;
    goto_pos(&g, g.st[0].pos);
    pc_radio_tick(&g, 3000);
    pc_radio_lock(&g);
    CHECK_EQ_INT(pc_radio_time_ms(&g), 0);
}
static void test_rules(void)
{
    pc_radio_t c, w;
    pc_radio_init(&c, PC_RADIO_CLASSIC, 5);
    pc_radio_init(&w, PC_RADIO_WEAK, 5);
    CHECK_EQ_INT(w.n, 3);
    CHECK(pc_radio_reach(PC_RADIO_WEAK, false) >= 2);
    /* the weak rule: a narrower lock window (the signal must still be 80 % of a narrower reach) */
    int cw = 0, ww = 0;
    for (int d = 0; d < 10; d++) { if (pc_radio_signal_of(d, pc_radio_reach(PC_RADIO_CLASSIC, false)) >= PC_RADIO_LOCK_PCT) cw++; if (pc_radio_signal_of(d, pc_radio_reach(PC_RADIO_WEAK, false)) >= PC_RADIO_LOCK_PCT) ww++; }
    CHECK(ww < cw);
}
static void test_sound(void)
{
    /* far: static, denser for the weak rule; near: notes */
    int sc = 0, sw = 0;
    for (unsigned s = 0; s < 120; s++) { sc += pc_radio_sound(PC_RADIO_CLASSIC, 0, 0, s) == 0; sw += pc_radio_sound(PC_RADIO_WEAK, 0, 0, s) == 0; }
    CHECK(sc > 0 && sw > sc);                                           /* louder static: more ticks */
    int prev = 1000;
    for (int sig = 0; sig < 40; sig += 5) {                             /* the static thins out as the signal grows */
        int n = 0;
        for (unsigned s = 0; s < 120; s++) n += pc_radio_sound(PC_RADIO_CLASSIC, sig, 0, s) == 0;
        CHECK(n <= prev);
        prev = n;
    }
    prev = -1;
    for (int sig = 40; sig <= 100; sig += 10) {                         /* the melody fragment grows: more notes per cycle */
        int notes = 0;
        for (unsigned s = 0; s < 12; s++) { int v = pc_radio_sound(PC_RADIO_CLASSIC, sig, 2, s); CHECK(v >= -1 && v <= PC_RADIO_MELODY); notes += v >= 1; }
        CHECK(notes >= prev);
        prev = notes;
    }
    CHECK_EQ_INT(prev, 4);                                              /* the whole fragment at the lock level */
    int none = 0;
    for (unsigned s = 0; s < 12; s++) none += pc_radio_sound(PC_RADIO_CLASSIC, 100, 1, s) == 0;
    CHECK_EQ_INT(none, 0);                                              /* clear: no static in it */
    CHECK_EQ_INT(pc_radio_shake10(100), 0);
    CHECK_EQ_INT(pc_radio_shake10(40), 0);
    CHECK(pc_radio_shake10(0) > pc_radio_shake10(30) && pc_radio_shake10(0) <= 30);
    for (int t = 0; t < 4; t++) {                                       /* a station's fragment: each tune its own order */
        int a = 0, b = 0;
        for (unsigned s = 0; s < 12; s++) { a = a * 5 + pc_radio_sound(PC_RADIO_CLASSIC, 100, t, s) + 1; b = b * 5 + pc_radio_sound(PC_RADIO_CLASSIC, 100, (t + 1) % 4, s) + 1; }
        CHECK(a != b);
    }
}
int main(void)
{
    RUN(test_stations);
    RUN(test_signal);
    RUN(test_lock_and_win);
    RUN(test_clamp_and_clock);
    RUN(test_timed);
    RUN(test_gold_bonus);
    RUN(test_rules);
    RUN(test_sound);
    return 0;
}
