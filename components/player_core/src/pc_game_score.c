// SPDX-License-Identifier: LGPL-2.1-or-later
/* The session score of a game page (pc_game_score.h). */
#include "pc/pc_game_score.h"
#include <string.h>

void pc_game_score_reset(pc_game_score_t *s) { memset(s, 0, sizeof *s); }

pc_game_rec_t pc_game_score_record(pc_game_score_t *s, int variant, int outcome, bool vs_device, int moves, uint32_t ms)
{
    pc_game_rec_t r = { 0 };
    bool ok = variant >= 0 && variant < PC_GAME_VARIANTS;
    s->played++;
    if (outcome == PC_GAME_WIN) {
        s->wins++;
        if (ok && moves > 0) {
            uint16_t *bm = &s->best_moves[variant];
            uint32_t *bt = &s->best_ms[variant];
            if (!*bm || moves < *bm || (moves == *bm && ms < *bt)) { *bm = (uint16_t)(moves > 65535 ? 65535 : moves); *bt = ms; r.new_best = true; }
        }
    }
    if (vs_device) {
        if (outcome == PC_GAME_WIN) { if (s->streak < 65535) s->streak++; }
        else s->streak = 0;
        if (s->streak > s->best_streak) s->best_streak = s->streak;
        r.streak_hit = outcome == PC_GAME_WIN && s->streak >= PC_GAME_STREAK;
    }
    r.streak = s->streak;
    if (ok) { r.best_moves = s->best_moves[variant]; r.best_ms = s->best_ms[variant]; }
    return r;
}

pc_game_rec_t pc_game_score_record_high(pc_game_score_t *s, int variant, int score, uint32_t ms)
{
    pc_game_rec_t r = { 0 };
    s->played++;
    r.streak = s->streak;
    if (variant < 0 || variant >= PC_GAME_VARIANTS) return r;
    uint16_t v = (uint16_t)(score > 65535 ? 65535 : score < 0 ? 0 : score);
    if (v > s->best_moves[variant]) { s->best_moves[variant] = v; s->best_ms[variant] = ms; r.new_best = true; }
    r.best_moves = s->best_moves[variant];
    r.best_ms = s->best_ms[variant];
    return r;
}

pc_game_rec_t pc_game_score_points(pc_game_score_t *s, int variant, uint32_t points, uint32_t ms)
{
    pc_game_rec_t r = { 0 };
    bool ok = variant >= 0 && variant < PC_GAME_VARIANTS;
    s->played++;
    if (ok && points > s->best_points[variant]) { s->best_points[variant] = points; s->best_ms[variant] = ms; r.new_best = true; }
    r.streak = s->streak;
    if (ok) { r.best_points = s->best_points[variant]; r.best_ms = s->best_ms[variant]; }
    return r;
}

bool pc_game_gold(uint32_t seed)
{
    uint32_t x = seed ^ 0x5bd1e995u;              /* a mix, so neighbouring seeds (one game apart) are not neighbours here */
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x % PC_GAME_GOLD_N == 0;
}
