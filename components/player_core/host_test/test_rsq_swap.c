// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_rsq.h"
#include "pc_test.h"

/* hires T10b (H29): the table is built off the audio task into a spare block; the audio task swaps at the pair change or drops the block
 * (waits) until the spare is ready. The rules (pc_rsq_pick, pc_rsq_build_pick) and the two-block state machine, driven step by step in the
 * orders the two tasks can take. bt_svc.c's hp_sink and its builder task only call these. */

static pc_rsq_pair_t pr(uint32_t in, uint32_t out) { pc_rsq_pair_t p = { in, out }; return p; }
static const pc_rsq_pair_t NONE = { 0, 0 };
#define CHECK_PAIR(a, I, O) do { pc_rsq_pair_t p_ = (a); CHECK_EQ_INT(p_.in, I); CHECK_EQ_INT(p_.out, O); } while (0)

static void test_pick(void)
{
    pc_rsq_pair_t a = pr(44100, 48000), b = pr(48000, 24000);
    CHECK_EQ_INT(pc_rsq_pick(pr(48000, 48000), a, b), PC_RSQ_PASS);     /* rate == link: no table, whatever the blocks hold */
    CHECK_EQ_INT(pc_rsq_pick(pr(44100, 44100), NONE, NONE), PC_RSQ_PASS);
    CHECK_EQ_INT(pc_rsq_pick(a, a, NONE), PC_RSQ_USE);
    CHECK_EQ_INT(pc_rsq_pick(a, a, b), PC_RSQ_USE);                     /* a ready spare for another pair does not matter */
    CHECK_EQ_INT(pc_rsq_pick(a, a, a), PC_RSQ_USE);                     /* the live one first: no needless swap */
    CHECK_EQ_INT(pc_rsq_pick(b, a, b), PC_RSQ_SWAP);
    CHECK_EQ_INT(pc_rsq_pick(b, NONE, b), PC_RSQ_SWAP);                 /* sink on: the first table */
    CHECK_EQ_INT(pc_rsq_pick(b, a, NONE), PC_RSQ_WAIT);                 /* not built yet: dropped, never at a's table */
    CHECK_EQ_INT(pc_rsq_pick(b, NONE, NONE), PC_RSQ_WAIT);
    CHECK_EQ_INT(pc_rsq_pick(pr(44100, 24000), a, b), PC_RSQ_WAIT);    /* same source rate, other link: another table */
    CHECK_EQ_INT(pc_rsq_pick(pr(48000, 48000 + 1), pr(48000, 48000), NONE), PC_RSQ_WAIT);
}

static void test_build_pick(void)
{
    pc_rsq_pair_t a = pr(44100, 48000), b = pr(48000, 24000), h = pr(32000, 48000);
    CHECK_PAIR(pc_rsq_build_pick(b, h, NONE, a), 48000, 24000);              /* the audio task's request first */
    CHECK_PAIR(pc_rsq_build_pick(NONE, h, NONE, a), 32000, 48000);           /* then the plan's hint */
    CHECK_PAIR(pc_rsq_build_pick(a, h, NONE, a), 32000, 48000);              /* a request the live table already serves: the hint */
    CHECK_PAIR(pc_rsq_build_pick(a, a, NONE, a), 0, 0);                      /* nothing new */
    CHECK_PAIR(pc_rsq_build_pick(pr(48000, 48000), pr(44100, 44100), NONE, a), 0, 0);   /* pass-through pairs need no table */
    CHECK_PAIR(pc_rsq_build_pick(pr(0, 48000), pr(44100, 0), NONE, NONE), 0, 0);        /* no rate yet (no source, no link) */
    CHECK_PAIR(pc_rsq_build_pick(NONE, NONE, NONE, NONE), 0, 0);
    CHECK_PAIR(pc_rsq_build_pick(NONE, a, NONE, NONE), 44100, 48000);        /* sink on, before the first block: the plan's pair */
    /* fix 1, the look-ahead: the queued next track's pair after the request and the plan of the moment */
    CHECK_PAIR(pc_rsq_build_pick(NONE, a, h, a), 32000, 48000);              /* the live table serves the blocks now: the next track's */
    CHECK_PAIR(pc_rsq_build_pick(NONE, a, h, NONE), 44100, 48000);           /* the blocks of the moment first */
    CHECK_PAIR(pc_rsq_build_pick(b, a, h, a), 48000, 24000);                 /* a waiting block before both */
    CHECK_PAIR(pc_rsq_build_pick(NONE, a, a, a), 0, 0);                      /* the next track at the same pair: nothing to build */
    CHECK_PAIR(pc_rsq_build_pick(NONE, a, pr(48000, 48000), a), 0, 0);       /* the next one passes through */
    CHECK_PAIR(pc_rsq_build_pick(NONE, NONE, h, NONE), 32000, 48000);
}

/* one "block" of the audio task as hp_sink makes it: the pick, the table it would process with */
static pc_rsq_pick_t blk(pc_rsq_swap_t *s, uint32_t in, uint32_t out, pc_rsq_t **q) { *q = (pc_rsq_t *)1; return pc_rsq_swap_take(s, in, out, q); }

/* the builder's whole step: spare -> pc_rsq_init -> publish; returns the block it built (NULL: nothing to do) */
static pc_rsq_t *build2(pc_rsq_swap_t *s, pc_rsq_pair_t now, pc_rsq_pair_t next, pc_rsq_pair_t *p)
{
    pc_rsq_t *q = pc_rsq_swap_spare(s, now, next, p);
    if (!q) return NULL;
    CHECK_EQ_INT(pc_rsq_init(q, p->in, p->out), 0);
    pc_rsq_swap_publish(s, *p);
    return q;
}
static pc_rsq_t *build(pc_rsq_swap_t *s, pc_rsq_pair_t hint, pc_rsq_pair_t *p) { return build2(s, hint, NONE, p); }

static void test_swap_flow(void)
{
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *q;
    pc_rsq_swap_t s;
    pc_rsq_pair_t p;
    pc_rsq_swap_setup(&s, A, B);
    CHECK(pc_rsq_swap_spare(&s, NONE, NONE, &p) == NULL);                    /* nothing asked, no hint */
    /* sink on, the first block at 44.1 -> 48: no table yet, it waits and asks */
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_WAIT);
    CHECK(q == NULL);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_WAIT);              /* still building: still waits, never another table */
    pc_rsq_t *b1 = build(&s, NONE, &p);
    CHECK(b1 != NULL);
    CHECK_PAIR(p, 44100, 48000);
    CHECK(pc_rsq_swap_spare(&s, NONE, NONE, &p) == NULL);                    /* ready: the builder leaves the spare alone */
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    CHECK(q == b1);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);
    CHECK(q == b1);
    CHECK(pc_rsq_swap_spare(&s, NONE, NONE, &p) == NULL);                    /* the request is served by the live table */
    /* a 48k track: pass-through, the table stays */
    CHECK_EQ_INT(blk(&s, 48000, 48000, &q), PC_RSQ_PASS);
    CHECK(q == NULL);
    /* the plan announces 48 -> 24 (LC3 low quality, say) before the first block: the spare is built ahead, the change swaps at once */
    pc_rsq_t *b2 = build(&s, pr(48000, 24000), &p);
    CHECK(b2 != NULL && b2 != b1);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);               /* the old pair still plays from its table */
    CHECK(q == b1);
    CHECK_EQ_INT(blk(&s, 48000, 24000, &q), PC_RSQ_SWAP);
    CHECK(q == b2);
    CHECK(pc_rsq_swap_spare(&s, NONE, NONE, &p) == NULL);                    /* a served request is cleared: the old pair is not rebuilt for nothing */
    /* 10c: back to the first pair: its table stayed as the ready spare, so it is a pointer swap, no wait; and again forth and back */
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    CHECK(q == b1);
    CHECK_EQ_INT(blk(&s, 48000, 24000, &q), PC_RSQ_SWAP);
    CHECK(q == b2);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    CHECK(q == b1);
    CHECK(pc_rsq_swap_spare(&s, NONE, NONE, &p) == NULL);              /* the cache stays: nothing else to build */
    CHECK(pc_rsq_swap_has(&s, pr(48000, 24000)) && pc_rsq_swap_has(&s, pr(44100, 48000)) && !pc_rsq_swap_has(&s, pr(32000, 48000)) && !pc_rsq_swap_has(&s, pr(48000, 48000)));
    /* a third pair evicts the older one (spare asked back at the next block, then rebuilt) */
    CHECK_EQ_INT(blk(&s, 32000, 48000, &q), PC_RSQ_WAIT);
    pc_rsq_t *b3 = pc_rsq_swap_spare(&s, NONE, NONE, &p);
    CHECK(b3 == b2);
    CHECK_PAIR(p, 32000, 48000);
    free(A);
    free(B);
}

/* the builder made a pair the audio task no longer wants (a hint that was wrong, a torn request, a second change during the build):
 * never used; handed back and the wanted pair built next */
static void test_wrong_spare(void)
{
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *q;
    pc_rsq_swap_t s;
    pc_rsq_pair_t p;
    pc_rsq_swap_setup(&s, A, B);
    CHECK(build(&s, pr(96000, 48000), &p) != NULL);                    /* hint: a 96k track before the plan halved it */
    CHECK_EQ_INT(blk(&s, 48000, 44100, &q), PC_RSQ_WAIT);              /* the block's real pair */
    CHECK(q == NULL);
    pc_rsq_t *b = build(&s, pr(96000, 48000), &p);                     /* the spare came back free; the request wins over the hint */
    CHECK(b != NULL);
    CHECK_PAIR(p, 48000, 44100);
    CHECK_EQ_INT(blk(&s, 48000, 44100, &q), PC_RSQ_SWAP);
    CHECK(q == b);
    /* a change during a build: the builder took 44.1 -> 48, the audio task meanwhile wants 32 -> 48 */
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_WAIT);
    pc_rsq_t *sp = pc_rsq_swap_spare(&s, NONE, NONE, &p);
    CHECK(sp != NULL);
    CHECK_PAIR(p, 44100, 48000);
    CHECK_EQ_INT(blk(&s, 32000, 48000, &q), PC_RSQ_WAIT);              /* nothing ready: the spare is not handed over half-built */
    CHECK_EQ_INT(pc_rsq_init(sp, p.in, p.out), 0);
    pc_rsq_swap_publish(&s, p);
    CHECK_EQ_INT(blk(&s, 32000, 48000, &q), PC_RSQ_WAIT);              /* ready but the wrong pair: handed back, not used */
    CHECK(q == NULL);
    CHECK(build(&s, NONE, &p) == sp);
    CHECK_PAIR(p, 32000, 48000);
    CHECK_EQ_INT(blk(&s, 32000, 48000, &q), PC_RSQ_SWAP);
    CHECK(q == sp);
    free(A);
    free(B);
}

/* fix 1: a gapless join to another rate. The next track is queued seconds before the join; the builder makes its table while the current one
 * plays, and the first block of the next track swaps at once - no waiting block, nothing dropped */
static void test_lookahead_join(void)
{
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *q;
    pc_rsq_swap_t s;
    pc_rsq_pair_t p, cur = pr(44100, 48000), nxt = pr(32000, 48000);
    pc_rsq_swap_setup(&s, A, B);
    CHECK(build2(&s, cur, NONE, &p) != NULL);                          /* sink on: the plan's pair */
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    pc_rsq_t *live = q;
    CHECK(pc_rsq_swap_spare(&s, cur, NONE, &p) == NULL);               /* nothing queued yet */
    pc_rsq_t *b = build2(&s, cur, nxt, &p);                            /* the next track was queued (audio_svc_info().next_rate) */
    CHECK(b != NULL && b != live);
    CHECK_PAIR(p, 32000, 48000);
    for (int i = 0; i < 5; i++) { CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE); CHECK(q == live); }   /* the current track to its end */
    CHECK(pc_rsq_swap_spare(&s, cur, nxt, &p) == NULL);                /* ready: kept for the join */
    CHECK_EQ_INT(blk(&s, 32000, 48000, &q), PC_RSQ_SWAP);              /* the join: the first block of the next track */
    CHECK(q == b);
    /* after the join the plan is the new pair (now == live) and nothing more is queued: nothing to build */
    CHECK(pc_rsq_swap_spare(&s, nxt, NONE, &p) == NULL);
    /* 10c: a next track at the old rate again: its table is still cached in the other block, nothing to build, the join is a pointer swap */
    CHECK(build2(&s, nxt, cur, &p) == NULL);
    CHECK_EQ_INT(blk(&s, 32000, 48000, &q), PC_RSQ_USE);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    CHECK(q == live);
    free(A);
    free(B);
}

/* fix 2: a ready spare nobody wants is reclaimed by the builder and the next track's table built into it (review Important 1) */
static void test_spare_unwanted(void)
{
    pc_rsq_pair_t a = pr(44100, 48000), b = pr(96000, 48000), n = pr(32000, 48000);
    CHECK(pc_rsq_spare_unwanted(b, NONE, pr(48000, 48000), a));           /* a 96k table after the plan halved to 48k (pass-through now) */
    CHECK(!pc_rsq_spare_unwanted(b, b, NONE, NONE));                     /* the audio task asks for it */
    CHECK(!pc_rsq_spare_unwanted(a, NONE, a, n));                        /* the plan's pair */
    CHECK(!pc_rsq_spare_unwanted(n, NONE, a, n));                        /* the next track's */
    CHECK(pc_rsq_spare_unwanted(n, NONE, a, pr(22050, 48000)));          /* the next track was replaced */
    CHECK(pc_rsq_spare_unwanted(n, NONE, a, NONE));                      /* the next track was cancelled */
}

/* hires T12 (carried from T10b's re-review): the builder meets a WANTED ready spare (the queued next track's pair) while the blocks are PASS or USE.
 * Nothing is reclaimed and nothing handed back: the spare stays ready through every block, and the join swaps at once (kills "reclaim every ready
 * spare" and "hand back without the wanted check") */
static void test_wanted_spare_kept(void)
{
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *q;
    pc_rsq_swap_t s;
    pc_rsq_pair_t p, cur = pr(44100, 48000), nxt = pr(32000, 48000), pass = pr(48000, 48000);
    pc_rsq_swap_setup(&s, A, B);
    CHECK(build2(&s, cur, NONE, &p) != NULL);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    pc_rsq_t *live = q;
    pc_rsq_t *b = build2(&s, cur, nxt, &p);                            /* the look-ahead for the queued track */
    CHECK(b != NULL && b != live);
    CHECK_EQ_INT(s.ready, 1);
    for (int i = 0; i < 4; i++) {
        CHECK(pc_rsq_swap_spare(&s, cur, nxt, &p) == NULL);            /* the builder looks: wanted, left alone (no reclaim written) */
        CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);           /* USE: the spare stays ready */
        CHECK(q == live);
        CHECK_EQ_INT(s.ready, 1);
        CHECK(pc_rsq_swap_spare(&s, cur, nxt, &p) == NULL);
        CHECK_EQ_INT(blk(&s, 48000, 48000, &q), PC_RSQ_PASS);          /* PASS (a stretch at the link rate): the spare stays ready */
        CHECK_EQ_INT(s.ready, 1);
    }
    CHECK(pc_rsq_swap_spare(&s, pass, nxt, &p) == NULL);               /* the plan moved to pass-through, the next track still wants it */
    CHECK_EQ_INT(blk(&s, 48000, 48000, &q), PC_RSQ_PASS);
    CHECK_EQ_INT(s.ready, 1);
    CHECK_EQ_INT(blk(&s, 32000, 48000, &q), PC_RSQ_SWAP);              /* the join: no wait, the wanted table */
    CHECK(q == b);
    free(A);
    free(B);
}

/* review scenario A: sink on with a 96k track on a 48k link. The first block (made before the plan halved) waits for 96k -> 48k, which is built
 * and then never used (the blocks pass through at 48k); a 44.1k next track is queued: its table is built in the reclaimed spare, the join swaps */
static void test_stale_96k_spare(void)
{
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *q;
    pc_rsq_swap_t s;
    pc_rsq_pair_t p, pass = pr(48000, 48000), nxt = pr(44100, 48000);
    pc_rsq_swap_setup(&s, A, B);
    CHECK_EQ_INT(blk(&s, 96000, 48000, &q), PC_RSQ_WAIT);              /* the block before the re-plan */
    CHECK(build2(&s, NONE, NONE, &p) != NULL);
    CHECK_PAIR(p, 96000, 48000);
    CHECK_EQ_INT(blk(&s, 48000, 48000, &q), PC_RSQ_PASS);              /* re-planned: pass-through */
    CHECK(pc_rsq_swap_spare(&s, pass, NONE, &p) == NULL);              /* nobody wants the 96k spare: reclaimed (nothing to build yet) */
    CHECK(pc_rsq_swap_spare(&s, pass, nxt, &p) == NULL);               /* the next track is queued; the reclaim stands until the next block... */
    CHECK_EQ_INT(blk(&s, 48000, 48000, &q), PC_RSQ_PASS);              /* ...handed back at the next block (no flip) */
    pc_rsq_t *b = build2(&s, pass, nxt, &p);                           /* ...and the next track's table built into it */
    CHECK(b != NULL);
    CHECK_PAIR(p, 44100, 48000);
    for (int i = 0; i < 3; i++) CHECK_EQ_INT(blk(&s, 48000, 48000, &q), PC_RSQ_PASS);   /* a ready, wanted spare is kept */
    CHECK(pc_rsq_swap_spare(&s, pass, nxt, &p) == NULL);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);              /* the join: no wait */
    CHECK(q == b);
    free(A);
    free(B);
}

/* review scenario C: the look-ahead was built for a 32k next track; a skip lands on another 44.1k track (the live table plays on) and a 22.05k
 * next track is queued: the 32k spare is reclaimed, 22.05k built, the join swaps */
static void test_skip_after_lookahead(void)
{
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *q;
    pc_rsq_swap_t s;
    pc_rsq_pair_t p, cur = pr(44100, 48000), n1 = pr(32000, 48000), n2 = pr(22050, 48000);
    pc_rsq_swap_setup(&s, A, B);
    CHECK(build2(&s, cur, NONE, &p) != NULL);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    pc_rsq_t *live = q;
    CHECK(build2(&s, cur, n1, &p) != NULL);                            /* the look-ahead for 32k */
    CHECK_PAIR(p, 32000, 48000);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);               /* the skip: another 44.1k track */
    CHECK(pc_rsq_swap_spare(&s, cur, n2, &p) == NULL);                 /* 22.05k queued: the 32k spare is reclaimed */
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);               /* handed back; the live table plays on */
    CHECK(q == live);
    pc_rsq_t *b = build2(&s, cur, n2, &p);
    CHECK(b != NULL && b != live);
    CHECK_PAIR(p, 22050, 48000);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);               /* the new spare is wanted: not handed back */
    CHECK_EQ_INT(blk(&s, 22050, 48000, &q), PC_RSQ_SWAP);              /* the join: no wait */
    CHECK(q == b);
    /* an old reclaim never takes a newer spare: the old block is free after the join, a 44.1k next track is built into it, and a reclaim
     * still standing from an earlier gen does not hand it back */
    CHECK(build2(&s, n2, cur, &p) == NULL);                            /* 10c: the 44.1k table is cached in the old block: nothing to build */
    s.reclaim = s.gen - 1;                                             /* as an earlier reclaim still standing */
    CHECK_EQ_INT(blk(&s, 22050, 48000, &q), PC_RSQ_USE);
    CHECK_EQ_INT(s.ready, 1);
    free(A);
    free(B);
}

/* fix 2: a block that no longer waits clears the request (a waited 96k pair after the re-plan is not "asked for" any more) */
static void test_request_cleared(void)
{
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *q;
    pc_rsq_swap_t s;
    pc_rsq_swap_setup(&s, A, B);
    CHECK_EQ_INT(blk(&s, 96000, 48000, &q), PC_RSQ_WAIT);
    CHECK_EQ_INT(s.req.in, 96000);
    CHECK_EQ_INT(blk(&s, 48000, 48000, &q), PC_RSQ_PASS);
    CHECK_EQ_INT(s.req.in, 0);
    CHECK_EQ_INT(s.req.out, 0);
    free(A);
    free(B);
}

static void test_no_blocks(void)
{
    pc_rsq_swap_t s;
    pc_rsq_pair_t p;
    pc_rsq_t *q;
    pc_rsq_swap_setup(&s, NULL, NULL);                                 /* the PSRAM allocation failed */
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_WAIT);
    CHECK(q == NULL);
    CHECK(pc_rsq_swap_spare(&s, pr(44100, 48000), NONE, &p) == NULL);
    CHECK_EQ_INT(blk(&s, 48000, 48000, &q), PC_RSQ_PASS);              /* pass-through still plays */
}

/* sink on again (or a pass-through stretch) at the same pair: the table is kept, the history starts silent - the output equals a fresh table's */
static void test_restart_and_reset(void)
{
    enum { N = 441 };
    float *in = malloc(sizeof(float) * 2 * N), *o1 = malloc(sizeof(float) * 2 * 600), *o2 = malloc(sizeof(float) * 2 * 600);
    for (int i = 0; i < N; i++) { in[2 * i] = sinf(0.05f * (float)i); in[2 * i + 1] = cosf(0.03f * (float)i); }
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *F = malloc(pc_rsq_size()), *q;
    int used, n1, n2;
    CHECK_EQ_INT(pc_rsq_init(F, 44100, 48000), 0);
    n2 = pc_rsq_process(F, in, N, o2, 600, &used);
    /* pc_rsq_reset itself */
    CHECK_EQ_INT(pc_rsq_init(A, 44100, 48000), 0);
    pc_rsq_process(A, in, N, o1, 600, &used);
    pc_rsq_process(A, in, 100, o1, 600, &used);
    pc_rsq_reset(A);
    n1 = pc_rsq_process(A, in, N, o1, 600, &used);
    CHECK_EQ_INT(n1, n2);
    CHECK(memcmp(o1, o2, sizeof(float) * 2 * (size_t)n1) == 0);
    /* through the state machine: a restart before a USE block resets the live table's history */
    pc_rsq_swap_t s;
    pc_rsq_pair_t p;
    pc_rsq_swap_setup(&s, A, B);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_WAIT);
    CHECK(build(&s, NONE, &p) != NULL);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    pc_rsq_process(q, in, N, o1, 600, &used);                          /* history now holds the tone */
    pc_rsq_swap_restart(&s);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);
    n1 = pc_rsq_process(q, in, N, o1, 600, &used);
    CHECK_EQ_INT(n1, n2);
    CHECK(memcmp(o1, o2, sizeof(float) * 2 * (size_t)n1) == 0);
    /* a pass-through block in between also starts the next resampled one fresh (no stale tail blended in) */
    pc_rsq_process(q, in, N, o1, 600, &used);
    CHECK_EQ_INT(blk(&s, 48000, 48000, &q), PC_RSQ_PASS);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);
    n1 = pc_rsq_process(q, in, N, o1, 600, &used);
    CHECK_EQ_INT(n1, n2);
    CHECK(memcmp(o1, o2, sizeof(float) * 2 * (size_t)n1) == 0);
    /* without a restart the history carries on (the state is not reset on every block) */
    n1 = pc_rsq_process(q, in, N, o1, 600, &used);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);
    n1 = pc_rsq_process(q, in, N, o1, 600, &used);
    CHECK(n1 != n2 || memcmp(o1, o2, sizeof(float) * 2 * (size_t)n1) != 0);
    free(in); free(o1); free(o2); free(A); free(B); free(F);
}

/* 10c: the two-slot cache. 44.1k-source and 48k-source (both on a 24k link here, so two resampling pairs) built once, then skipped back and forth:
 * every change is a pointer swap (no wait, no build), the swapped-in table starts from a silent history (same output as a fresh table), a third
 * pair evicts the older one, and swap_has says what is held (the sink start waits for it) */
static void test_cache_skip(void)
{
    enum { N = 441 };
    float *in = malloc(sizeof(float) * 2 * N), *o1 = malloc(sizeof(float) * 2 * 600), *o2 = malloc(sizeof(float) * 2 * 600);
    for (int i = 0; i < N; i++) { in[2 * i] = sinf(0.05f * (float)i); in[2 * i + 1] = cosf(0.03f * (float)i); }
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *F = malloc(pc_rsq_size()), *q, *qa, *qb;
    int used, n1, n2;
    pc_rsq_swap_t s;
    pc_rsq_pair_t p;
    CHECK_EQ_INT(pc_rsq_init(F, 44100, 24000), 0);
    n2 = pc_rsq_process(F, in, N, o2, 600, &used);
    pc_rsq_swap_setup(&s, A, B);
    CHECK(!pc_rsq_swap_has(&s, pr(44100, 24000)));
    CHECK(build(&s, pr(44100, 24000), &p) != NULL);
    CHECK(pc_rsq_swap_has(&s, pr(44100, 24000)));                      /* built before the first block: the sink start can wait for this */
    CHECK_EQ_INT(blk(&s, 44100, 24000, &qa), PC_RSQ_SWAP);             /* the first block: no wait */
    CHECK(build(&s, pr(48000, 24000), &p) != NULL);
    CHECK_EQ_INT(blk(&s, 48000, 24000, &qb), PC_RSQ_SWAP);
    int waits = 0, builds = 0;
    for (int i = 0; i < 6; i++) {
        uint32_t r = (i & 1) ? 48000 : 44100;                          /* manual rate skip 44.1k <-> 48k */
        pc_rsq_pick_t k = blk(&s, r, 24000, &q);
        waits += k == PC_RSQ_WAIT;
        CHECK(k == PC_RSQ_SWAP);
        CHECK(q == (r == 48000 ? qb : qa) || q == (r == 48000 ? qa : qb));
        pc_rsq_process(q, in, N, o1, 600, &used);                      /* leave a tone in the history */
        builds += pc_rsq_swap_spare(&s, pr(r, 24000), NONE, &p) != NULL;   /* the plan's pair is held: nothing to build, nothing evicted */
    }
    CHECK_EQ_INT(waits, 0);
    CHECK_EQ_INT(builds, 0);
    CHECK_EQ_INT(blk(&s, 44100, 24000, &q), PC_RSQ_SWAP);
    n1 = pc_rsq_process(q, in, N, o1, 600, &used);                     /* swapped back in after use: silent history, as a fresh table */
    CHECK_EQ_INT(n1, n2);
    CHECK(memcmp(o1, o2, sizeof(float) * 2 * (size_t)n1) == 0);
    CHECK(pc_rsq_swap_has(&s, pr(44100, 24000)) && pc_rsq_swap_has(&s, pr(48000, 24000)));
    CHECK_EQ_INT(blk(&s, 32000, 24000, &q), PC_RSQ_WAIT);              /* a third pair: waits, the older (held, not live) table is evicted */
    CHECK(pc_rsq_swap_spare(&s, NONE, NONE, &p) != NULL);
    CHECK_PAIR(p, 32000, 24000);
    CHECK(!pc_rsq_swap_has(&s, pr(48000, 24000)));
    free(in); free(o1); free(o2); free(A); free(B); free(F);
}
/* 10c fix round 1: (a) a reclaim decided before a swap never takes the table the swap just cached (it names the swap count too); (b) a full cache
 * (both blocks hold a pair) is "busy": the sink start must not wait for another pair (no block calls swap_take until a sink is set) */
static void test_reclaim_vs_swap_and_busy(void)
{
    pc_rsq_t *A = malloc(pc_rsq_size()), *B = malloc(pc_rsq_size()), *q;
    pc_rsq_swap_t s;
    pc_rsq_pair_t p, a = pr(44100, 48000), b = pr(32000, 48000), c = pr(22050, 48000);
    pc_rsq_swap_setup(&s, A, B);
    CHECK(build(&s, a, &p) != NULL);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    CHECK(build2(&s, a, b, &p) != NULL);                               /* b built in the free block */
    CHECK_EQ_INT(blk(&s, 32000, 48000, &q), PC_RSQ_SWAP);              /* b live, a cached (ready) */
    CHECK(pc_rsq_swap_spare_busy(&s));
    CHECK(pc_rsq_swap_has(&s, a) && pc_rsq_swap_has(&s, b) && !pc_rsq_swap_has(&s, c));   /* full cache, wrong pair c: the sink start skips the wait */
    /* the builder decided to reclaim (c wanted) ... */
    CHECK(pc_rsq_swap_spare(&s, b, c, &p) == NULL);
    /* ... but a swap back to a happens first: the old reclaim must not hand back the table that swap just cached */
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_SWAP);
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);
    CHECK_EQ_INT(s.ready, 1);                                          /* b stays cached */
    CHECK(pc_rsq_swap_has(&s, b));
    /* the audio task's reset then a builder's reclaim stored right after it (same gen) is stale too: it was decided from the old live */
    uint32_t old_tag = s.gen * 65536u + (s.swaps - 1u);
    s.reclaim = old_tag;
    CHECK_EQ_INT(blk(&s, 44100, 48000, &q), PC_RSQ_USE);
    CHECK_EQ_INT(s.ready, 1);
    free(A);
    free(B);
}
int main(void)
{
    RUN(test_pick);
    RUN(test_build_pick);
    RUN(test_swap_flow);
    RUN(test_wrong_spare);
    RUN(test_lookahead_join);
    RUN(test_spare_unwanted);
    RUN(test_wanted_spare_kept);
    RUN(test_stale_96k_spare);
    RUN(test_skip_after_lookahead);
    RUN(test_request_cleared);
    RUN(test_no_blocks);
    RUN(test_restart_and_reset);
    RUN(test_cache_skip);
    RUN(test_reclaim_vs_swap_and_busy);
    return 0;
}
