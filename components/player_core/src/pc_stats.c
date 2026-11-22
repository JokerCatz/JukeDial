// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *id; uint32_t plays; bool fav; } ent_t;
/* ids live in 4 KB chunks rather than one allocation each */
typedef struct chunk { struct chunk *next; size_t used, size; char data[]; } chunk_t;
struct pc_stats { ent_t *t; uint32_t cap, n; bool dirty; chunk_t *chunks; uint32_t fav_gen; };

static char *arena_dup(pc_stats_t *s, const char *id)
{
    size_t n = strlen(id) + 1;
    chunk_t *c = s->chunks;
    if (!c || c->size - c->used < n) {
        size_t size = n > 4000 ? n : 4000;
        if (!(c = malloc(sizeof *c + size))) return NULL;
        c->used = 0;
        c->size = size;
        c->next = s->chunks;
        s->chunks = c;
    }
    char *d = c->data + c->used;
    memcpy(d, id, n);
    c->used += n;
    return d;
}

static uint32_t hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

pc_stats_t *pc_stats_create(void)
{
    pc_stats_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->cap = 256;
    s->t = calloc(s->cap, sizeof *s->t);
    if (!s->t) { free(s); return NULL; }
    return s;
}

void pc_stats_destroy(pc_stats_t *s)
{
    if (!s) return;
    for (chunk_t *c = s->chunks, *nx; c; c = nx) { nx = c->next; free(c); }
    free(s->t);
    free(s);
}

static ent_t *find(const pc_stats_t *s, const char *id)
{
    for (uint32_t i = hash(id) & (s->cap - 1);; i = (i + 1) & (s->cap - 1)) {
        if (!s->t[i].id) return &s->t[i];
        if (!strcmp(s->t[i].id, id)) return &s->t[i];
    }
}

static bool grow(pc_stats_t *s)
{
    uint32_t cap = s->cap * 2;
    ent_t *old = s->t, *t = calloc(cap, sizeof *t);
    if (!t) return false;
    uint32_t oc = s->cap;
    s->t = t;
    s->cap = cap;
    for (uint32_t i = 0; i < oc; i++)
        if (old[i].id) *find(s, old[i].id) = old[i];
    free(old);
    return true;
}

/* entry for id, created when missing; NULL on allocation failure */
static ent_t *get(pc_stats_t *s, const char *id)
{
    ent_t *e = find(s, id);
    if (e->id) return e;
    if ((s->n + 1) * 10 > s->cap * 7) { if (!grow(s)) return NULL; e = find(s, id); }
    if (!(e->id = arena_dup(s, id))) return NULL;
    s->n++;
    return e;
}

bool pc_stats_is_fav(const pc_stats_t *s, const char *id) { ent_t *e = find(s, id); return e->id && e->fav; }
uint32_t pc_stats_plays(const pc_stats_t *s, const char *id) { ent_t *e = find(s, id); return e->id ? e->plays : 0; }
bool pc_stats_dirty(const pc_stats_t *s) { return s->dirty; }
uint32_t pc_stats_fav_gen(const pc_stats_t *s) { return s->fav_gen; }

void pc_stats_toggle_fav(pc_stats_t *s, const char *id)
{
    ent_t *e = get(s, id);
    if (e) { e->fav = !e->fav; s->dirty = true; s->fav_gen++; }
}

void pc_stats_add_play(pc_stats_t *s, const char *id)
{
    ent_t *e = get(s, id);
    if (e) { e->plays++; s->dirty = true; }
}

int pc_stats_list_favs(const pc_stats_t *s, const char *prefix, const char **out, int max)
{
    int n = 0;
    size_t pl = strlen(prefix);
    for (uint32_t i = 0; i < s->cap && n < max; i++)
        if (s->t[i].id && s->t[i].fav && !strncmp(s->t[i].id, prefix, pl)) out[n++] = s->t[i].id;
    return n;
}

int pc_stats_load(pc_stats_t *s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char line[600];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strrchr(line, '=');
        if (!eq || eq == line) continue;
        *eq = 0;
        int fav = 0;
        unsigned plays = 0;
        if (sscanf(eq + 1, "%d,%u", &fav, &plays) != 2) continue;
        ent_t *e = get(s, line);
        if (e) { e->fav = fav != 0; e->plays = plays; }
    }
    fclose(f);
    s->dirty = false;
    s->fav_gen++;
    return 0;
}

int pc_stats_save(pc_stats_t *s, const char *path)
{
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int ok = 1;
    for (uint32_t i = 0; i < s->cap; i++)
        if (s->t[i].id && (s->t[i].fav || s->t[i].plays))
            ok &= fprintf(f, "%s=%d,%u\n", s->t[i].id, s->t[i].fav ? 1 : 0, (unsigned)s->t[i].plays) > 0;
    ok &= fclose(f) == 0;
    if (!ok) { remove(tmp); return -1; }
    remove(path);
    if (rename(tmp, path)) return -1;
    s->dirty = false;
    return 0;
}
