// SPDX-License-Identifier: LGPL-2.1-or-later
/* 節拍點擊 (ideas G3): the judging windows, combo and points, the golden ring, the three rules, the round's end, determinism. */
#include <stdio.h>
#include "pc/pc_game_beat.h"
#include "pc_test.h"

/* a started metronome game; 10 ms steps; a tap at target + off_ms for every ring (off_ms 0: dead on); ends with the round or at limit_ms */
static void run_auto(pc_gb_t *g, int off_ms, int limit_ms)
{
    pc_gb_tap(g, 0);                                           /* start */
    for (int t = 0; t < limit_ms && g->state == PC_GB_PLAY; t += 10) {
        pc_gb_step(g, 10, false, 0, 0);
        for (int i = 0; i < PC_GB_RINGS; i++)
            if (g->ring[i].live && (int32_t)(g->now - g->ring[i].target) >= off_ms) { pc_gb_tap(g, 0); break; }
    }
}
static void test_perfect_round(void)
{
    pc_gb_t g;
    pc_gb_init(&g, PC_GB_METRO, 0, 7);
    pc_gb_set_bpm(&g, 120);
    run_auto(&g, 0, 60000);
    CHECK_EQ_INT(g.state, PC_GB_OVER);
    CHECK_EQ_INT(g.perfect, PC_GB_ROUND);
    CHECK_EQ_INT(g.miss + g.stray + g.good, 0);
    CHECK_EQ_INT(g.combo_best, PC_GB_ROUND);
    uint32_t sum = 0;                                        /* the multiplier 1.0 .. 4.0 (capped at combo 30) */
    for (int c = 0; c < PC_GB_ROUND; c++) sum += 100u * (10u + (c < 30 ? (uint32_t)c : 30u)) / 10u;
    CHECK(g.points >= sum && g.points <= 2 * sum);
    CHECK(g.points > sum || g.golds == 0);                   /* a golden ring doubled its ring (the next test pins one) */
}
static void test_windows(void)
{
    static const struct { int off, perfect, good, miss; } T[] = {
        { 0, 1, 0, 0 }, { 50, 1, 0, 0 }, { -50, 1, 0, 0 }, { 60, 0, 1, 0 }, { -110, 0, 1, 0 }, { 120, 0, 0, 1 }, { -220, 0, 0, 1 } };
    for (unsigned k = 0; k < sizeof T / sizeof T[0]; k++) {
        pc_gb_t g;
        pc_gb_init(&g, PC_GB_METRO, 0, 7);
        pc_gb_tap(&g, 0);
        while (!g.ring[0].live) pc_gb_step(&g, 10, false, 0, 0);
        uint32_t tgt = g.ring[0].target;
        while ((int32_t)(g.now - tgt) < T[k].off) pc_gb_step(&g, 10, false, 0, 0);
        pc_gb_tap(&g, 0);
        CHECK_EQ_INT(g.perfect, T[k].perfect);
        CHECK_EQ_INT(g.good, T[k].good);
        CHECK_EQ_INT(g.miss, T[k].miss);
        CHECK_EQ_INT(g.stray, 0);
        CHECK_EQ_INT(g.resolved, 1);
    }
}
static void test_stray_and_expiry(void)
{
    pc_gb_t g;
    pc_gb_init(&g, PC_GB_METRO, 0, 7);
    pc_gb_tap(&g, 0);
    pc_gb_step(&g, 10, false, 0, 0);
    pc_gb_tap(&g, 0);                                           /* no ring near: a stray, uses nothing, resets the combo */
    CHECK_EQ_INT(g.stray, 1);
    CHECK_EQ_INT(g.resolved, 0);
    CHECK_EQ_INT(g.ev[0].kind, PC_GB_EV_STRAY);
    while (!g.ring[0].live) pc_gb_step(&g, 10, false, 0, 0);
    uint32_t tgt = g.ring[0].target;
    while (g.now <= tgt + (uint32_t)pc_gb_window(&g, 2)) pc_gb_step(&g, 10, false, 0, 0);   /* nobody taps: the ring expires as a miss */
    CHECK_EQ_INT(g.miss, 1);
    CHECK_EQ_INT(g.resolved, 1);
    CHECK(!g.ring[0].live);
}
static void test_combo_reset(void)
{
    pc_gb_t g;
    pc_gb_init(&g, PC_GB_METRO, 0, 7);
    pc_gb_set_bpm(&g, 120);
    pc_gb_tap(&g, 0);
    int hits = 0, saw_combo5 = 0, last_pts = 0;
    for (int t = 0; t < 60000 && g.state == PC_GB_PLAY; t += 10) {
        pc_gb_step(&g, 10, false, 0, 0);
        for (int i = 0; i < PC_GB_RINGS; i++)
            if (g.ring[i].live && g.now == g.ring[i].target) {
                if (g.resolved == 6) break;                  /* the 7th ring is let go: a miss resets the combo */
                pc_gb_tap(&g, 0);
                hits++;
                for (int e = 0; e < g.n_ev; e++) { if (g.ev[e].kind == PC_GB_EV_COMBO) saw_combo5 = g.ev[e].value; if (g.ev[e].kind == PC_GB_EV_PERFECT) last_pts = (int)g.ev[e].points; }
                if (hits == 6) CHECK_EQ_INT(g.combo, 6);
                break;
            }
        if (g.resolved == 7 && g.miss == 1) break;
    }
    CHECK_EQ_INT(saw_combo5, 5);
    CHECK(last_pts >= 150);                                  /* the sixth hit: 1.5x (or 3x golden) */
    CHECK_EQ_INT(g.combo, 0);
    CHECK_EQ_INT(g.combo_best, 6);
}
static void test_gold(void)
{
    int golds = 0, rings = 0;
    for (uint32_t s = 1; s <= 400; s++) {
        pc_gb_t g;
        pc_gb_init(&g, PC_GB_METRO, 0, s);
        run_auto(&g, 0, 60000);
        golds += g.golds;
        rings += PC_GB_ROUND;
    }
    CHECK(golds * PC_GB_GOLD_N > rings * 3 / 4 && golds * PC_GB_GOLD_N < rings * 5 / 4);   /* about 1 in 16 */
    pc_gb_t a, b;                                            /* the same seed: the same golden rings and score */
    pc_gb_init(&a, PC_GB_METRO, 0, 99); run_auto(&a, 0, 60000);
    pc_gb_init(&b, PC_GB_METRO, 0, 99); run_auto(&b, 0, 60000);
    CHECK_EQ_INT(a.points, b.points);
    CHECK_EQ_INT(a.golds, b.golds);
}
static void test_metronome_bpm(void)
{
    pc_gb_t g;
    pc_gb_init(&g, PC_GB_METRO, 0, 7);
    pc_gb_set_bpm(&g, 30); CHECK_EQ_INT(g.bpm, 60);
    pc_gb_set_bpm(&g, 500); CHECK_EQ_INT(g.bpm, 120);
    pc_gb_set_bpm(&g, 90);
    pc_gb_tap(&g, 0);
    uint32_t prev = 0, gap = 0;
    for (int t = 0; t < 8000; t += 10) {
        pc_gb_step(&g, 10, false, 0, 0);
        for (int i = 0; i < PC_GB_RINGS; i++) if (g.ring[i].live && g.ring[i].target > prev) { if (prev) gap = g.ring[i].target - prev; prev = g.ring[i].target; }
    }
    CHECK_EQ_INT(gap, 666);                                  /* 60000 / 90 */
    float p = pc_gb_progress(&g, 0);
    CHECK(p >= -1.0f);
}
static void test_beat_rules(void)
{
    /* classic follows the detector's grid: a beat every 500 ms, the next in 300 ms now; the rings' targets lie on it */
    pc_gb_t g;
    pc_gb_init(&g, PC_GB_CLASSIC, 0, 7);
    pc_gb_tap(&g, 0);
    uint32_t next_abs = 300;                                  /* the grid: 300 + 500 k on the game's clock */
    for (int t = 0; t < 8000; t += 10) {
        uint32_t to_next = next_abs > g.now + 10 ? next_abs - (g.now + 10) : 0;
        if (to_next == 0) { next_abs += 500; to_next = next_abs - (g.now + 10); }
        pc_gb_step(&g, 10, true, to_next, 500);
    }
    int n = 0;
    for (int i = 0; i < PC_GB_RINGS; i++) if (g.ring[i].live) { n++; CHECK_EQ_INT((g.ring[i].target - 300) % 500, 0); }
    CHECK(n >= 1);
    CHECK(!pc_gb_no_beat(&g));
    /* off-beat: the same grid, half a period later */
    pc_gb_init(&g, PC_GB_OFFBEAT, 0, 7);
    pc_gb_tap(&g, 0);
    next_abs = 300;
    for (int t = 0; t < 8000; t += 10) {
        uint32_t to_next = next_abs > g.now + 10 ? next_abs - (g.now + 10) : 0;
        if (to_next == 0) { next_abs += 500; to_next = next_abs - (g.now + 10); }
        pc_gb_step(&g, 10, true, to_next, 500);
    }
    for (int i = 0; i < PC_GB_RINGS; i++) if (g.ring[i].live) CHECK_EQ_INT((g.ring[i].target - 300) % 500, 250);
}
static void test_no_beat(void)
{
    pc_gb_t g;
    pc_gb_init(&g, PC_GB_CLASSIC, 0, 7);
    pc_gb_tap(&g, 0);
    for (int t = 0; t < 4000; t += 10) pc_gb_step(&g, 10, false, 0, 0);
    CHECK(!pc_gb_no_beat(&g));                               /* the detector needs time to lock: not yet said */
    for (int t = 0; t < 2000; t += 10) pc_gb_step(&g, 10, false, 0, 0);
    CHECK(pc_gb_no_beat(&g));
    CHECK_EQ_INT(g.spawned, 0);                              /* no beat, no rings */
    pc_gb_step(&g, 10, true, 250, 500);                      /* the beat comes: no more said */
    CHECK(!pc_gb_no_beat(&g));
}
static void test_windows_mode(void)
{
    pc_gb_t g;
    pc_gb_init(&g, PC_GB_METRO, 1, 1);
    CHECK_EQ_INT(pc_gb_window(&g, 1), 165);
    pc_gb_init(&g, PC_GB_METRO, 2, 1);
    CHECK_EQ_INT(pc_gb_window(&g, 0), 35);
    pc_gb_init(&g, PC_GB_METRO, 9, 1);
    CHECK_EQ_INT(g.window, 0);
}
static void test_round_end_and_ready(void)
{
    pc_gb_t g;
    pc_gb_init(&g, PC_GB_METRO, 0, 7);
    pc_gb_step(&g, 100, false, 0, 0);
    CHECK_EQ_INT(g.spawned, 0);                              /* READY: nothing runs until the tap */
    run_auto(&g, 0, 60000);
    CHECK_EQ_INT(g.state, PC_GB_OVER);
    CHECK_EQ_INT(g.resolved, PC_GB_ROUND);
    CHECK_EQ_INT(g.spawned, PC_GB_ROUND);
    int over = 0;
    for (int e = 0; e < g.n_ev; e++) over += g.ev[e].kind == PC_GB_EV_OVER;
    CHECK_EQ_INT(over, 1);
    pc_gb_tap(&g, 0);                                           /* after the end a tap does nothing here (the page shows the card) */
    CHECK_EQ_INT(g.state, PC_GB_OVER);
}
static void test_lost_then_found(void)                                         /* the beat is lost for a while and found again: no rings behind the clock */
{
    pc_gb_t g;
    pc_gb_init(&g, PC_GB_CLASSIC, 0, 7);
    pc_gb_tap(&g, 0);
    uint32_t next_abs = 300;
    for (int t = 0; t < 3000; t += 10) {                                       /* locked: a beat every 500 ms */
        uint32_t to_next = next_abs > g.now + 10 ? next_abs - (g.now + 10) : 0;
        if (to_next == 0) { next_abs += 500; to_next = next_abs - (g.now + 10); }
        pc_gb_step(&g, 10, true, to_next, 500);
    }
    for (int t = 0; t < 7000; t += 10) pc_gb_step(&g, 10, false, 0, 0);       /* lost for 7 s: every ring in flight expires */
    int sp = g.spawned;
    next_abs = g.now + 120;                                                    /* found again: the grid is somewhere else now */
    for (int t = 0; t < 2000; t += 10) {
        uint32_t to_next = next_abs > g.now + 10 ? next_abs - (g.now + 10) : 0;
        if (to_next == 0) { next_abs += 500; to_next = next_abs - (g.now + 10); }
        pc_gb_step(&g, 10, true, to_next, 500);
        if (g.spawned > sp) {                                                  /* a new ring is never behind the clock: at least 600 ms ahead */
            sp = g.spawned;
            uint32_t newest = 0;
            for (int i = 0; i < PC_GB_RINGS; i++) if (g.ring[i].live && g.ring[i].target > newest) newest = g.ring[i].target;
            CHECK(newest >= g.now + 600);
        }
    }
    CHECK(g.spawned > 0);
}
static void test_tap_since(void)                                               /* the tap is judged at the clock plus the time since the last step */
{
    pc_gb_t g;
    pc_gb_init(&g, PC_GB_METRO, 0, 7);
    pc_gb_tap(&g, 0);
    while (!g.ring[0].live) pc_gb_step(&g, 33, false, 0, 0);
    while ((int32_t)(g.now - g.ring[0].target) < -30) pc_gb_step(&g, 33, false, 0, 0);
    int32_t d = (int32_t)(g.now - g.ring[0].target);                           /* -30..2: this tap alone would be within 50 ms either way... */
    pc_gb_tap(&g, (uint32_t)(-d + 100));                                       /* ...but 100 ms past the target it is good, not perfect */
    CHECK_EQ_INT(g.good, 1);
    CHECK_EQ_INT(g.perfect, 0);
}
int main(void)
{
    test_lost_then_found();
    test_tap_since();
    test_perfect_round();
    test_windows();
    test_stray_and_expiry();
    test_combo_reset();
    test_gold();
    test_metronome_bpm();
    test_beat_rules();
    test_no_beat();
    test_windows_mode();
    test_round_end_and_ready();
    printf("test_game_beat ok\n");
    return 0;
}
