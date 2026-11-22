// SPDX-License-Identifier: LGPL-2.1-or-later
/* 猜歌 / Guess the song (G4): questions, answers, combo and score, clip timing, labels. */
#include "pc/pc_game_guess.h"
#include "pc_test.h"

static const char *const TITLES[] = { "Alpha", "Beta", "Gamma", "Delta", "", "Alpha", "Epsilon", "Zeta" };
static const char *const ARTISTS[] = { "A", "A", "B", "B", "C", "C", "", "D" };
static const char *title(void *u, int i) { (void)u; return TITLES[i]; }
static const char *artist(void *u, int i) { (void)u; return ARTISTS[i]; }
static const char *same(void *u, int i) { (void)u; (void)i; return "X"; }

static void test_ready(void)
{
    CHECK_EQ_INT(pc_guess_ready(8, title, NULL), 4);          /* more than enough: counted to 4 */
    CHECK_EQ_INT(pc_guess_ready(4, title, NULL), 4);          /* Alpha Beta Gamma Delta */
    CHECK_EQ_INT(pc_guess_ready(3, title, NULL), 3);
    CHECK_EQ_INT(pc_guess_ready(8, same, NULL), 1);           /* all alike */
    CHECK_EQ_INT(pc_guess_ready(0, title, NULL), 0);
    CHECK_EQ_INT(pc_guess_ready(8, artist, NULL), 4);         /* A B C D (the empty one skipped) */
    CHECK_EQ_INT(pc_guess_ready(7, artist, NULL), 3);         /* without D */
}

static void test_ask(void)
{
    pc_guess_t g;
    pc_guess_init(&g, PC_GUESS_CLASSIC, 10, 7);
    int right_pos[4] = { 0 };
    for (int q = 0; q < 10; q++) {
        CHECK(pc_guess_ask(&g, 8, title, NULL));
        CHECK(!pc_guess_ask(&g, 8, title, NULL));              /* one question at a time */
        CHECK(g.open && g.asked == q + 1);
        CHECK_EQ_INT(g.track[g.answer], g.target);
        for (int a = 0; a < 4; a++) {
            CHECK(g.track[a] >= 0 && g.track[a] < 8 && TITLES[g.track[a]][0]);   /* never an empty title */
            for (int b = a + 1; b < 4; b++) CHECK(strcmp(TITLES[g.track[a]], TITLES[g.track[b]]));   /* four different titles */
        }
        CHECK(g.clip_pct >= 10 && g.clip_pct < 60);
        right_pos[g.answer]++;
        if (q) CHECK(g.target != g.last_target);               /* not the same track twice in a row */
        CHECK_EQ_INT(pc_guess_answer(&g, g.answer), PC_GUESS_RIGHT);
    }
    CHECK(g.over);
    CHECK(!pc_guess_ask(&g, 8, title, NULL));
    int used = 0;
    for (int a = 0; a < 4; a++) used += right_pos[a] > 0;
    CHECK(used >= 3);                                          /* the right answer is not always at one place */
}

static void test_ask_refuses(void)
{
    pc_guess_t g;
    pc_guess_init(&g, PC_GUESS_CLASSIC, 5, 1);
    CHECK(!pc_guess_ask(&g, 3, title, NULL));                  /* fewer than four tracks */
    CHECK(!pc_guess_ask(&g, 8, same, NULL));                   /* one title only */
    CHECK(!g.open && g.asked == 0);
    pc_guess_init(&g, PC_GUESS_ARTIST, 5, 1);
    CHECK(!pc_guess_ask(&g, 7, artist, NULL));                 /* three artists */
    CHECK(pc_guess_ask(&g, 8, artist, NULL));
    for (int a = 0; a < 4; a++) CHECK(ARTISTS[g.track[a]][0]);
    for (int a = 0; a < 4; a++) for (int b = a + 1; b < 4; b++) CHECK(strcmp(ARTISTS[g.track[a]], ARTISTS[g.track[b]]));
}

static void test_answer_score(void)
{
    pc_guess_t g;
    pc_guess_init(&g, PC_GUESS_CLASSIC, 8, 5);
    CHECK_EQ_INT(pc_guess_answer(&g, 0), PC_GUESS_IGNORED);    /* nothing asked */
    int rights = 0;
    uint32_t pts = 0;
    for (int q = 0; q < 8; q++) {
        CHECK(pc_guess_ask(&g, 8, title, NULL));
        CHECK_EQ_INT(pc_guess_answer(&g, 9), PC_GUESS_IGNORED);
        CHECK_EQ_INT(pc_guess_answer(&g, -1), PC_GUESS_IGNORED);
        CHECK(g.open);
        if (q == 4) {                                          /* a wrong answer breaks the streak but the game goes on */
            CHECK_EQ_INT(pc_guess_answer(&g, (g.answer + 1) % 4), PC_GUESS_WRONG);
            CHECK_EQ_INT(g.streak, 0);
            CHECK_EQ_INT(g.best_streak, 4);
            CHECK(!g.over);
        } else {
            bool gold = g.golden;
            CHECK_EQ_INT(pc_guess_answer(&g, g.answer), PC_GUESS_RIGHT);
            rights++;
            pts += pc_guess_points(g.streak, gold);
        }
        CHECK_EQ_INT(pc_guess_answer(&g, 0), PC_GUESS_IGNORED);   /* answered once */
    }
    CHECK(g.over);
    CHECK_EQ_INT(g.correct, rights);
    CHECK_EQ_INT(g.points, pts);
    CHECK_EQ_INT(g.best_streak, 4);                            /* 4 right, 1 wrong, 3 right */
}

static void test_combo_and_gold(void)
{
    CHECK_EQ_INT(pc_guess_mult(0), 1);
    CHECK_EQ_INT(pc_guess_mult(1), 1);
    CHECK_EQ_INT(pc_guess_mult(2), 1);
    CHECK_EQ_INT(pc_guess_mult(3), 2);
    CHECK_EQ_INT(pc_guess_mult(4), 2);
    CHECK_EQ_INT(pc_guess_mult(5), 3);
    CHECK_EQ_INT(pc_guess_mult(7), 4);
    CHECK_EQ_INT(pc_guess_mult(200), 4);
    CHECK_EQ_INT(pc_guess_points(1, false), 100);
    CHECK_EQ_INT(pc_guess_points(3, false), 200);
    CHECK_EQ_INT(pc_guess_points(7, true), 800);
    int golden = 0, n = 0;                                     /* about one question in ten is golden */
    for (uint32_t s = 1; s <= 40; s++) {
        pc_guess_t g;
        pc_guess_init(&g, PC_GUESS_CLASSIC, 20, s);
        for (int q = 0; q < 20; q++) { CHECK(pc_guess_ask(&g, 8, title, NULL)); golden += g.golden; n++; pc_guess_answer(&g, 0); }
    }
    CHECK(golden > n / 20 && golden < n / 5);
}

static void test_rules_and_clip(void)
{
    CHECK_EQ_INT(pc_guess_clip_ms(PC_GUESS_CLASSIC), 5000);
    CHECK_EQ_INT(pc_guess_clip_ms(PC_GUESS_SHORT), 2000);
    CHECK_EQ_INT(pc_guess_clip_ms(PC_GUESS_ARTIST), 5000);
    pc_guess_t g;
    pc_guess_init(&g, 9, 0, 3);                                 /* out of range: classic, one question */
    CHECK_EQ_INT(g.rule, PC_GUESS_CLASSIC);
    CHECK_EQ_INT(g.rounds, 1);
    pc_guess_init(&g, PC_GUESS_SHORT, 500, 3);
    CHECK_EQ_INT(g.rounds, PC_GUESS_MAX_ROUNDS);
    CHECK(pc_guess_ask(&g, 8, title, NULL));
    CHECK_EQ_INT(g.clip_pct >= 10 && g.clip_pct < 60, 1);
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_BYTES, 1000, NULL, 30), 0);          /* a tiny file: its start */
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_BYTES, 4000000, NULL, 30), 1200000);
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_NONE, 4000000, NULL, 30), 0);         /* OGG / M4A / tracker files: from their start */
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_BYTES, 4000000, NULL, 150), 0);       /* a percentage out of range */
    pc_guess_t h;                                               /* the same seed asks the same questions; another seed another ones */
    pc_guess_init(&h, PC_GUESS_SHORT, 500, 3);
    CHECK(pc_guess_ask(&h, 8, title, NULL));
    CHECK_EQ_INT(h.target, g.target);
    CHECK_EQ_INT(h.answer, g.answer);
}

/* a FLAC token is a PCM frame index, never a byte: 10 minutes at 44.1 kHz = 26 460 000 frames in STREAMINFO */
static void flac_head(uint8_t h[PC_CLIP_FLAC_HEAD], uint64_t total)
{
    memset(h, 0, PC_CLIP_FLAC_HEAD);
    memcpy(h, "fLaC", 4);
    h[4] = 0x00; h[7] = 34;                                    /* STREAMINFO, 34 bytes */
    h[18] = 0x0A; h[19] = 0xC4; h[20] = 0x42;                  /* 44100 Hz, 2 channels, 16 bits (the sample count's top bits below) */
    h[21] = (uint8_t)(0xF0 | ((total >> 32) & 0x0F));
    h[22] = (uint8_t)(total >> 24); h[23] = (uint8_t)(total >> 16); h[24] = (uint8_t)(total >> 8); h[25] = (uint8_t)total;
}
static void test_flac_token(void)
{
    uint8_t h[PC_CLIP_FLAC_HEAD];
    flac_head(h, 26460000ull);
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_FLAC, 300000000u, h, 30), 7938000);       /* 30 % of the frames, not of the 300 MB */
    CHECK(pc_guess_clip_token(PC_CLIP_FLAC, 300000000u, h, 59) < 26460000u);           /* always inside the stream: the open cannot seek past the end */
    flac_head(h, 5ull << 32 | 1000);                                                    /* > 2^32 frames: not representable, from the start */
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_FLAC, 1u << 30, h, 30), 0);
    flac_head(h, 100000);                                                               /* 2 s of audio: from the start */
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_FLAC, 1u << 20, h, 30), 0);
    flac_head(h, 0);                                                                    /* unknown length */
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_FLAC, 1u << 20, h, 30), 0);
    flac_head(h, 26460000ull);
    h[0] = 'I';                                                                         /* not a plain fLaC header (an ID3 tag first) */
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_FLAC, 1u << 20, h, 30), 0);
    flac_head(h, 26460000ull);
    h[4] = 0x04;                                                                        /* the first block is not STREAMINFO */
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_FLAC, 1u << 20, h, 30), 0);
    CHECK_EQ_INT(pc_guess_clip_token(PC_CLIP_FLAC, 1u << 20, NULL, 30), 0);
}

static void test_skip(void)
{
    pc_guess_t g;
    pc_guess_init(&g, PC_GUESS_CLASSIC, 3, 11);
    CHECK(pc_guess_ask(&g, 8, title, NULL));
    int t = g.target;
    pc_guess_skip(&g);                                          /* its clip would not play: not counted, not open, not asked again at once */
    CHECK_EQ_INT(g.asked, 0);
    CHECK(!g.open);
    pc_guess_skip(&g);                                          /* nothing open: nothing happens */
    CHECK_EQ_INT(g.asked, 0);
    CHECK(pc_guess_ask(&g, 8, title, NULL));
    CHECK(g.target != t);
    CHECK_EQ_INT(g.asked, 1);
}

static void test_label(void)
{
    char o[PC_GUESS_LABEL];
    pc_guess_label(o, "Short");
    CHECK_STR(o, "Short");
    pc_guess_label(o, "123456789012345");                      /* exactly 15 bytes: as it is */
    CHECK_STR(o, "123456789012345");
    pc_guess_label(o, "1234567890123456");                      /* 16: cut at 12 and the ellipsis */
    CHECK_STR(o, "123456789012\xE2\x80\xA6");
    pc_guess_label(o, "\xE5\xA4\x9C\xE8\x88\xAA\xE6\x98\x9F\xE5\x9C\x96\xE4\xB8\x80\xE4\xBA\x8C");   /* 夜航星圖一二: 18 bytes: 4 characters (12 bytes) and the ellipsis */
    CHECK_STR(o, "\xE5\xA4\x9C\xE8\x88\xAA\xE6\x98\x9F\xE5\x9C\x96\xE2\x80\xA6");
    pc_guess_label(o, "12345678901\xE5\xA4\x9C" "xyz");           /* a character that would be cut in the middle goes whole */
    CHECK_STR(o, "12345678901\xE2\x80\xA6");
    pc_guess_label(o, NULL);
    CHECK_STR(o, "");
}

int main(void)
{
    RUN(test_ready);
    RUN(test_ask);
    RUN(test_ask_refuses);
    RUN(test_answer_score);
    RUN(test_combo_and_gold);
    RUN(test_rules_and_clip);
    RUN(test_flac_token);
    RUN(test_skip);
    RUN(test_label);
    return 0;
}
