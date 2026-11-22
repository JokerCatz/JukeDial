// SPDX-License-Identifier: LGPL-2.1-or-later
/* 猜歌 / Guess the song: the rules (pc_game_guess.h). */
#include "pc/pc_game_guess.h"
#include <string.h>
#include "pc/pc_utf.h"

#define KEY_CMP 12        /* two keys that agree in their first 12 bytes read the same on a sector (label) */

static uint32_t next(pc_guess_t *g)                          /* xorshift32: the game's own stream */
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x;
    return x;
}

void pc_guess_init(pc_guess_t *g, int rule, int rounds, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->rule = (uint8_t)(rule >= 0 && rule < PC_GUESS_RULES ? rule : PC_GUESS_CLASSIC);
    g->rounds = (uint8_t)(rounds < 1 ? 1 : rounds > PC_GUESS_MAX_ROUNDS ? PC_GUESS_MAX_ROUNDS : rounds);
    g->last_target = g->target = -1;
    g->rng = seed * 2654435761u ^ 0x9E3779B9u;
    if (!g->rng) g->rng = 0x6d2b79f5u;
    next(g);
}

static const char *key_of(pc_guess_key_fn key, void *u, int i) { const char *k = key ? key(u, i) : NULL; return k ? k : ""; }
static bool differs(const char *a, const char *b) { return strncmp(a, b, KEY_CMP) != 0; }

int pc_guess_ready(int n, pc_guess_key_fn key, void *u)
{
    const char *seen[PC_GUESS_CHOICES];
    int have = 0;
    for (int i = 0; i < n && have < PC_GUESS_CHOICES; i++) {
        const char *k = key_of(key, u, i);
        if (!k[0]) continue;
        bool fresh = true;
        for (int j = 0; j < have && fresh; j++) fresh = differs(seen[j], k);
        if (fresh) seen[have++] = k;
    }
    return have;
}

/* one more track for the question: its key is not empty and differs from the keys already taken; random draws first, then a walk from a random start */
static int pick(pc_guess_t *g, int n, pc_guess_key_fn key, void *u, const char *const *taken, int ntaken, int skip)
{
    for (int tries = 0; tries < 48; tries++) {
        int i = (int)(next(g) % (uint32_t)n);
        const char *k = key_of(key, u, i);
        bool ok = k[0] && i != skip;
        for (int j = 0; j < ntaken && ok; j++) ok = differs(taken[j], k);
        if (ok) return i;
    }
    int start = (int)(next(g) % (uint32_t)n);
    for (int d = 0; d < n; d++) {
        int i = (start + d) % n;
        const char *k = key_of(key, u, i);
        bool ok = k[0] && i != skip;
        for (int j = 0; j < ntaken && ok; j++) ok = differs(taken[j], k);
        if (ok) return i;
    }
    return -1;
}

bool pc_guess_ask(pc_guess_t *g, int n, pc_guess_key_fn key, void *u)
{
    if (g->over || g->open || g->asked >= g->rounds || n < PC_GUESS_CHOICES) return false;
    if (pc_guess_ready(n, key, u) < PC_GUESS_CHOICES) return false;
    const char *taken[PC_GUESS_CHOICES] = { NULL };
    int tr[PC_GUESS_CHOICES] = { 0 };
    int t = pick(g, n, key, u, taken, 0, g->last_target);
    if (t < 0) t = pick(g, n, key, u, taken, 0, -1);     /* only one track can be the target: it asks it again */
    if (t < 0) return false;
    tr[0] = t;
    taken[0] = key_of(key, u, t);
    for (int c = 1; c < PC_GUESS_CHOICES; c++) {
        int o = pick(g, n, key, u, taken, c, -1);
        if (o < 0) return false;
        tr[c] = o;
        taken[c] = key_of(key, u, o);
    }
    for (int i = PC_GUESS_CHOICES - 1; i > 0; i--) { int j = (int)(next(g) % (uint32_t)(i + 1)); int x = tr[i]; tr[i] = tr[j]; tr[j] = x; }   /* the answers in a random order */
    for (int c = 0; c < PC_GUESS_CHOICES; c++) { g->track[c] = tr[c]; if (tr[c] == t) g->answer = (uint8_t)c; }
    g->target = t;
    g->golden = next(g) % PC_GUESS_GOLD_N == 0;
    g->clip_pct = (uint8_t)(10 + next(g) % 50);
    g->asked++;
    g->open = 1;
    return true;
}

void pc_guess_skip(pc_guess_t *g)
{
    if (!g->open) return;
    g->open = 0;
    g->last_target = g->target;
    if (g->asked) g->asked--;
}

int pc_guess_mult(int streak) { int m = streak < 1 ? 1 : 1 + (streak - 1) / 2; return m > 4 ? 4 : m; }
uint32_t pc_guess_points(int streak, bool golden) { return 100u * (uint32_t)pc_guess_mult(streak) * (golden ? 2u : 1u); }

int pc_guess_answer(pc_guess_t *g, int sector)
{
    if (!g->open || sector < 0 || sector >= PC_GUESS_CHOICES) return PC_GUESS_IGNORED;
    g->open = 0;
    g->last_target = g->target;
    if (g->asked >= g->rounds) g->over = 1;
    if (sector != g->answer) { g->streak = 0; return PC_GUESS_WRONG; }
    g->correct++;
    if (g->streak < 255) g->streak++;
    if (g->streak > g->best_streak) g->best_streak = g->streak;
    g->points += pc_guess_points(g->streak, g->golden);
    return PC_GUESS_RIGHT;
}

uint32_t pc_guess_clip_ms(int rule) { return rule == PC_GUESS_SHORT ? 2000 : 5000; }

uint32_t pc_guess_clip_token(int kind, uint32_t size, const uint8_t *head, int pct)
{
    if (pct < 0 || pct > 99) pct = 0;
    if (kind == PC_CLIP_BYTES) return size < 65536u ? 0 : (size / 100u) * (uint32_t)pct;
    if (kind != PC_CLIP_FLAC || !head || memcmp(head, "fLaC", 4) || (head[4] & 0x7F) != 0) return 0;   /* the first block must be STREAMINFO */
    uint64_t total = ((uint64_t)(head[21] & 0x0F) << 32) | ((uint64_t)head[22] << 24) | ((uint64_t)head[23] << 16) | ((uint64_t)head[24] << 8) | head[25];
    uint64_t at = total / 100u * (uint64_t)pct;
    return total < 44100u * 4u || at > 0xFFFFFFFFu ? 0 : (uint32_t)at;      /* under 4 s of audio: from the start */
}

void pc_guess_label(char out[PC_GUESS_LABEL], const char *s)
{
    if (!s) s = "";
    if (strlen(s) <= PC_GUESS_LABEL - 1) { pc_utf8_copy(out, PC_GUESS_LABEL, s); return; }
    size_t n = pc_utf8_copy(out, PC_GUESS_LABEL - 3, s);      /* 12 bytes at most, never inside a character */
    memcpy(out + n, "\xE2\x80\xA6", 4);                       /* U+2026 and the NUL */
}
