// SPDX-License-Identifier: LGPL-2.1-or-later
/* See fz_alloc.h. Not force-included into itself: the real malloc family is used here. */
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static long long s_live;
static size_t s_cap = 32u << 20, s_one = 16u << 20;
static int s_trunc32, s_init;
static unsigned long long s_fail;

static void init(void)
{
    const char *e;
    s_init = 1;
    if ((e = getenv("FZ_TRUNC32")) && *e == '1') s_trunc32 = 1;
    if ((e = getenv("FZ_CAP"))) s_cap = (size_t)strtoull(e, NULL, 0);
    if ((e = getenv("FZ_ONE"))) s_one = (size_t)strtoull(e, NULL, 0);
}
long long fz_live(void) { return s_live; }
unsigned long long fz_failed(void) { return s_fail; }
void fz_reset(void) { s_live = 0; s_fail = 0; }

void *fz_malloc(size_t n)
{
    if (!s_init) init();
    if (s_trunc32) n = (uint32_t)n;
    if (n > s_one || (size_t)s_live + n > s_cap) { s_fail++; return NULL; }
    void *p = malloc(n ? n : 1);
    if (p) s_live += (long long)malloc_usable_size(p);
    return p;
}
void *fz_calloc(size_t n, size_t size)
{
    if (!s_init) init();
    unsigned long long t = (unsigned long long)n * size;
    if (size && t / size != n) { s_fail++; return NULL; }
    if (s_trunc32 && t > 0xffffffffull) { s_fail++; return NULL; }
    if (t > s_one || (size_t)s_live + t > s_cap) { s_fail++; return NULL; }
    void *p = calloc(t ? t : 1, 1);
    if (p) s_live += (long long)malloc_usable_size(p);
    return p;
}
void *fz_realloc(void *p, size_t n)
{
    if (!s_init) init();
    if (s_trunc32) n = (uint32_t)n;
    if (!p) return fz_malloc(n);
    if (n == 0) { s_live -= (long long)malloc_usable_size(p); free(p); return NULL; }
    long long old = (long long)malloc_usable_size(p);
    if (n > s_one || (size_t)(s_live - old) + n > s_cap) { s_fail++; return NULL; }
    void *q = realloc(p, n);
    if (q) s_live += (long long)malloc_usable_size(q) - old;
    return q;
}
void fz_free(void *p)
{
    if (!p) return;
    s_live -= (long long)malloc_usable_size(p);
    free(p);
}
