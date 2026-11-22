// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Fortune / draw series (spec an internal note): pure logic, no statics.
 * Every draw takes an injectable random state, so a day seed gives the same result all day and tests are exact. */

/* ---- random numbers: splitmix64 ---- */
typedef struct { uint64_t s; } pc_rng_t;
void     pc_rng_seed(pc_rng_t *r, uint64_t seed);
uint32_t pc_rng_next(pc_rng_t *r);
uint32_t pc_rng_below(pc_rng_t *r, uint32_t n);        /* uniform 0..n-1 (no modulo bias); n 0 -> 0 */

/* The same local date and salt always give the same seed; salts keep the omikuji and tarot of a day apart. */
#define PC_SALT_OMIKUJI 0x6f6d696bu
#define PC_SALT_TAROT   0x7461726fu
uint64_t pc_fortune_day_seed(int year, int month, int day, uint32_t salt);

/* ---- today's omikuji (今日籤) ---- */
typedef enum { PC_KUJI_DAIKICHI, PC_KUJI_KICHI, PC_KUJI_CHUKICHI, PC_KUJI_SHOKICHI, PC_KUJI_SUEKICHI, PC_KUJI_KYO, PC_KUJI_COUNT } pc_kuji_t;
/* weights out of 100 (a common temple-box mix, 凶 about 1/10): 大吉 16, 吉 22, 中吉 18, 小吉 18, 末吉 16, 凶 10 */
extern const uint8_t pc_kuji_weight[PC_KUJI_COUNT];
pc_kuji_t pc_kuji_rank(pc_rng_t *r);
/* the four small items (music, work, people, health), 1..5 stars, centred on the rank (大吉 4.5 ... 凶 2) */
#define PC_KUJI_ASPECTS 4
typedef struct { pc_kuji_t rank; int verse; uint8_t stars[PC_KUJI_ASPECTS]; } pc_omikuji_t;
/* verse_rank[i]: the rank verse i is written for, or -1 = any rank. The verse is one written for the rank
 * (or for any rank); when there is none, any verse. n 0 -> verse -1. */
void pc_omikuji_draw(pc_rng_t *r, const int8_t *verse_rank, int n, pc_omikuji_t *out);
/* "大吉" / "吉" ... "凶" (UTF-8) -> rank; "*" or "" -> -1; anything else -> -2 */
int  pc_kuji_parse_rank(const char *s);

/* ---- text lists on the TF card (omikuji.txt, wheel.txt) ----
 * One item a line; '#' starts a comment line; blank lines and a UTF-8 BOM are skipped; spaces around an item are
 * trimmed; items longer than max_len bytes are cut at a UTF-8 character boundary. Parsed in place (text is
 * modified); out[] points into text. Returns the number of items, at most max. */
int  pc_fortune_lines(char *text, char **out, int max, size_t max_len);

/* ---- moon blocks (擲筊): each block lands flat side up with p 1/2 ---- */
typedef enum { PC_JIAO_SHENG, PC_JIAO_XIAO, PC_JIAO_YIN } pc_jiao_t;   /* 聖筊 one up one down, 笑筊 both flat up, 陰筊 both round up */
typedef struct { bool flat_up[2]; pc_jiao_t result; } pc_jiao_throw_t;
void pc_jiao_throw(pc_rng_t *r, pc_jiao_throw_t *out);
/* consecutive 聖筊: returns the new streak (0 after any other result) */
static inline int pc_jiao_streak(int streak, pc_jiao_t res) { return res == PC_JIAO_SHENG ? streak + 1 : 0; }

/* ---- dice ---- */
static inline int pc_dice_roll(pc_rng_t *r) { return 1 + (int)pc_rng_below(r, 6); }

/* ---- the wheel ----
 * n equal segments clockwise from the pointer at the top; a needle turns clockwise by `angle` degrees and
 * points at segment floor((angle mod 360) / (360 / n)). A spin picks the target first (uniform), then the total
 * turn: 3..5 whole turns plus a landing point inside the target segment (never within 15 % of its edges). */
int   pc_wheel_index(double angle_deg, int n);
typedef struct { int target; double start, total; } pc_wheel_spin_t;
void  pc_wheel_plan(pc_rng_t *r, int n, double start_deg, pc_wheel_spin_t *out);
/* angle at t (0..1) of the spin: ease-out cubic, so the needle slows down to a stop at start + total */
double pc_wheel_angle_at(const pc_wheel_spin_t *s, double t);

/* ---- song gacha (抽卡) ----
 * Rarity from play counts, by quantile of the library: a track's position = how many tracks have fewer plays
 * (ties share the lowest position). position < 25 % of the library -> SSR, < 50 % SR, < 75 % R, else N.
 * So never-played tracks are always SSR, and the most played quarter is N. A pull picks the rarity first
 * (SSR 10 %, SR 25 %, R 35 %, N 30 %), then a track of that rarity uniformly; an empty rarity falls back to the
 * next more common one (wrapping to the rarer ones). A ten-pull holds at least one SR or better when the
 * library has any, and no track twice when the library has ten or more. */
typedef enum { PC_RAR_N, PC_RAR_R, PC_RAR_SR, PC_RAR_SSR, PC_RAR_COUNT } pc_rarity_t;
extern const uint8_t pc_rarity_rate[PC_RAR_COUNT];               /* percent, indexed by pc_rarity_t */
/* scratch: n uint32 the caller provides (sorted copy of plays); tier_out: n bytes */
void pc_gacha_tiers(const uint32_t *plays, int n, uint32_t *scratch, uint8_t *tier_out);
/* count pulls (1 or 10, at most 10) into idx_out; returns the number pulled (0 for an empty library) */
int  pc_gacha_pull(pc_rng_t *r, const uint8_t *tier, int n, int count, int *idx_out);

/* ---- the collection file (/sdcard/fortune/cards.txt): one "<id>=<times drawn>" per line, split at the last '='.
 * Copies in to out with the ids' counts raised by one (new ids appended); counts_out[i] = id i's new count.
 * Returns the number of distinct ids in out, or -1 on a write error. in may be NULL (no file yet). */
int  pc_cards_update(FILE *in, FILE *out, const char *const *ids, int n, uint32_t *counts_out);

/* ---- tarot: 22 major arcana ---- */
#define PC_TAROT_N 22
typedef struct { int card; bool reversed; } pc_tarot_t;
void pc_tarot_draw(pc_rng_t *r, pc_tarot_t *out);

#ifdef __cplusplus
}
#endif
