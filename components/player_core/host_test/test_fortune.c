// SPDX-License-Identifier: LGPL-2.1-or-later
#include <unistd.h>
#include "pc/pc_fortune.h"
#include "pc_test.h"

static void test_rng_below_range(void)
{
    pc_rng_t r;
    pc_rng_seed(&r, 1);
    for (int i = 0; i < 10000; i++) CHECK(pc_rng_below(&r, 7) < 7);
    CHECK_EQ_INT(pc_rng_below(&r, 0), 0);
    CHECK_EQ_INT(pc_rng_below(&r, 1), 0);
    pc_rng_t a, b;
    pc_rng_seed(&a, 42);
    pc_rng_seed(&b, 42);
    for (int i = 0; i < 100; i++) CHECK_EQ_INT(pc_rng_next(&a), pc_rng_next(&b));
}

static void test_day_seed_stable(void)
{
    CHECK(pc_fortune_day_seed(2026, 11, 10, PC_SALT_OMIKUJI) == pc_fortune_day_seed(2026, 11, 10, PC_SALT_OMIKUJI));
    CHECK(pc_fortune_day_seed(2026, 11, 10, PC_SALT_OMIKUJI) != pc_fortune_day_seed(2026, 11, 11, PC_SALT_OMIKUJI));
    CHECK(pc_fortune_day_seed(2026, 11, 10, PC_SALT_OMIKUJI) != pc_fortune_day_seed(2026, 11, 10, PC_SALT_TAROT));
    CHECK(pc_fortune_day_seed(2026, 1, 11, 0) != pc_fortune_day_seed(2026, 11, 1, 0));
    /* the whole draw of a day repeats */
    int8_t vr[6] = { -1, -1, -1, -1, -1, -1 };
    pc_omikuji_t o1, o2;
    pc_rng_t r;
    pc_rng_seed(&r, pc_fortune_day_seed(2026, 11, 10, PC_SALT_OMIKUJI));
    pc_omikuji_draw(&r, vr, 6, &o1);
    pc_rng_seed(&r, pc_fortune_day_seed(2026, 11, 10, PC_SALT_OMIKUJI));
    pc_omikuji_draw(&r, vr, 6, &o2);
    CHECK(!memcmp(&o1, &o2, sizeof o1));
}

static void test_omikuji_distribution(void)
{
    int sum = 0;
    for (int k = 0; k < PC_KUJI_COUNT; k++) sum += pc_kuji_weight[k];
    CHECK_EQ_INT(sum, 100);
    CHECK_EQ_INT(pc_kuji_weight[PC_KUJI_KYO], 10);
    int count[PC_KUJI_COUNT] = { 0 }, days = 0;
    int8_t vr[1] = { -1 };
    for (int y = 2000; y < 2060; y++)                   /* ~22000 day seeds */
        for (int m = 1; m <= 12; m++)
            for (int d = 1; d <= 31; d++, days++) {
                pc_rng_t r;
                pc_rng_seed(&r, pc_fortune_day_seed(y, m, d, PC_SALT_OMIKUJI));
                pc_omikuji_t o;
                pc_omikuji_draw(&r, vr, 1, &o);
                CHECK(o.rank >= 0 && o.rank < PC_KUJI_COUNT);
                count[o.rank]++;
                for (int a = 0; a < PC_KUJI_ASPECTS; a++) CHECK(o.stars[a] >= 1 && o.stars[a] <= 5);
            }
    for (int k = 0; k < PC_KUJI_COUNT; k++) CHECK_NEAR((double)count[k] / days, pc_kuji_weight[k] / 100.0, 0.012);
    CHECK_NEAR((double)count[PC_KUJI_KYO] / days, 0.10, 0.01);
}

static void test_omikuji_stars_follow_rank(void)
{
    double mean[PC_KUJI_COUNT] = { 0 };
    int n[PC_KUJI_COUNT] = { 0 };
    pc_rng_t r;
    pc_rng_seed(&r, 7);
    int8_t vr[1] = { -1 };
    for (int i = 0; i < 20000; i++) {
        pc_omikuji_t o;
        pc_omikuji_draw(&r, vr, 1, &o);
        for (int a = 0; a < PC_KUJI_ASPECTS; a++) mean[o.rank] += o.stars[a];
        n[o.rank] += PC_KUJI_ASPECTS;
    }
    for (int k = 0; k < PC_KUJI_COUNT; k++) mean[k] /= n[k];
    CHECK(mean[PC_KUJI_DAIKICHI] > mean[PC_KUJI_SHOKICHI]);
    CHECK(mean[PC_KUJI_SHOKICHI] > mean[PC_KUJI_KYO]);
}

static void test_omikuji_verse_matches_rank(void)
{
    /* verses 0,1 大吉 (great luck); 2 凶 (ill luck); 3 any */
    int8_t vr[4] = { PC_KUJI_DAIKICHI, PC_KUJI_DAIKICHI, PC_KUJI_KYO, -1 };
    pc_rng_t r;
    pc_rng_seed(&r, 99);
    int seen[4] = { 0 };
    for (int i = 0; i < 5000; i++) {
        pc_omikuji_t o;
        pc_omikuji_draw(&r, vr, 4, &o);
        CHECK(o.verse >= 0 && o.verse < 4);
        seen[o.verse]++;
        if (o.rank == PC_KUJI_DAIKICHI) CHECK(o.verse == 0 || o.verse == 1 || o.verse == 3);
        else if (o.rank == PC_KUJI_KYO) CHECK(o.verse == 2 || o.verse == 3);
        else CHECK_EQ_INT(o.verse, 3);
    }
    for (int i = 0; i < 4; i++) CHECK(seen[i] > 0);
    /* nothing written for the rank: any verse */
    int8_t only_kyo[2] = { PC_KUJI_KYO, PC_KUJI_KYO };
    for (int i = 0; i < 200; i++) {
        pc_omikuji_t o;
        pc_omikuji_draw(&r, only_kyo, 2, &o);
        CHECK(o.verse == 0 || o.verse == 1);
    }
    pc_omikuji_t o;
    pc_omikuji_draw(&r, NULL, 0, &o);
    CHECK_EQ_INT(o.verse, -1);
}

static void test_kuji_parse_rank(void)
{
    CHECK_EQ_INT(pc_kuji_parse_rank("大吉"), PC_KUJI_DAIKICHI);
    CHECK_EQ_INT(pc_kuji_parse_rank("吉"), PC_KUJI_KICHI);
    CHECK_EQ_INT(pc_kuji_parse_rank("中吉"), PC_KUJI_CHUKICHI);
    CHECK_EQ_INT(pc_kuji_parse_rank("小吉"), PC_KUJI_SHOKICHI);
    CHECK_EQ_INT(pc_kuji_parse_rank("末吉"), PC_KUJI_SUEKICHI);
    CHECK_EQ_INT(pc_kuji_parse_rank("凶"), PC_KUJI_KYO);
    CHECK_EQ_INT(pc_kuji_parse_rank("*"), -1);
    CHECK_EQ_INT(pc_kuji_parse_rank(""), -1);
    CHECK_EQ_INT(pc_kuji_parse_rank("大凶"), -2);
}

static void test_fortune_lines(void)
{
    char text[] = "\xEF\xBB\xBF# wheel.txt\n\n  Pizza  \r\n#comment\nRamen\n\t\nSushi\nTacos";
    char *out[8];
    int n = pc_fortune_lines(text, out, 8, 64);
    CHECK_EQ_INT(n, 4);
    CHECK_STR(out[0], "Pizza");
    CHECK_STR(out[1], "Ramen");
    CHECK_STR(out[2], "Sushi");
    CHECK_STR(out[3], "Tacos");
    /* the cap on the count */
    char many[] = "a\nb\nc\nd\ne\n";
    CHECK_EQ_INT(pc_fortune_lines(many, out, 3, 64), 3);
    CHECK_STR(out[2], "c");
    /* long items cut at a character boundary: 中 is 3 bytes */
    char cjk[] = "中中中中\nok\n";
    CHECK_EQ_INT(pc_fortune_lines(cjk, out, 8, 7), 2);
    CHECK_STR(out[0], "中中");
    CHECK_STR(out[1], "ok");
    char empty[] = "# only\n\n";
    CHECK_EQ_INT(pc_fortune_lines(empty, out, 8, 64), 0);
}

static void test_jiao_probabilities(void)
{
    pc_rng_t r;
    pc_rng_seed(&r, 2026);
    int c[3] = { 0 }, N = 100000;
    for (int i = 0; i < N; i++) {
        pc_jiao_throw_t t;
        pc_jiao_throw(&r, &t);
        c[t.result]++;
        pc_jiao_t want = t.flat_up[0] != t.flat_up[1] ? PC_JIAO_SHENG : t.flat_up[0] ? PC_JIAO_XIAO : PC_JIAO_YIN;
        CHECK_EQ_INT(t.result, want);
    }
    CHECK_NEAR((double)c[PC_JIAO_SHENG] / N, 0.50, 0.01);
    CHECK_NEAR((double)c[PC_JIAO_XIAO] / N, 0.25, 0.01);
    CHECK_NEAR((double)c[PC_JIAO_YIN] / N, 0.25, 0.01);
    CHECK_EQ_INT(pc_jiao_streak(2, PC_JIAO_SHENG), 3);
    CHECK_EQ_INT(pc_jiao_streak(2, PC_JIAO_XIAO), 0);
}

static void test_dice_range(void)
{
    pc_rng_t r;
    pc_rng_seed(&r, 5);
    int c[7] = { 0 };
    for (int i = 0; i < 60000; i++) {
        int d = pc_dice_roll(&r);
        CHECK(d >= 1 && d <= 6);
        c[d]++;
    }
    for (int f = 1; f <= 6; f++) CHECK_NEAR(c[f] / 60000.0, 1 / 6.0, 0.01);
}

static void test_wheel_index(void)
{
    CHECK_EQ_INT(pc_wheel_index(0, 4), 0);
    CHECK_EQ_INT(pc_wheel_index(89.9, 4), 0);
    CHECK_EQ_INT(pc_wheel_index(90, 4), 1);
    CHECK_EQ_INT(pc_wheel_index(359.9, 4), 3);
    CHECK_EQ_INT(pc_wheel_index(360 * 3 + 100, 4), 1);
    CHECK_EQ_INT(pc_wheel_index(-10, 4), 3);
    CHECK_EQ_INT(pc_wheel_index(123, 1), 0);
    CHECK_EQ_INT(pc_wheel_index(123, 0), 0);
}

static void test_wheel_spin_stops_on_target(void)
{
    pc_rng_t r;
    pc_rng_seed(&r, 11);
    int n = 7, hits[7] = { 0 };
    double start = 33;
    for (int i = 0; i < 7000; i++) {
        pc_wheel_spin_t s;
        pc_wheel_plan(&r, n, start, &s);
        CHECK(s.target >= 0 && s.target < n);
        hits[s.target]++;
        CHECK(s.total >= 3 * 360.0 && s.total <= 6 * 360.0);
        CHECK_NEAR(pc_wheel_angle_at(&s, 0), start, 1e-9);
        double end = pc_wheel_angle_at(&s, 1);
        CHECK_NEAR(end, start + s.total, 1e-9);
        CHECK_EQ_INT(pc_wheel_index(end, n), s.target);
        double seg = 360.0 / n, off = fmod(end, seg);   /* away from the edges */
        CHECK(off > 0.15 * seg - 1e-9 && off < 0.85 * seg + 1e-9);
        double prev = start;                             /* only ever forward, slowing down */
        for (int k = 1; k <= 20; k++) { double a = pc_wheel_angle_at(&s, k / 20.0); CHECK(a >= prev); prev = a; }
        CHECK(pc_wheel_angle_at(&s, 0.1) - start > pc_wheel_angle_at(&s, 1.0) - pc_wheel_angle_at(&s, 0.9));
        start = fmod(end, 360);
    }
    for (int i = 0; i < n; i++) CHECK_NEAR(hits[i] / 7000.0, 1.0 / n, 0.025);
}

static void test_gacha_tiers(void)
{
    /* 8 tracks: two never played, the rest by plays */
    uint32_t plays[8] = { 0, 50, 3, 0, 10, 1, 7, 2 }, scratch[8];
    uint8_t tier[8];
    pc_gacha_tiers(plays, 8, scratch, tier);
    /* sorted: 0 0 1 2 3 7 10 50; position <2 SSR, <4 SR, <6 R, else N */
    CHECK_EQ_INT(tier[0], PC_RAR_SSR);
    CHECK_EQ_INT(tier[3], PC_RAR_SSR);
    CHECK_EQ_INT(tier[5], PC_RAR_SR);   /* 1 play: position 2 */
    CHECK_EQ_INT(tier[7], PC_RAR_SR);   /* 2: position 3 */
    CHECK_EQ_INT(tier[2], PC_RAR_R);    /* 3: position 4 */
    CHECK_EQ_INT(tier[6], PC_RAR_R);    /* 7: position 5 */
    CHECK_EQ_INT(tier[4], PC_RAR_N);
    CHECK_EQ_INT(tier[1], PC_RAR_N);    /* the most played */
    /* most never played: every unplayed track is SSR, every played one N */
    uint32_t p2[8] = { 0, 0, 0, 0, 0, 0, 0, 5 };
    pc_gacha_tiers(p2, 8, scratch, tier);
    for (int i = 0; i < 7; i++) CHECK_EQ_INT(tier[i], PC_RAR_SSR);
    CHECK_EQ_INT(tier[7], PC_RAR_N);
    /* ties share the lowest position */
    uint32_t p3[4] = { 4, 4, 4, 4 };
    pc_gacha_tiers(p3, 4, scratch, tier);
    for (int i = 0; i < 4; i++) CHECK_EQ_INT(tier[i], PC_RAR_SSR);
}

static void test_gacha_rates(void)
{
    int sum = 0;
    for (int k = 0; k < PC_RAR_COUNT; k++) sum += pc_rarity_rate[k];
    CHECK_EQ_INT(sum, 100);
    CHECK(pc_rarity_rate[PC_RAR_SSR] < pc_rarity_rate[PC_RAR_SR]);
    enum { N = 400 };
    uint32_t plays[N], scratch[N];
    uint8_t tier[N];
    for (int i = 0; i < N; i++) plays[i] = (uint32_t)i;   /* 100 tracks a rarity */
    pc_gacha_tiers(plays, N, scratch, tier);
    pc_rng_t r;
    pc_rng_seed(&r, 3);
    int c[PC_RAR_COUNT] = { 0 }, pulls = 40000;
    for (int i = 0; i < pulls; i++) {
        int idx;
        CHECK_EQ_INT(pc_gacha_pull(&r, tier, N, 1, &idx), 1);
        CHECK(idx >= 0 && idx < N);
        c[tier[idx]]++;
    }
    for (int k = 0; k < PC_RAR_COUNT; k++) CHECK_NEAR((double)c[k] / pulls, pc_rarity_rate[k] / 100.0, 0.01);
}

static void test_gacha_ten_pull_guarantee(void)
{
    /* only one SR-or-better track among 40: a plain ten-pull would often miss it */
    enum { N = 40 };
    uint8_t tier[N];
    for (int i = 0; i < N; i++) tier[i] = PC_RAR_N;
    tier[17] = PC_RAR_SR;
    pc_rng_t r;
    pc_rng_seed(&r, 8);
    for (int k = 0; k < 2000; k++) {
        int idx[10], seen[N] = { 0 }, good = 0;
        CHECK_EQ_INT(pc_gacha_pull(&r, tier, N, 10, idx), 10);
        for (int i = 0; i < 10; i++) {
            CHECK(idx[i] >= 0 && idx[i] < N);
            CHECK(!seen[idx[i]]);                         /* no repeats: the library has ten or more */
            seen[idx[i]] = 1;
            good += tier[idx[i]] >= PC_RAR_SR;
        }
        CHECK(good >= 1);
    }
    /* all N: nothing to guarantee, still ten */
    for (int i = 0; i < N; i++) tier[i] = PC_RAR_N;
    int idx[10];
    CHECK_EQ_INT(pc_gacha_pull(&r, tier, N, 10, idx), 10);
    /* tiny library: repeats allowed */
    uint8_t t3[3] = { PC_RAR_N, PC_RAR_R, PC_RAR_SSR };
    CHECK_EQ_INT(pc_gacha_pull(&r, t3, 3, 10, idx), 10);
    for (int i = 0; i < 10; i++) CHECK(idx[i] >= 0 && idx[i] < 3);
    CHECK_EQ_INT(pc_gacha_pull(&r, t3, 0, 10, idx), 0);
}

static char *slurp(FILE *f)
{
    static char buf[4096];
    rewind(f);
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    return buf;
}

static void test_cards_update(void)
{
    FILE *out = tmpfile();
    const char *a[2] = { "mp3:/sdcard/export/A/x=y.mp3", "mp3:/sdcard/export/B/b.mp3" };
    uint32_t cnt[3];
    CHECK_EQ_INT(pc_cards_update(NULL, out, a, 2, cnt), 2);
    CHECK_EQ_INT(cnt[0], 1);
    CHECK_EQ_INT(cnt[1], 1);
    CHECK_STR(slurp(out), "mp3:/sdcard/export/A/x=y.mp3=1\nmp3:/sdcard/export/B/b.mp3=1\n");
    /* again with one old id twice (a ten-pull with a repeat) and one new */
    FILE *in = out;
    rewind(in);
    FILE *out2 = tmpfile();
    const char *b[3] = { "mp3:/sdcard/export/B/b.mp3", "mp3:/new.mp3", "mp3:/sdcard/export/B/b.mp3" };
    CHECK_EQ_INT(pc_cards_update(in, out2, b, 3, cnt), 3);
    CHECK_EQ_INT(cnt[0], 3);
    CHECK_EQ_INT(cnt[1], 1);
    CHECK_EQ_INT(cnt[2], 3);
    CHECK_STR(slurp(out2), "mp3:/sdcard/export/A/x=y.mp3=1\nmp3:/sdcard/export/B/b.mp3=3\nmp3:/new.mp3=1\n");
    fclose(in);
    fclose(out2);
    /* junk lines are dropped, counts kept */
    FILE *j = tmpfile();
    fputs("garbage\n\nmp3:/a.mp3=7\n", j);
    rewind(j);
    FILE *o3 = tmpfile();
    const char *c1[1] = { "mp3:/a.mp3" };
    CHECK_EQ_INT(pc_cards_update(j, o3, c1, 1, cnt), 1);
    CHECK_EQ_INT(cnt[0], 8);
    CHECK_STR(slurp(o3), "mp3:/a.mp3=8\n");
    fclose(j);
    fclose(o3);
}

static void test_tarot_draw(void)
{
    pc_rng_t r;
    pc_rng_seed(&r, 13);
    int seen[PC_TAROT_N] = { 0 }, rev = 0, N = 44000;
    for (int i = 0; i < N; i++) {
        pc_tarot_t t;
        pc_tarot_draw(&r, &t);
        CHECK(t.card >= 0 && t.card < PC_TAROT_N);
        seen[t.card]++;
        rev += t.reversed;
    }
    for (int k = 0; k < PC_TAROT_N; k++) CHECK_NEAR((double)seen[k] / N, 1.0 / PC_TAROT_N, 0.008);
    CHECK_NEAR((double)rev / N, 0.5, 0.01);
    pc_tarot_t t1, t2;                                  /* one card a day */
    pc_rng_seed(&r, pc_fortune_day_seed(2026, 11, 10, PC_SALT_TAROT));
    pc_tarot_draw(&r, &t1);
    pc_rng_seed(&r, pc_fortune_day_seed(2026, 11, 10, PC_SALT_TAROT));
    pc_tarot_draw(&r, &t2);
    CHECK_EQ_INT(t1.card, t2.card);
    CHECK_EQ_INT(t1.reversed, t2.reversed);
}

int main(void)
{
    RUN(test_rng_below_range);
    RUN(test_day_seed_stable);
    RUN(test_omikuji_distribution);
    RUN(test_omikuji_stars_follow_rank);
    RUN(test_omikuji_verse_matches_rank);
    RUN(test_kuji_parse_rank);
    RUN(test_fortune_lines);
    RUN(test_jiao_probabilities);
    RUN(test_dice_range);
    RUN(test_wheel_index);
    RUN(test_wheel_spin_stops_on_target);
    RUN(test_gacha_tiers);
    RUN(test_gacha_rates);
    RUN(test_gacha_ten_pull_guarantee);
    RUN(test_cards_update);
    RUN(test_tarot_draw);
    return 0;
}
