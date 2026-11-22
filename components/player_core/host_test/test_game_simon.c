// SPDX-License-Identifier: LGPL-2.1-or-later
/* 四色記憶 / Simon (G1): the sequence, the four rules, the timing, the milestones. */
#include "pc/pc_game_simon.h"
#include "pc/pc_game_score.h"
#include "pc_test.h"

/* plays the round as the rule wants it (every tap right); returns the last result */
static int play_round(pc_simon_t *g)
{
    int r = PC_SIMON_IGNORED;
    for (int k = 0; k < g->len; k++) r = pc_simon_tap(g, pc_simon_expect(g));
    return r;
}

static void test_classic_grows_and_repeats(void)
{
    pc_simon_t g;
    pc_simon_init(&g, PC_SIMON_CLASSIC, 1234);
    CHECK_EQ_INT(g.len, 0);
    CHECK_EQ_INT(pc_simon_expect(&g), -1);
    CHECK_EQ_INT(pc_simon_tap(&g, 0), PC_SIMON_IGNORED);       /* nothing to repeat yet */
    for (int c = 0; c < PC_SIMON_COLOURS; c++) CHECK_EQ_INT(g.place[c], c);
    uint8_t first[8];
    for (int round = 1; round <= 8; round++) {
        CHECK(!pc_simon_grow(&g));                              /* classic: the colours never move */
        CHECK_EQ_INT(g.len, round);
        CHECK_EQ_INT(g.pos, 0);
        for (int k = 0; k < round - 1; k++) CHECK_EQ_INT(g.seq[k], first[k]);   /* the sequence only grows at its end */
        first[round - 1] = g.seq[round - 1];
        CHECK(g.seq[round - 1] < PC_SIMON_COLOURS);
        for (int k = 0; k < round; k++) CHECK_EQ_INT(pc_simon_shown(&g, k), g.place[g.seq[k]]);
        CHECK_EQ_INT(pc_simon_shown(&g, round), -1);
        for (int k = 0; k < round; k++) {
            CHECK_EQ_INT(pc_simon_expect(&g), pc_simon_shown(&g, k));   /* classic: in the order played */
            CHECK_EQ_INT(pc_simon_tap(&g, pc_simon_expect(&g)), k == round - 1 ? PC_SIMON_ROUND : PC_SIMON_OK);
        }
        CHECK_EQ_INT(g.done, round);
        CHECK_EQ_INT(pc_simon_tap(&g, 0), PC_SIMON_IGNORED);   /* the round is complete: a tap before the next one is not counted */
    }
    /* the same seed, the same sequence; another seed, another one */
    pc_simon_t h, k;
    pc_simon_init(&h, PC_SIMON_CLASSIC, 1234);
    pc_simon_init(&k, PC_SIMON_CLASSIC, 1235);
    int same = 1, diff = 0;
    for (int i = 0; i < 8; i++) { pc_simon_grow(&h); pc_simon_grow(&k); same &= h.seq[i] == first[i]; diff += k.seq[i] != first[i]; }
    CHECK(same);
    CHECK(diff > 0);
}

static void test_wrong_ends(void)
{
    pc_simon_t g;
    pc_simon_init(&g, PC_SIMON_CLASSIC, 7);
    pc_simon_grow(&g);
    CHECK_EQ_INT(play_round(&g), PC_SIMON_ROUND);
    pc_simon_grow(&g);
    CHECK_EQ_INT(pc_simon_tap(&g, pc_simon_expect(&g)), PC_SIMON_OK);
    int wrong = (pc_simon_expect(&g) + 1) % PC_SIMON_COLOURS;
    CHECK_EQ_INT(pc_simon_tap(&g, wrong), PC_SIMON_WRONG);
    CHECK(g.over);
    CHECK_EQ_INT(g.done, 1);                                    /* the reached length: the rounds repeated in full */
    CHECK_EQ_INT(pc_simon_tap(&g, 0), PC_SIMON_IGNORED);
    CHECK(!pc_simon_grow(&g));                                  /* over: no next round */
    CHECK_EQ_INT(g.len, 2);
    CHECK_EQ_INT(pc_simon_tap(&g, -1), PC_SIMON_IGNORED);
    /* practice: the same round again from its start */
    pc_simon_retry(&g);
    CHECK(!g.over);
    CHECK_EQ_INT(g.pos, 0);
    CHECK_EQ_INT(g.len, 2);
    CHECK_EQ_INT(play_round(&g), PC_SIMON_ROUND);
    CHECK_EQ_INT(g.done, 2);
    /* a tap out of range is wrong while the round runs */
    pc_simon_grow(&g);
    CHECK_EQ_INT(pc_simon_tap(&g, 9), PC_SIMON_WRONG);
}

static void test_reverse(void)
{
    pc_simon_t g;
    pc_simon_init(&g, PC_SIMON_REVERSE, 99);
    for (int round = 1; round <= 6; round++) {
        pc_simon_grow(&g);
        for (int k = 0; k < round; k++) {
            CHECK_EQ_INT(pc_simon_expect(&g), pc_simon_shown(&g, round - 1 - k));   /* the last played first */
            pc_simon_tap(&g, pc_simon_expect(&g));
        }
        CHECK_EQ_INT(g.done, round);
    }
    /* the forward order is wrong in REVERSE once the two ends differ */
    pc_simon_t h;
    for (uint32_t s = 1; s < 200; s++) {
        pc_simon_init(&h, PC_SIMON_REVERSE, s);
        pc_simon_grow(&h); play_round(&h); pc_simon_grow(&h);
        if (h.seq[0] == h.seq[1]) continue;
        CHECK_EQ_INT(pc_simon_tap(&h, pc_simon_shown(&h, 0)), PC_SIMON_WRONG);
        return;
    }
    CHECK(0);
}

static void test_shuffle(void)
{
    pc_simon_t g;
    pc_simon_init(&g, PC_SIMON_SHUFFLE, 4242);
    int moved_at[8], nm = 0;
    for (int round = 1; round <= 21; round++) {
        uint8_t before[PC_SIMON_COLOURS];
        memcpy(before, g.place, sizeof before);
        bool moved = pc_simon_grow(&g);
        if (moved) {
            moved_at[nm++] = round;
            int seen = 0;
            for (int c = 0; c < PC_SIMON_COLOURS; c++) {
                CHECK(g.place[c] != before[c]);                 /* every colour leaves its sector */
                CHECK(g.place[c] < PC_SIMON_COLOURS);
                seen |= 1 << g.place[c];
            }
            CHECK_EQ_INT(seen, 15);                             /* still one colour a sector */
        } else CHECK(!memcmp(before, g.place, sizeof before));
        for (int k = 0; k < round; k++) CHECK_EQ_INT(pc_simon_shown(&g, k), g.place[g.seq[k]]);   /* shown where the colour is now */
        CHECK_EQ_INT(play_round(&g), PC_SIMON_ROUND);
    }
    CHECK_EQ_INT(nm, 4);                                        /* after rounds 5, 10, 15, 20: at the start of 6, 11, 16, 21 */
    CHECK_EQ_INT(moved_at[0], 6);
    CHECK_EQ_INT(moved_at[3], 21);
    CHECK_EQ_INT(g.moves, 4);
    /* the colour's old sector is wrong after a move (the sequence is of colours) */
    pc_simon_t h;
    pc_simon_init(&h, PC_SIMON_SHUFFLE, 4242);
    uint8_t old[PC_SIMON_COLOURS];
    for (int r = 1; r <= 5; r++) { pc_simon_grow(&h); play_round(&h); }
    memcpy(old, h.place, sizeof old);
    CHECK(pc_simon_grow(&h));
    CHECK_EQ_INT(pc_simon_tap(&h, old[h.seq[0]]), PC_SIMON_WRONG);
    /* other rules never move the colours */
    for (int rule = 0; rule < PC_SIMON_RULES; rule++) {
        if (rule == PC_SIMON_SHUFFLE) continue;
        pc_simon_init(&h, rule, 5);
        for (int r = 1; r <= 12; r++) { CHECK(!pc_simon_grow(&h)); play_round(&h); }
        for (int c = 0; c < PC_SIMON_COLOURS; c++) CHECK_EQ_INT(h.place[c], c);
    }
}

static void test_timing(void)
{
    int on, gap, prev = 100000;
    for (int len = 1; len <= 30; len++) {                       /* classic: steps at 6 and 14, never faster in between */
        pc_simon_timing(PC_SIMON_CLASSIC, len, &on, &gap);
        CHECK(on <= prev);
        CHECK(on >= 200 && gap >= 80 && on <= 500);
        if (len == 5 || len == 6 || len == 13 || len == 14) printf("classic len %d: on %d gap %d\n", len, on, gap);
        prev = on;
    }
    int on5, on6;
    pc_simon_timing(PC_SIMON_CLASSIC, 2, &on5, &gap); pc_simon_timing(PC_SIMON_CLASSIC, 5, &on6, &gap);
    CHECK_EQ_INT(on5, on6);                                     /* classic: the same speed for 1..5 */
    pc_simon_timing(PC_SIMON_REVERSE, 3, &on, &gap); CHECK_EQ_INT(on, on5);   /* REVERSE and SHUFFLE take the classic speed */
    prev = 100000;
    int strict = 0;
    for (int len = 1; len <= 40; len++) {                       /* FAST: faster every round until its floor */
        pc_simon_timing(PC_SIMON_FAST, len, &on, &gap);
        CHECK(on >= 120 && gap >= 60);
        CHECK(on <= prev);
        strict += on < prev;
        prev = on;
    }
    CHECK(strict >= 10);
    pc_simon_timing(PC_SIMON_FAST, 10, &on, &gap);
    pc_simon_timing(PC_SIMON_CLASSIC, 10, &on5, &gap);
    CHECK(on < on5);
}

static void test_milestones_and_max(void)
{
    CHECK(!pc_simon_milestone(0));
    CHECK(!pc_simon_milestone(4));
    CHECK(pc_simon_milestone(5));
    CHECK(!pc_simon_milestone(6));
    CHECK(pc_simon_milestone(10));
    pc_simon_t g;
    pc_simon_init(&g, PC_SIMON_FAST, 3);
    for (int r = 1; r <= PC_SIMON_MAX; r++) { CHECK(!pc_simon_grow(&g) || 0); CHECK_EQ_INT(play_round(&g), PC_SIMON_ROUND); }
    CHECK_EQ_INT(g.done, PC_SIMON_MAX);
    CHECK(g.over);                                              /* the longest sequence repeated: the game is over (a win) */
    CHECK(!pc_simon_grow(&g));
    CHECK_EQ_INT(g.len, PC_SIMON_MAX);
    pc_simon_init(&g, 77, 3);                                   /* a rule out of range: classic */
    CHECK_EQ_INT(g.rule, PC_SIMON_CLASSIC);
    pc_simon_init(&g, PC_SIMON_CLASSIC, 0);                     /* seed 0 still makes colours */
    int seen = 0;
    for (int r = 0; r < 40; r++) { pc_simon_grow(&g); seen |= 1 << g.seq[r]; play_round(&g); }
    CHECK_EQ_INT(seen, 15);
}

/* fix round 1 (I1): the seed mixes a value taken once per boot: another boot value = another first sequence, the same one = the same; 0 = the bare hash */
static void test_boot_seed(void)
{
    uint32_t boots[] = { 0, 1, 0xdeadbeef, 12345 };
    uint8_t first[4][12];
    for (int b = 0; b < 4; b++) {
        pc_simon_t g;
        pc_simon_init(&g, PC_SIMON_CLASSIC, pc_game_seed("simon", 0, boots[b]));
        for (int r = 0; r < 12; r++) { pc_simon_grow(&g); play_round(&g); }
        memcpy(first[b], g.seq, 12);
        pc_simon_t h;
        pc_simon_init(&h, PC_SIMON_CLASSIC, pc_game_seed("simon", 0, boots[b]));
        for (int r = 0; r < 12; r++) { pc_simon_grow(&h); play_round(&h); }
        CHECK(!memcmp(h.seq, first[b], 12));                    /* the same boot value: the same game */
    }
    for (int a = 0; a < 4; a++) for (int b = a + 1; b < 4; b++) CHECK(memcmp(first[a], first[b], 12) != 0);   /* another boot value: another game */
    uint32_t h = 2166136261u;
    for (const char *p = "simon"; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    CHECK_EQ_INT(pc_game_seed("simon", 3, 0), h + 3 * 0x9E3779B9u);   /* boot 0 (the simulator) = the seeds as before: its pictures do not move */
    CHECK(pc_game_seed("simon", 3, 7) != pc_game_seed("simon", 3, 8));
    CHECK(pc_game_seed("ttt", 0, 7) != pc_game_seed("c4", 0, 7));
}

int main(void)
{
    RUN(test_boot_seed);
    RUN(test_classic_grows_and_repeats);
    RUN(test_wrong_ends);
    RUN(test_reverse);
    RUN(test_shuffle);
    RUN(test_timing);
    RUN(test_milestones_and_max);
    return 0;
}
