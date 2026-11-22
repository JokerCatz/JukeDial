// SPDX-License-Identifier: LGPL-2.1-or-later
/* The pure half of games.txt (pc_gamesave.h). */
#include "pc/pc_gamesave.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fnv(const char *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)p[i]) * 16777619u;
    return h;
}

void pc_gs_init(pc_gs_t *g) { memset(g, 0, sizeof *g); }

bool pc_gs_valid_key(const char *key)
{
    if (!key) return false;
    size_t n = strlen(key);
    if (n < 1 || n >= PC_GS_KEY_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
    }
    return true;
}

static int find(const pc_gs_t *g, const char *key)
{
    for (int i = 0; i < g->n; i++) if (!strcmp(g->e[i].key, key)) return i;
    return -1;
}

bool pc_gs_get(const pc_gs_t *g, const char *key, uint32_t *v)
{
    int i = pc_gs_valid_key(key) ? find(g, key) : -1;
    if (i < 0) return false;
    if (v) *v = g->e[i].val;
    return true;
}

uint32_t pc_gs_get_or(const pc_gs_t *g, const char *key, uint32_t def)
{
    uint32_t v;
    return pc_gs_get(g, key, &v) ? v : def;
}

bool pc_gs_set(pc_gs_t *g, const char *key, uint32_t v)
{
    if (!pc_gs_valid_key(key)) return false;
    int i = find(g, key);
    if (i < 0) {
        if (g->n >= PC_GS_ENTRIES) return false;
        i = g->n++;
        snprintf(g->e[i].key, sizeof g->e[i].key, "%s", key);
    } else if (g->e[i].val == v) return true;
    g->e[i].val = v;
    g->dirty = true;
    return true;
}

size_t pc_gs_format(const pc_gs_t *g, char *buf, size_t cap)
{
    size_t n = 0;
    int r = snprintf(buf, cap, "pcgs1\n");
    if (r < 0 || (size_t)r >= cap) return 0;
    n = (size_t)r;
    for (int i = 0; i < g->n; i++) {
        r = snprintf(buf + n, cap - n, "%s=%u\n", g->e[i].key, (unsigned)g->e[i].val);
        if (r < 0 || (size_t)r >= cap - n) return 0;
        n += (size_t)r;
    }
    uint32_t h = fnv(buf, n);
    r = snprintf(buf + n, cap - n, "end=%d %08x\n", g->n, (unsigned)h);
    if (r < 0 || (size_t)r >= cap - n) return 0;
    n += (size_t)r;
    return n > PC_GS_FILE_MAX ? 0 : n;
}

int pc_gs_parse(pc_gs_t *g, const char *text, size_t len)
{
    pc_gs_init(g);
    if (len == 0) return 0;
    if (len > PC_GS_FILE_MAX) return PC_GS_BAD_SIZE;
    if (len < 6 || memcmp(text, "pcgs1\n", 6)) return PC_GS_BAD_HEADER;
    size_t pos = 6;
    bool ended = false;
    while (pos < len) {
        const char *nl = memchr(text + pos, '\n', len - pos);
        if (!nl) { pc_gs_init(g); return PC_GS_BAD_END; }          /* an unfinished last line: a cut file */
        size_t ll = (size_t)(nl - (text + pos));
        char line[PC_GS_KEY_MAX + 16];
        if (ll >= sizeof line) { pc_gs_init(g); return PC_GS_BAD_LINE; }
        memcpy(line, text + pos, ll);
        line[ll] = 0;
        if (ended) { pc_gs_init(g); return PC_GS_BAD_END; }        /* something after the end line */
        if (!strncmp(line, "end=", 4)) {
            int cnt;
            unsigned sum;
            char junk;
            if (sscanf(line + 4, "%d %8x%c", &cnt, &sum, &junk) != 2 || cnt != g->n) { pc_gs_init(g); return PC_GS_BAD_END; }
            if (sum != fnv(text, pos)) { pc_gs_init(g); return PC_GS_BAD_SUM; }
            ended = true;
        } else {
            char *eq = strchr(line, '=');
            if (!eq) { pc_gs_init(g); return PC_GS_BAD_LINE; }
            *eq = 0;
            const char *vs = eq + 1;
            char *endp;
            unsigned long long v = 0;
            bool digits = *vs >= '0' && *vs <= '9';
            if (digits) v = strtoull(vs, &endp, 10);
            if (!digits || *endp || v > 0xFFFFFFFFull || !pc_gs_valid_key(line) || find(g, line) >= 0 || g->n >= PC_GS_ENTRIES) { pc_gs_init(g); return PC_GS_BAD_LINE; }
            memcpy(g->e[g->n].key, line, strlen(line) + 1);   /* valid: shorter than PC_GS_KEY_MAX */
            g->e[g->n++].val = (uint32_t)v;
        }
        pos += ll + 1;
    }
    if (!ended) { pc_gs_init(g); return PC_GS_BAD_END; }
    return g->n;
}

bool pc_gs_due(const pc_gs_t *g, uint32_t now_ms, uint32_t debounce_ms)
{
    return g->dirty && (!g->wrote || (uint32_t)(now_ms - g->last_write) >= debounce_ms);
}

void pc_gs_written(pc_gs_t *g, uint32_t now_ms) { g->dirty = false; g->wrote = true; g->last_write = now_ms; }

static void skey(char *b, size_t n, const char *id, char kind, int v) { snprintf(b, n, "%s.%c%d", id, kind, v); }

void pc_gs_load_score(const pc_gs_t *g, const char *id, pc_game_score_t *s)
{
    char k[PC_GS_KEY_MAX + 8];
    uint32_t v;
    for (int i = 0; i < PC_GAME_VARIANTS; i++) {
        skey(k, sizeof k, id, 'm', i); if (pc_gs_get(g, k, &v)) s->best_moves[i] = (uint16_t)(v > 65535 ? 65535 : v);
        skey(k, sizeof k, id, 't', i); if (pc_gs_get(g, k, &v)) s->best_ms[i] = v;
        skey(k, sizeof k, id, 'p', i); if (pc_gs_get(g, k, &v)) s->best_points[i] = v;
    }
}

bool pc_gs_store_score(pc_gs_t *g, const char *id, const pc_game_score_t *s)
{
    char k[PC_GS_KEY_MAX + 8];
    bool ok = strlen(id) <= PC_GS_ID_MAX;
    for (int i = 0; i < PC_GAME_VARIANTS; i++) {
        if (s->best_moves[i]) { skey(k, sizeof k, id, 'm', i); ok &= pc_gs_set(g, k, s->best_moves[i]); }     /* only what exists: a key a game never earns is never written */
        if (s->best_points[i]) { skey(k, sizeof k, id, 'p', i); ok &= pc_gs_set(g, k, s->best_points[i]); }
        if (s->best_moves[i] || s->best_points[i]) { skey(k, sizeof k, id, 't', i); ok &= pc_gs_set(g, k, s->best_ms[i]); }
    }
    return ok;
}
