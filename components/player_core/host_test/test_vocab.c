// SPDX-License-Identifier: LGPL-2.1-or-later
#include <errno.h>
#include "pc/pc_crc32.h"
#include "pc/pc_glyphpack.h"
#include "pc/pc_io.h"
#include "pc/pc_vocab.h"
#include "pc_test.h"

/* fixtures/make_glyphpacks.py vocab(): 12 cards
 *   0 N b1 [1]->[2]   1 N b1   2 N b2   3 V b1   4 V b1   5 ADJ b1   6 N b1 meaning [2] like card 0
 *   7 N b9   8 weight 0 (left out)   9 no meaning (left out)   10 PREFIX b1 + extra [11]   11 ROOT b1 + extra
 * vocab_big(): 2000 cards, class 1 + i % 4, band 1 + i % 20, all meanings different */
typedef struct { uint8_t *zip; pc_reader_t r; pc_gp_t *gp; } deck_t;
static void deck_open(deck_t *d, const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", FIXTURES, name);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d->zip = malloc((size_t)n);
    CHECK(fread(d->zip, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    pc_reader_mem(&d->r, d->zip, (uint32_t)n);
    CHECK_EQ_INT(pc_gp_open(&d->r, NULL, &d->gp), 0);
    CHECK_EQ_INT(pc_gp_kind(d->gp), PC_GP_KIND_VOCAB);
}
static void deck_close(deck_t *d) { pc_gp_close(d->gp); free(d->zip); }

static void test_deck(void)
{
    deck_t d;
    deck_open(&d, "gp_vocab.zip");
    CHECK_EQ_INT(pc_vocab_count(d.gp), 12);
    pc_vocab_entry_t e;
    CHECK_EQ_INT(pc_vocab_entry(d.gp, 2, &e), 0);
    CHECK_EQ_INT(e.word_len, 2); CHECK_EQ_INT(e.word[0], 1); CHECK_EQ_INT(e.word[1], 2);
    CHECK_EQ_INT(e.meaning_len, 1); CHECK_EQ_INT(e.meaning[0], 4);
    CHECK(e.extra == NULL); CHECK_EQ_INT(e.extra_len, 0);
    CHECK_EQ_INT(e.cls, PC_VC_NOUN); CHECK_EQ_INT(e.band, 2);
    CHECK_EQ_INT(pc_vocab_entry(d.gp, 11, &e), 0);
    CHECK_EQ_INT(e.cls, PC_VC_ROOT); CHECK_EQ_INT(e.extra_len, 2); CHECK_EQ_INT(e.extra[0], 11);
    CHECK_EQ_INT(e.meaning_len, 2);
    CHECK_EQ_INT(pc_vocab_entry(d.gp, 9, &e), -EINVAL);       /* no meaning */
    CHECK_EQ_INT(pc_vocab_entry(d.gp, 12, &e), -ERANGE);
    uint8_t skip[2] = { 0xFF, 0xFF };
    CHECK_EQ_INT(pc_vocab_skipbits(d.gp, skip, sizeof skip), 10);
    CHECK_EQ_INT(skip[0], 0x00); CHECK_EQ_INT(skip[1], 0x03);  /* 8 and 9 */
    CHECK_EQ_INT(pc_vocab_skipbits(d.gp, skip, 1), 0);         /* too small */
    uint32_t h = pc_vocab_hash(d.gp);
    CHECK(h != 0);
    deck_t b;
    deck_open(&b, "gp_vocab_big.zip");
    CHECK_EQ_INT(pc_vocab_count(b.gp), 2000);
    CHECK(pc_vocab_hash(b.gp) != h);
    CHECK_EQ_INT(pc_vocab_hash(d.gp), h);                       /* stable */
    deck_close(&b);
    deck_close(&d);
    /* another kind is no deck */
    deck_t l;
    char p[256];
    snprintf(p, sizeof p, "%s/gp_picks.zip", FIXTURES);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    l.zip = malloc((size_t)n);
    CHECK(fread(l.zip, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    pc_reader_mem(&l.r, l.zip, (uint32_t)n);
    CHECK_EQ_INT(pc_gp_open(&l.r, NULL, &l.gp), 0);
    CHECK_EQ_INT(pc_vocab_count(l.gp), 0);
    CHECK_EQ_INT(pc_vocab_entry(l.gp, 0, &e), -ERANGE);
    deck_close(&l);
}

static bool same_text(const pc_gp_t *gp, uint32_t a, uint32_t b, int field)
{
    pc_vocab_entry_t x, y;
    CHECK_EQ_INT(pc_vocab_entry(gp, a, &x), 0);
    CHECK_EQ_INT(pc_vocab_entry(gp, b, &y), 0);
    const uint16_t *s = field ? x.meaning : x.word, *t = field ? y.meaning : y.word;
    uint16_t m = field ? x.meaning_len : x.word_len, n = field ? y.meaning_len : y.word_len;
    return m == n && !memcmp(s, t, 2u * m);
}

static void test_distractors(void)
{
    deck_t d;
    deck_open(&d, "gp_vocab.zip");
    uint8_t skip[2];
    pc_vocab_skipbits(d.gp, skip, sizeof skip);
    pc_rng_t r;
    pc_rng_seed(&r, 5);
    int hits[12] = { 0 };
    for (int it = 0; it < 3000; it++) {
        uint32_t out[3];
        int n = pc_vocab_distractors(&r, d.gp, skip, 0, 1, out, 3);
        CHECK_EQ_INT(n, 3);
        for (int i = 0; i < n; i++) {
            CHECK(out[i] != 0 && out[i] < 12);
            CHECK(out[i] != 8 && out[i] != 9);                 /* left out */
            CHECK(out[i] != 6);                                 /* the same meaning as the answer */
            CHECK(!same_text(d.gp, out[i], 0, 1));
            for (int j = 0; j < i; j++) { CHECK(out[i] != out[j]); CHECK(!same_text(d.gp, out[i], out[j], 1)); }
            hits[out[i]]++;
        }
    }
    /* card 0 is a band-1 noun: nouns 1 (b1) and 2 (b2) every time, then the nearest other: verbs / adj (b1) */
    CHECK_EQ_INT(hits[1], 3000);
    CHECK_EQ_INT(hits[2], 3000);
    CHECK(hits[3] + hits[4] + hits[5] + hits[7] == 3000);
    CHECK(hits[3] > 500 && hits[4] > 500 && hits[5] > 500);   /* ties are random */
    CHECK_EQ_INT(hits[10] + hits[11], 0);                      /* word parts only as a last resort */
    /* the word side (ZH -> EN): card 6 has another word but the same meaning as card 0, so it would be a second
     * right answer to the prompt: never offered */
    for (int it = 0; it < 500; it++) {
        uint32_t out[3];
        CHECK_EQ_INT(pc_vocab_distractors(&r, d.gp, skip, 0, 0, out, 3), 3);
        for (int i = 0; i < 3; i++) { CHECK(out[i] != 0); CHECK(out[i] != 6); }
    }
    /* a root: the prefix first (word part), then the others */
    for (int it = 0; it < 200; it++) {
        uint32_t out[3];
        CHECK_EQ_INT(pc_vocab_distractors(&r, d.gp, skip, 11, 1, out, 3), 3);
        CHECK_EQ_INT(out[0] == 10 || out[1] == 10 || out[2] == 10, 1);
    }
    /* fewer wanted, none wanted, a tiny usable set */
    uint32_t o[3];
    CHECK_EQ_INT(pc_vocab_distractors(&r, d.gp, skip, 0, 1, o, 1), 1);
    CHECK_EQ_INT(o[0], 1);                                     /* the best: the band-1 noun (6 has the same meaning) */
    CHECK_EQ_INT(pc_vocab_distractors(&r, d.gp, skip, 0, 1, o, 0), 0);
    uint8_t most[2] = { 0xFC, 0x0F };                          /* only 0 and 1 usable */
    CHECK_EQ_INT(pc_vocab_distractors(&r, d.gp, most, 0, 1, o, 3), 1);
    CHECK_EQ_INT(o[0], 1);
    CHECK_EQ_INT(pc_vocab_distractors(&r, d.gp, skip, 12, 1, o, 3), 0);   /* bad card */
    deck_close(&d);

    /* big deck: sampled; same class and a near band nearly always */
    deck_t b;
    deck_open(&b, "gp_vocab_big.zip");
    uint8_t bs[pc_vocab_bitbytes(2000)];
    CHECK_EQ_INT(pc_vocab_skipbits(b.gp, bs, sizeof bs), 2000);
    int good = 0, total = 0;
    for (int it = 0; it < 300; it++) {
        uint32_t card = pc_rng_below(&r, 2000), out[3];
        pc_vocab_entry_t c;
        pc_vocab_entry(b.gp, card, &c);
        CHECK_EQ_INT(pc_vocab_distractors(&r, b.gp, bs, card, 1, out, 3), 3);
        for (int i = 0; i < 3; i++) {
            pc_vocab_entry_t e;
            CHECK(out[i] != card && out[i] < 2000);
            for (int j = 0; j < i; j++) CHECK(out[i] != out[j]);
            pc_vocab_entry(b.gp, out[i], &e);
            total++;
            good += e.cls == c.cls && abs((int)e.band - (int)c.band) <= 1;
        }
    }
    CHECK(good > total * 9 / 10);
    deck_close(&b);
}

static void test_file(void)
{
    pc_vocab_card_t cards[5], c2[5];
    pc_vocab_prog_t p = { .cards = cards }, q = { .cards = c2 };
    pc_vocab_reset(&p, 0x1234, 5);
    CHECK_EQ_INT(p.ncards, 5); CHECK_EQ_INT(p.deck_hash, 0x1234); CHECK_EQ_INT(cards[3].box, 0);
    cards[1] = (pc_vocab_card_t){ 3, 2, 9001 };
    cards[4] = (pc_vocab_card_t){ 7, 0, 65535 };
    p.last_day = 9000; p.streak = 4; p.streak_day = 9000; p.new_day = 9000; p.new_count = 7;
    p.stat_day = 9000; p.stat_n = 20; p.stat_ok = 15; p.reviews = 123456; p.correct = 100000;
    size_t n = pc_vocab_file_bytes(5);
    CHECK_EQ_INT(n, PC_VOCAB_FILE_HDR + 20);
    uint8_t buf[200];
    CHECK_EQ_INT(pc_vocab_save(&p, buf, n - 1), 0);
    CHECK_EQ_INT(pc_vocab_save(&p, buf, sizeof buf), n);
    CHECK(!memcmp(buf, "JVPR", 4));
    CHECK_EQ_INT(pc_vocab_load(&q, buf, n, 0x1234, 5), 0);
    CHECK_EQ_INT(q.last_day, 9000); CHECK_EQ_INT(q.streak, 4); CHECK_EQ_INT(q.new_count, 7);
    CHECK_EQ_INT(q.stat_ok, 15); CHECK_EQ_INT(q.reviews, 123456); CHECK_EQ_INT(q.correct, 100000);
    CHECK(!memcmp(c2, cards, sizeof cards));
    /* every damage resets */
    for (size_t i = 0; i < n; i++) {
        uint8_t bad[200];
        memcpy(bad, buf, n);
        bad[i] ^= 0x40;
        memset(c2, 0x55, sizeof c2);
        int rc = pc_vocab_load(&q, bad, n, 0x1234, 5);
        CHECK(rc < 0);
        CHECK_EQ_INT(q.ncards, 5); CHECK_EQ_INT(q.reviews, 0); CHECK_EQ_INT(q.deck_hash, 0x1234);
        for (int k = 0; k < 5; k++) { CHECK_EQ_INT(c2[k].box, 0); CHECK_EQ_INT(c2[k].due, 0); }
    }
    for (size_t cut = 0; cut < n; cut++) CHECK_EQ_INT(pc_vocab_load(&q, buf, cut, 0x1234, 5), -EINVAL);
    CHECK_EQ_INT(pc_vocab_load(&q, NULL, 0, 0x1234, 5), -EINVAL);
    CHECK_EQ_INT(pc_vocab_load(&q, buf, n, 0x1235, 5), -ESTALE);          /* another deck */
    CHECK_EQ_INT(q.deck_hash, 0x1235);
    q.cards = c2;
    pc_vocab_card_t c3[6];
    pc_vocab_prog_t r = { .cards = c3 };
    CHECK_EQ_INT(pc_vocab_load(&r, buf, n, 0x1234, 6), -ESTALE);          /* the deck has another card count */
    /* version 2 -> not supported (fixed CRC) */
    uint8_t v2[200];
    memcpy(v2, buf, n);
    v2[4] = 2;
    uint32_t crc = pc_crc32(0, v2, 44);
    crc = pc_crc32(crc, v2 + 48, (uint32_t)n - 48);
    v2[44] = (uint8_t)crc; v2[45] = (uint8_t)(crc >> 8); v2[46] = (uint8_t)(crc >> 16); v2[47] = (uint8_t)(crc >> 24);
    CHECK_EQ_INT(pc_vocab_load(&q, v2, n, 0x1234, 5), -EPROTONOSUPPORT);
    /* a box over 7 with a right CRC */
    memcpy(v2, buf, n);
    v2[48 + 4] = 9;
    crc = pc_crc32(0, v2, 44);
    crc = pc_crc32(crc, v2 + 48, (uint32_t)n - 48);
    v2[44] = (uint8_t)crc; v2[45] = (uint8_t)(crc >> 8); v2[46] = (uint8_t)(crc >> 16); v2[47] = (uint8_t)(crc >> 24);
    CHECK_EQ_INT(pc_vocab_load(&q, v2, n, 0x1234, 5), -EINVAL);
    /* empty deck: header only */
    pc_vocab_prog_t e = { .cards = NULL };
    pc_vocab_reset(&e, 7, 0);
    CHECK_EQ_INT(pc_vocab_save(&e, buf, sizeof buf), PC_VOCAB_FILE_HDR);
    CHECK_EQ_INT(pc_vocab_load(&e, buf, PC_VOCAB_FILE_HDR, 7, 0), 0);
}

static void test_days(void)
{
    CHECK_EQ_INT(pc_vocab_day(946684800), 1);                 /* 2000-01-01 00:00 -> clamped to day 1 */
    CHECK_EQ_INT(pc_vocab_day(946684800 + 86400 * 10 + 5), 10);
    CHECK_EQ_INT(pc_vocab_day(0), 1);
    CHECK_EQ_INT(pc_vocab_day(-5), 1);
    CHECK_EQ_INT(pc_vocab_day(1793836800), 9805);              /* 2026-11-05 */
    CHECK_EQ_INT(pc_vocab_interval(1), 0);
    CHECK_EQ_INT(pc_vocab_interval(2), 1);
    CHECK_EQ_INT(pc_vocab_interval(3), 2);
    CHECK_EQ_INT(pc_vocab_interval(7), 32);
    CHECK_EQ_INT(pc_vocab_interval(8), 64);
    CHECK_EQ_INT(pc_vocab_interval(9), 64);                    /* past the top box */
    CHECK_EQ_INT(pc_vocab_interval(0), 0);
}

static void test_schedule(void)
{
    enum { N = 30 };
    pc_vocab_card_t cards[N];
    pc_vocab_prog_t p = { .cards = cards };
    pc_vocab_reset(&p, 1, N);
    uint8_t skip[pc_vocab_bitbytes(N)];
    memset(skip, 0, sizeof skip);
    skip[0] = 0x04;                                            /* card 2 left out */
    pc_vocab_session_t s;
    pc_vocab_session_init(&s, 5);
    pc_rng_t r;
    pc_rng_seed(&r, 9);
    uint16_t day = 9000;
    int why;
    /* new cards in deck order (2 skipped), five a day */
    int order[5];
    for (int i = 0; i < 5; i++) {
        int32_t c = pc_vocab_next(&s, &p, skip, day, true, &r, &why);
        CHECK_EQ_INT(why, PC_VN_NEW);
        order[i] = c;
        pc_vocab_answer(&s, &p, (uint32_t)c, why, true, day, true);
    }
    CHECK_EQ_INT(order[0], 0); CHECK_EQ_INT(order[1], 1); CHECK_EQ_INT(order[2], 3); CHECK_EQ_INT(order[4], 5);
    CHECK_EQ_INT(cards[0].box, 2); CHECK_EQ_INT(cards[0].due, day + 1);
    CHECK_EQ_INT(p.new_day, day); CHECK_EQ_INT(p.new_count, 5);
    CHECK_EQ_INT(p.streak, 1); CHECK_EQ_INT(p.streak_day, day); CHECK_EQ_INT(p.last_day, day);
    CHECK_EQ_INT(p.stat_n, 5); CHECK_EQ_INT(p.stat_ok, 5); CHECK_EQ_INT(s.run, 5);
    /* limit reached, nothing due: practice, never one of the last four */
    uint32_t last[8];
    int nl = 0;
    for (int i = 0; i < 40; i++) {
        int32_t c = pc_vocab_next(&s, &p, skip, day, true, &r, &why);
        CHECK_EQ_INT(why, PC_VN_PRACTICE);
        CHECK(c == 0 || c == 1 || c == 3 || c == 4 || c == 5);
        for (int k = 0; k < nl && k < 4; k++) CHECK(last[(nl - 1 - k) % 8] != (uint32_t)c);
        last[nl++ % 8] = (uint32_t)c;
        uint16_t due = cards[c].due;
        pc_vocab_answer(&s, &p, (uint32_t)c, why, true, day, true);
        CHECK_EQ_INT(cards[c].box, 2); CHECK_EQ_INT(cards[c].due, due);   /* practice: no change when right */
        if (nl == 8) break;
    }
    /* the next day: due cards first, most overdue first; a wrong one comes back after 3 others */
    day++;
    cards[4].due = day - 3;                                    /* overdue the most */
    int32_t c = pc_vocab_next(&s, &p, skip, day, true, &r, &why);
    CHECK_EQ_INT(why, PC_VN_DUE);
    CHECK_EQ_INT(c, 4);
    pc_vocab_answer(&s, &p, 4, why, false, day, true);
    CHECK_EQ_INT(cards[4].box, 1); CHECK_EQ_INT(cards[4].due, day); CHECK_EQ_INT(cards[4].lapses, 1);
    CHECK_EQ_INT(s.run, 0); CHECK_EQ_INT(p.streak, 2);
    int seen4 = -1;
    for (int i = 0; i < 6; i++) {
        c = pc_vocab_next(&s, &p, skip, day, true, &r, &why);
        if (c == 4) { CHECK_EQ_INT(why, PC_VN_RELEARN); seen4 = i; pc_vocab_answer(&s, &p, 4, why, true, day, true); break; }
        CHECK(why == PC_VN_DUE);
        pc_vocab_answer(&s, &p, (uint32_t)c, why, true, day, true);
        CHECK_EQ_INT(cards[c].box, 3); CHECK_EQ_INT(cards[c].due, day + 2);
    }
    CHECK_EQ_INT(seen4, 3);
    CHECK_EQ_INT(cards[4].box, 2); CHECK_EQ_INT(cards[4].due, day + 1);
    CHECK_EQ_INT(s.nrelearn, 0);
    c = pc_vocab_next(&s, &p, skip, day, true, &r, &why);        /* the fourth due card */
    CHECK_EQ_INT(why, PC_VN_DUE);
    CHECK(c != 4);
    pc_vocab_answer(&s, &p, (uint32_t)c, why, true, day, true);
    /* then new cards again (a new day, a new count) */
    c = pc_vocab_next(&s, &p, skip, day, true, &r, &why);
    CHECK_EQ_INT(why, PC_VN_NEW); CHECK_EQ_INT(c, 6);
    pc_vocab_answer(&s, &p, 6, why, false, day, true);            /* a new card missed: box 1, no lapse */
    CHECK_EQ_INT(cards[6].box, 1); CHECK_EQ_INT(cards[6].lapses, 0); CHECK_EQ_INT(p.new_count, 1);
    /* stats */
    pc_vocab_stats_t st;
    pc_vocab_stats(&p, skip, day, &st);
    CHECK_EQ_INT(st.total, N - 1);
    CHECK_EQ_INT(st.seen, 6);
    CHECK_EQ_INT(st.fresh, N - 1 - 6);
    CHECK_EQ_INT(st.due, 1);                                       /* card 6, due today */
    CHECK_EQ_INT(st.learned, 4);                                   /* 0 1 3 5 in box 3 */
    CHECK_EQ_INT(st.streak, 2);
    pc_vocab_stats(&p, skip, day + 1, &st);
    CHECK_EQ_INT(st.streak, 2);                                    /* yesterday still counts */
    CHECK_EQ_INT(st.today_n, 0);
    pc_vocab_stats(&p, skip, day + 2, &st);
    CHECK_EQ_INT(st.streak, 0);
    /* a gap breaks the streak */
    pc_vocab_answer(&s, &p, 6, PC_VN_DUE, true, day + 3, true);
    CHECK_EQ_INT(p.streak, 1);
    /* no valid clock: no streak, no last_day; the new limit counts the session */
    pc_vocab_session_t s2;
    pc_vocab_session_init(&s2, 2);
    uint16_t ld = p.last_day;
    int news = 0;
    for (int i = 0; i < 6; i++) {
        c = pc_vocab_next(&s2, &p, skip, 50, false, &r, &why);
        CHECK(c >= 0);
        news += why == PC_VN_NEW;
        pc_vocab_answer(&s2, &p, (uint32_t)c, why, true, 50, false);
    }
    CHECK_EQ_INT(news, 2);
    CHECK_EQ_INT(p.last_day, ld);
    CHECK_EQ_INT(p.streak, 1);
    /* the top box is 8 */
    for (int i = 0; i < 12; i++) pc_vocab_answer(&s, &p, 0, PC_VN_DUE, true, day, true);
    CHECK_EQ_INT(cards[0].box, PC_VOCAB_BOX_MAX); CHECK_EQ_INT(cards[0].due, day + 64);
    /* relearn holds at most PC_VOCAB_RELEARN; missed cards beyond are still box 1 and due */
    pc_vocab_session_t s3;
    pc_vocab_session_init(&s3, 100);
    for (uint32_t k = 10; k < 25; k++) pc_vocab_answer(&s3, &p, k, PC_VN_NEW, false, day, true);
    CHECK_EQ_INT(s3.nrelearn, PC_VOCAB_RELEARN);
    CHECK_EQ_INT(cards[24].box, 1);
}

static void test_schedule_edges(void)
{
    pc_vocab_card_t cards[3];
    pc_vocab_prog_t p = { .cards = cards };
    pc_vocab_reset(&p, 1, 3);
    uint8_t skip[1] = { 0x07 };
    pc_vocab_session_t s;
    pc_vocab_session_init(&s, 10);
    pc_rng_t r;
    pc_rng_seed(&r, 1);
    int why = 99;
    CHECK_EQ_INT(pc_vocab_next(&s, &p, skip, 10, true, &r, &why), -1);   /* nothing usable */
    CHECK_EQ_INT(why, PC_VN_NONE);
    /* one usable card: asked again and again although it is recent */
    skip[0] = 0x06;
    for (int i = 0; i < 5; i++) {
        int32_t c = pc_vocab_next(&s, &p, skip, 10, true, &r, &why);
        CHECK_EQ_INT(c, 0);
        pc_vocab_answer(&s, &p, 0, why, true, 10, true);
    }
    /* two cards, new limit 0: the first one is taught anyway (nothing seen yet) */
    pc_vocab_reset(&p, 1, 3);
    pc_vocab_session_init(&s, 0);
    skip[0] = 0x04;
    int32_t c = pc_vocab_next(&s, &p, skip, 10, true, &r, &why);
    CHECK_EQ_INT(c, 0); CHECK_EQ_INT(why, PC_VN_NEW);
    pc_vocab_answer(&s, &p, 0, why, true, 10, true);
    c = pc_vocab_next(&s, &p, skip, 10, true, &r, &why);
    CHECK_EQ_INT(c, 0); CHECK_EQ_INT(why, PC_VN_PRACTICE);        /* limit 0: practice the one known card */
    /* out-of-range card in answer is ignored */
    pc_vocab_answer(&s, &p, 3, PC_VN_NEW, true, 10, true);
    pc_vocab_answer(&s, &p, 99999, PC_VN_NEW, false, 10, true);
    CHECK_EQ_INT(s.nrelearn, 0);
    /* the day going backwards (clock set back) still works: due in the "future" is just not due */
    cards[0].due = 20;
    pc_vocab_stats_t st;
    pc_vocab_stats(&p, skip, 10, &st);
    CHECK_EQ_INT(st.due, 0);
}

static void test_stats_unloaded(void)
{
    /* a closed deck leaves ncards behind with the card array gone: stats must read as empty, not crash */
    pc_vocab_prog_t p;
    memset(&p, 0, sizeof p);
    p.ncards = 5; p.cards = NULL;
    pc_vocab_stats_t st;
    memset(&st, 0xAB, sizeof st);
    pc_vocab_stats(&p, NULL, 10, &st);
    CHECK_EQ_INT(st.total, 0); CHECK_EQ_INT(st.fresh, 0); CHECK_EQ_INT(st.due, 0);
}

int main(void)
{
    RUN(test_deck);
    RUN(test_distractors);
    RUN(test_file);
    RUN(test_stats_unloaded);
    RUN(test_days);
    RUN(test_schedule);
    RUN(test_schedule_edges);
    return 0;
}
