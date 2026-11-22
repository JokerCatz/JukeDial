// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_rsq.h"
#include "pc_test.h"

/* hires T10b fix 2 (review Important 2): the store order and the acquire/release kind of the table hand-off (pc_rsq.h, "the hand-off, step by
 * step"). The host tests run one thread, so a race cannot be seen; instead pc_rsq.c is built here with an instrumented atomic layer
 * (rsq_order_hook.h): every load and store of the shared state is recorded, and at each store of `ready` the state the other task would see
 * is checked. Fails when ready = 0 is stored before the live flip, when pair / gen are written after ready = 1, and when any of these
 * loads / stores is not acquire / release. */

typedef struct { const char *f; unsigned long v; int mo, st; int live_at; pc_rsq_pair_t spare_at; uint32_t gen_at; } ev_t;
static ev_t s_ev[64];
static int s_n;
static pc_rsq_swap_t *s_sw;

void rsq_hook(const char *field, const volatile void *addr, unsigned long v, int mo, int is_store)
{
    (void)addr;
    CHECK(s_n < 64);
    ev_t *e = &s_ev[s_n++];
    e->f = field; e->v = v; e->mo = mo; e->st = is_store;
    e->live_at = s_sw ? s_sw->live : -1;
    if (s_sw) { e->spare_at = s_sw->pair[!s_sw->live]; e->gen_at = s_sw->gen; }
}

static const ev_t *find(const char *f, int st, int from) { for (int i = from; i < s_n; i++) if (s_ev[i].st == st && !strcmp(s_ev[i].f, f)) return &s_ev[i]; return NULL; }
static int idx(const ev_t *e) { return (int)(e - s_ev); }
static void every_kind_ok(void)            /* every recorded load is acquire, every store release */
{
    for (int i = 0; i < s_n; i++) {
        if (s_ev[i].st && s_ev[i].mo != __ATOMIC_RELEASE) { fprintf(stderr, "store %s not release (%d)\n", s_ev[i].f, s_ev[i].mo); exit(1); }
        if (!s_ev[i].st && s_ev[i].mo != __ATOMIC_ACQUIRE) { fprintf(stderr, "load %s not acquire (%d)\n", s_ev[i].f, s_ev[i].mo); exit(1); }
    }
}
static pc_rsq_pair_t pr(uint32_t in, uint32_t out) { pc_rsq_pair_t p = { in, out }; return p; }
static const pc_rsq_pair_t NONE = { 0, 0 };

static void test_order(void)
{
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *q;
    pc_rsq_swap_t s;
    pc_rsq_pair_t p;
    pc_rsq_swap_setup(&s, A, B);
    s_sw = &s;

    /* builder, steps 1-2: loads ready (acquire) before live; pair and gen are in place when ready = 1 is stored (release) */
    s_n = 0;
    pc_rsq_t *sp = pc_rsq_swap_spare(&s, pr(44100, 48000), NONE, &p);
    CHECK(sp == s.blk[1]);
    const ev_t *lr = find("s->ready", 0, 0), *ll = find("s->live", 0, 0);
    CHECK(lr && ll && idx(lr) < idx(ll));
    every_kind_ok();
    CHECK_EQ_INT(pc_rsq_init(sp, p.in, p.out), 0);
    s_n = 0;
    pc_rsq_swap_publish(&s, p);
    const ev_t *r1 = find("s->ready", 1, 0);
    CHECK(r1 && r1->v == 1);
    CHECK_EQ_INT(r1->spare_at.in, 44100);                        /* the pair was written before ready = 1 */
    CHECK_EQ_INT(r1->spare_at.out, 48000);
    CHECK_EQ_INT(r1->gen_at, 1);                                 /* and the gen */
    CHECK(idx(r1) == s_n - 1);                                   /* nothing written after it */
    every_kind_ok();

    /* audio task, steps 3-4a: ready loaded (acquire) first; the live flip stored before ready = 0 */
    s_n = 0;
    CHECK_EQ_INT(pc_rsq_swap_take(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    CHECK(q == B);
    CHECK(s_n > 0 && !s_ev[0].st && !strcmp(s_ev[0].f, "s->ready"));
    const ev_t *fl = find("s->live", 1, 0);
    const ev_t *r0 = find("s->ready", 1, 0);
    CHECK(fl && fl->v == 1 && r0 && r0->v == 0 && idx(fl) < idx(r0));   /* 10c: the old block was never built (no cache): free, ready 0 after the flip */
    CHECK_EQ_INT(r0->live_at, 1);
    CHECK(idx(r0) == s_n - 1);
    every_kind_ok();

    /* 10c: a second swap leaves the old table as a cached ready spare (ready stays 1, no ready store) */
    CHECK(pc_rsq_swap_spare(&s, pr(48000, 24000), NONE, &p) == A);
    CHECK_EQ_INT(pc_rsq_init(A, p.in, p.out), 0);
    pc_rsq_swap_publish(&s, p);
    s_n = 0;
    CHECK_EQ_INT(pc_rsq_swap_take(&s, 48000, 24000, &q), PC_RSQ_SWAP);
    CHECK(q == A && s.ready == 1 && s.live == 0);
    CHECK(find("s->live", 1, 0) != NULL && !find("s->ready", 1, 0));
    every_kind_ok();
    pc_rsq_swap_restart(&s);
    /* 10c: the cached spare (44.1k, the plan's own pair) stays while there is nothing else to build; reclaimed (handed back, rebuilt) only for another pair */
    s_n = 0;
    CHECK(pc_rsq_swap_spare(&s, pr(44100, 48000), NONE, &p) == NULL);
    CHECK(!find("s->reclaim", 1, 0));
    CHECK(pc_rsq_swap_spare(&s, pr(48000, 24000), pr(32000, 48000), &p) == NULL);
    CHECK(find("s->reclaim", 1, 0) != NULL);
    every_kind_ok();
    CHECK_EQ_INT(pc_rsq_swap_take(&s, 48000, 24000, &q), PC_RSQ_USE);
    sp = pc_rsq_swap_spare(&s, pr(48000, 24000), pr(32000, 48000), &p);
    CHECK(sp == B);
    CHECK_EQ_INT(p.in, 32000);
    every_kind_ok();
    CHECK_EQ_INT(pc_rsq_init(sp, p.in, p.out), 0);
    pc_rsq_swap_publish(&s, p);

    /* reclaim (fix 2, step 4b): the builder stores reclaim (release); the audio task's next block loads ready, then reclaim / gen, then hands
     * back with ready = 0 (release) and no flip */
    s_n = 0;
    CHECK(pc_rsq_swap_spare(&s, pr(44100, 48000), pr(22050, 48000), &p) == NULL);   /* the 32k next track was replaced */
    const ev_t *rc = find("s->reclaim", 1, 0);
    CHECK(rc && (uint32_t)rc->v == 3u * 65536u + (uint32_t)s.swaps);
    every_kind_ok();
    s_n = 0;
    CHECK_EQ_INT(pc_rsq_swap_take(&s, 48000, 24000, &q), PC_RSQ_USE);
    CHECK(q == A);
    const ev_t *lr2 = find("s->ready", 0, 0), *lc = find("s->reclaim", 0, 0), *hb = find("s->ready", 1, 0);
    CHECK(lr2 && lc && hb && hb->v == 0 && idx(lr2) < idx(lc) && idx(lc) < idx(hb));
    CHECK(!find("s->live", 1, 0));                               /* no flip */
    CHECK_EQ_INT(s.live, 0);
    every_kind_ok();

    /* WAIT hand-back (step 4b): the request stored before ready = 0, so a builder that sees ready 0 builds the asked pair */
    CHECK(pc_rsq_swap_spare(&s, pr(44100, 48000), pr(22050, 48000), &p) == B);
    CHECK_EQ_INT(pc_rsq_init(B, p.in, p.out), 0);
    pc_rsq_swap_publish(&s, p);
    s_n = 0;
    CHECK_EQ_INT(pc_rsq_swap_take(&s, 32000, 48000, &q), PC_RSQ_WAIT);
    const ev_t *rq = find("s->req.out", 1, 0), *hw = find("s->ready", 1, 0);
    CHECK(rq && hw && hw->v == 0 && idx(rq) < idx(hw));
    CHECK(!find("s->live", 1, 0));
    every_kind_ok();
    s_sw = NULL;
    free(A);
    free(B);
}

int main(void)
{
    RUN(test_order);
    return 0;
}
