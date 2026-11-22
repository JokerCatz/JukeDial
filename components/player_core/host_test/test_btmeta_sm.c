// SPDX-License-Identifier: LGPL-2.1-or-later
/* the speaker mode's AVRCP controller core (pc_bma_*, fix round 1): events in, actions out */
#include <stdlib.h>
#include <string.h>
#include "pc/pc_btmeta.h"
#include "pc_test.h"

static int s_live;                                  /* buffers handed out and not given back */
static bool s_grow_fails;                           /* the next grows fail (no memory) */
static void *grow(void *p, size_t n) { if (s_grow_fails) return NULL; if (!p) s_live++; return realloc(p, n); }
static void drop(void *p) { if (p) { s_live--; free(p); } }

static pc_bma_t S;
static pc_bma_out_t O;
static uint32_t T;                                  /* the clock */
static pc_btmeta_t M;                               /* the last published track */
static int s_meta_n, s_art_n;
static char s_art_hdl[8];
static uint32_t s_art_len;

static void ev(uint8_t kind, bool on, uint8_t id, const void *data, int len, bool final, int status)
{
    pc_bma_ev_t e = { .kind = kind, .on = on, .id = id, .data = data, .len = len, .final = final, .status = status, .now_ms = T };
    pc_bma_event(&S, &e, &O);
    if (O.act & PC_BMA_DO_META) { M = *O.meta; s_meta_n++; }
    if (O.act & PC_BMA_DO_ART) { s_art_n++; strcpy(s_art_hdl, O.art_hdl); s_art_len = O.art_len; drop(O.art); }   /* the receiver owns it */
}
static void link(bool on) { ev(PC_BMA_LINK, on, 0, NULL, 0, false, 0); }
static void feat(bool on) { ev(PC_BMA_FEAT, on, 0, NULL, 0, false, 0); }
static void caps(bool on) { ev(PC_BMA_CAPS, on, 0, NULL, 0, false, 0); }
static void track(void) { ev(PC_BMA_TRACK, false, 0, NULL, 0, false, 0); }
static void attr(uint8_t id, const char *t) { ev(PC_BMA_ATTR, false, id, t, (int)strlen(t), false, 0); }
static void ca(bool on) { ev(PC_BMA_CA, on, 0, NULL, 0, false, 0); }
static void data(int n, bool final) { static uint8_t b[2000]; memset(b, 0x5A, sizeof b); ev(PC_BMA_DATA, false, 0, b, n, final, 0); }
static void tick(uint32_t ms) { T += ms; ev(PC_BMA_TICK, false, 0, NULL, 0, false, 0); }
static void answer(const char *title, const char *hdl) { attr(PC_BTMETA_TITLE, title); attr(PC_BTMETA_ARTIST, "Band"); if (hdl) attr(PC_BTMETA_ART, hdl); tick(PC_BMA_SETTLE_MS + 10); }

static void start(void)                             /* a phone with cover art: link, features, caps, the cover-art link up, the first answer */
{
    if (S.grow) pc_bma_free(&S);
    pc_bma_init(&S, grow, drop);
    memset(&M, 0, sizeof M);
    s_meta_n = s_art_n = 0; s_art_hdl[0] = 0; T = 1000;
    link(true);
    CHECK(O.act & PC_BMA_DO_CAPS);
    feat(true);
    CHECK(O.act & PC_BMA_DO_CA_CONNECT);
    caps(true);
    CHECK((O.act & (PC_BMA_DO_REG | PC_BMA_DO_ASK)) == (PC_BMA_DO_REG | PC_BMA_DO_ASK));
    CHECK_EQ_INT(O.ask_mask, PC_BMA_META_MASK | PC_BTMETA_ART);
    answer("Alpha", NULL);                          /* before the cover-art link: no handle */
    CHECK_STR(M.title, "Alpha");
    ca(true);
    CHECK(O.act & PC_BMA_DO_ASK);                   /* the handles come once the cover-art link is up */
}

static void test_flow_and_cover(void)
{
    start();
    attr(PC_BTMETA_TITLE, "Alpha"); attr(PC_BTMETA_ART, "0000001");
    CHECK(!(O.act & PC_BMA_DO_GET));                /* not before the answer has settled */
    tick(PC_BMA_SETTLE_MS + 10);
    CHECK(O.act & PC_BMA_DO_GET);
    CHECK_STR(O.get, "0000001");
    CHECK_STR(S.art_state, "asking");
    data(1000, false); data(500, true);
    CHECK_EQ_INT(s_art_n, 1);
    CHECK_STR(s_art_hdl, "0000001");
    CHECK_EQ_INT(s_art_len, 1500);
    CHECK_STR(S.art_state, "ok");
    tick(100);
    CHECK(!(O.act & PC_BMA_DO_GET));                /* the same handle is not fetched again */
    CHECK_EQ_INT(s_live, 0);
}

static void test_late_thumbnail_after_newer_track(void)
{
    start();
    answer("Alpha", "0000001");
    CHECK(O.act & PC_BMA_DO_GET);
    data(800, false);
    track();                                        /* the next track: the title shows the wait mark at once */
    CHECK_STR(M.title, PC_BTMETA_WAIT);
    CHECK_STR(M.artist, "");
    CHECK(O.act & PC_BMA_DO_ASK);
    answer("Beta", "0000002");
    CHECK_STR(M.title, "Beta");
    CHECK(!(O.act & PC_BMA_DO_GET));                /* one transfer at a time: Alpha's still runs */
    data(800, true);                                /* Alpha's thumbnail ends: dropped, never Beta's cover */
    CHECK_EQ_INT(s_art_n, 0);
    CHECK(O.act & PC_BMA_DO_GET);
    CHECK_STR(O.get, "0000002");
    data(300, true);
    CHECK_EQ_INT(s_art_n, 1);
    CHECK_STR(s_art_hdl, "0000002");
    CHECK_EQ_INT(s_art_len, 300);
    CHECK_EQ_INT(s_live, 0);
}

static void test_two_handles_quickly(void)
{
    start();
    answer("Alpha", "0000001");
    CHECK_STR(O.get, "0000001");
    track(); answer("Beta", "0000002");
    track(); answer("Gamma", "0000003");
    CHECK(!(O.act & PC_BMA_DO_GET));
    data(10, true);                                 /* the first one ends (stale): straight to the newest */
    CHECK(O.act & PC_BMA_DO_GET);
    CHECK_STR(O.get, "0000003");
    CHECK_EQ_INT(s_art_n, 0);
    data(10, true);
    CHECK_STR(s_art_hdl, "0000003");
    CHECK_EQ_INT(s_live, 0);
}

static void test_timeout(void)
{
    start();
    answer("Alpha", "0000001");
    data(100, false);
    tick(PC_BMA_ART_WAIT_MS);                       /* 15 s: this GET is given up, the link is cycled */
    CHECK(O.act & PC_BMA_DO_FAIL);
    CHECK_STR(O.fail, "timeout");
    CHECK(O.act & PC_BMA_DO_CA_DROP);
    CHECK_EQ_INT(s_live, 0);                        /* the partial buffer went */
    data(100, true);                                /* the old transfer's end comes late: dropped */
    CHECK_EQ_INT(s_art_n, 0);
    CHECK_EQ_INT(s_live, 0);
    track(); answer("Beta", "0000002");             /* no GET on a link that may still carry the old transfer */
    CHECK(!(O.act & PC_BMA_DO_GET));
    ca(false);                                      /* the drop is done: a new track may connect again (once) */
    CHECK(O.act & PC_BMA_DO_CA_CONNECT);
    ca(true);
    answer("Beta", "0000002");
    CHECK(O.act & PC_BMA_DO_GET);
    CHECK_STR(O.get, "0000002");
}

static void test_disconnect_mid_transfer(void)
{
    start();
    answer("Alpha", "0000001");
    data(1000, false);
    CHECK_EQ_INT(s_live, 1);
    ca(false);                                      /* IDF closes the cover-art link after a failed GET too */
    CHECK(O.act & PC_BMA_DO_FAIL);
    CHECK_STR(O.fail, "disconnect");
    CHECK(O.act & PC_BMA_DO_ART_CLEAR);
    CHECK_EQ_INT(s_live, 0);
    CHECK(!(O.act & PC_BMA_DO_CA_CONNECT));         /* not again on the same track */
    data(10, true);
    CHECK_EQ_INT(s_art_n, 0);
    track();
    CHECK(O.act & PC_BMA_DO_CA_CONNECT);            /* the next track reconnects once */
}

static void test_link_change_mid_transfer(void)
{
    start();
    answer("Alpha", "0000001");
    data(1000, false);
    link(false);                                    /* the phone goes: everything of it goes */
    CHECK(O.act & PC_BMA_DO_META);
    CHECK_STR(M.title, "");
    CHECK(O.act & PC_BMA_DO_ART_CLEAR);
    CHECK_EQ_INT(s_live, 0);
    link(true);                                     /* another phone */
    CHECK(O.act & PC_BMA_DO_CAPS);
    data(10, true);                                 /* the old phone's last piece: nothing */
    CHECK_EQ_INT(s_art_n, 0);
    CHECK_STR(M.title, "");
    feat(false); caps(false);
    CHECK_EQ_INT(O.ask_mask, PC_BMA_META_MASK);     /* no cover art: no handle asked */
    answer("Delta", NULL);
    CHECK_STR(M.title, "Delta");
    CHECK_STR(S.art_state, "no_ca");
    CHECK_EQ_INT(s_live, 0);
}

static void test_old_answer_dropped(void)
{
    start();
    tick(PC_BMA_ASK_FORGET_MS);                     /* the ask of the cover-art link was never answered: forgotten */
    track();                                        /* asks for the new track */
    track();                                        /* and again: two asks out */
    answer("Old", NULL);                            /* the first answer describes an older state: dropped */
    CHECK_STR(M.title, PC_BTMETA_WAIT);
    answer("New", NULL);
    CHECK_STR(M.title, "New");
    tick(PC_BMA_ASK_FORGET_MS);
    answer("Unasked", NULL);                        /* nothing out: an answer is taken as it is */
    CHECK_STR(M.title, "Unasked");
}

static void test_answers_back_to_back(void)
{
    start();
    tick(PC_BMA_ASK_FORGET_MS);
    track(); track();                               /* two asks out; their answers come without a pause between them */
    attr(PC_BTMETA_TITLE, "Old"); attr(PC_BTMETA_ARTIST, "x");
    attr(PC_BTMETA_TITLE, "New");                   /* a repeated attribute starts the next answer: the first one is complete (and older: dropped) */
    CHECK_STR(M.title, PC_BTMETA_WAIT);
    attr(PC_BTMETA_ARTIST, "y");
    tick(PC_BMA_SETTLE_MS + 10);
    CHECK_STR(M.title, "New");
    CHECK_STR(M.artist, "y");
}

static void test_ask_forgotten(void)
{
    start();
    tick(PC_BMA_ASK_FORGET_MS);                     /* the cover-art link's ask: never answered */
    track();
    tick(PC_BMA_ASK_FORGET_MS);                     /* nor this one (asked once more: fix round 3) */
    tick(PC_BMA_ASK_FORGET_MS);                     /* nor that */
    track();
    answer("Late", NULL);                           /* one ask out again: taken */
    CHECK_STR(M.title, "Late");
}

static void test_lost_and_errors(void)
{
    start();
    answer("Alpha", "0000001");
    data(100, false);
    ev(PC_BMA_LOST, false, 0, NULL, 0, false, 0);  /* lost pieces: the picture would be broken */
    CHECK(O.act & PC_BMA_DO_FAIL);
    CHECK_STR(O.fail, "lost");
    CHECK(O.act & PC_BMA_DO_ASK);
    CHECK_EQ_INT(s_live, 0);
    CHECK(pc_bma_busy(&S));                         /* fix round 3: the phone still streams Alpha (no abort in IDF): the GET stays in flight */
    answer("Alpha", "0000001");                     /* the answer to that ask (the same handle failed: not asked again) */
    CHECK(!(O.act & PC_BMA_DO_GET));
    track(); answer("Beta", "0000002");
    CHECK(!(O.act & PC_BMA_DO_GET));                /* never a GET over a streaming one */
    data(100, false);                               /* Alpha's next pieces: not collected */
    CHECK(!(O.act & PC_BMA_DO_GET));
    CHECK_EQ_INT(s_live, 0);
    data(100, true);                                /* Alpha's own end: dropped without a second word, then Beta is asked */
    CHECK_EQ_INT(s_art_n, 0);
    CHECK(!(O.act & PC_BMA_DO_FAIL));
    CHECK(O.act & PC_BMA_DO_GET);
    CHECK_STR(O.get, "0000002");
    ev(PC_BMA_DATA, false, 0, NULL, 0, true, 5);   /* a GET that failed */
    CHECK_STR(O.fail, "status");
    CHECK_STR(S.art_state, "fail");
    track(); answer("Gamma", "0000003");
    ca(false);                                      /* IDF closes the link after a failed GET; a later track than the last attempt: connect again */
    CHECK(O.act & PC_BMA_DO_CA_CONNECT);
    ca(true); answer("Gamma", "0000003");
    CHECK(O.act & PC_BMA_DO_GET);
    for (int i = 0; i < 140; i++) data(2000, false);   /* 280 KB: more than a thumbnail */
    data(10, true);
    CHECK_STR(O.fail, "too_big");
    CHECK_EQ_INT(s_art_n, 0);
    CHECK_EQ_INT(s_live, 0);
    track(); answer("Eps", "0000004");
    data(0, true);
    CHECK_STR(O.fail, "empty");
}

static void test_no_mem_keeps_transfer(void)      /* fix round 3 (re-review I-1): our end is not the phone's end */
{
    start();
    answer("Alpha", "0000001");
    data(100, false);
    s_grow_fails = true;
    data(100000, false);                            /* the buffer cannot grow */
    s_grow_fails = false;
    CHECK(O.act & PC_BMA_DO_FAIL);
    CHECK_STR(O.fail, "no_mem");
    CHECK_EQ_INT(s_live, 0);
    track(); answer("Beta", "0000002");
    CHECK(!(O.act & PC_BMA_DO_GET));
    data(500, false); data(500, true);              /* the rest of Alpha's transfer */
    CHECK_EQ_INT(s_art_n, 0);
    CHECK(O.act & PC_BMA_DO_GET);
    CHECK_STR(O.get, "0000002");
    data(300, true);
    CHECK_EQ_INT(s_art_n, 1);
    CHECK_STR(s_art_hdl, "0000002");
    CHECK_EQ_INT(s_art_len, 300);                   /* nothing of Alpha in Beta's picture */
    CHECK_EQ_INT(s_live, 0);
}

static void test_early_end_then_timeout(void)     /* an early end whose transfer never ends: the timeout cycles the link (no second fail) */
{
    start();
    answer("Alpha", "0000001");
    data(100, false);
    ev(PC_BMA_LOST, false, 0, NULL, 0, false, 0);
    CHECK_STR(O.fail, "lost");
    tick(PC_BMA_ART_WAIT_MS);
    CHECK(O.act & PC_BMA_DO_CA_DROP);
    CHECK(!(O.act & PC_BMA_DO_FAIL));
    CHECK(!pc_bma_busy(&S) || S.asks_out);          /* the GET is over (an ask may still be out) */
    CHECK(!S.getting);
}

static void test_stale_timeout(void)              /* a stale GET that never ends: the link is cycled, nothing published, no fail for the new track */
{
    start();
    answer("Alpha", "0000001");
    track(); answer("Beta", "0000002");
    CHECK(!(O.act & PC_BMA_DO_GET));
    tick(PC_BMA_ART_WAIT_MS);
    CHECK(O.act & PC_BMA_DO_CA_DROP);
    CHECK(!(O.act & PC_BMA_DO_FAIL));
    CHECK_EQ_INT(s_art_n, 0);
    ca(false);
    CHECK(O.act & PC_BMA_DO_CA_CONNECT);            /* Beta's track had no attempt yet */
}

static void test_connect_timeout(void)            /* re-review M3: a cover-art connect that never answers ends after the wait */
{
    pc_bma_init(&S, grow, drop);
    T = 0;
    link(true); feat(true);
    CHECK(O.act & PC_BMA_DO_CA_CONNECT);
    CHECK(pc_bma_busy(&S));                         /* the clock runs while connecting */
    tick(PC_BMA_ART_WAIT_MS);
    CHECK_EQ_INT(S.ca, PC_BMA_CA_DOWN);
    CHECK_STR(S.art_state, "no_ca");
    CHECK(!pc_bma_busy(&S));
    caps(false); answer("A", NULL);
    CHECK(!(O.act & PC_BMA_DO_CA_CONNECT));         /* not again on this track */
    track();
    CHECK(O.act & PC_BMA_DO_CA_CONNECT);            /* the next track tries once more */
    pc_bma_free(&S);
}

static void test_reask_after_lost_answer(void)    /* re-review M5: two asks out, one answer (dropped), the other never comes: asked once more */
{
    start();
    tick(PC_BMA_ASK_FORGET_MS);
    track(); track();
    answer("Old", NULL);
    CHECK_STR(M.title, PC_BTMETA_WAIT);
    tick(PC_BMA_ASK_FORGET_MS);                     /* the second answer never came */
    CHECK(O.act & PC_BMA_DO_ASK);
    answer("Now", NULL);
    CHECK_STR(M.title, "Now");
    track();
    tick(PC_BMA_ASK_FORGET_MS);                     /* no answer at all: one more ask */
    CHECK(O.act & PC_BMA_DO_ASK);
    tick(PC_BMA_ASK_FORGET_MS);                     /* and then no more on this track */
    CHECK(!(O.act & PC_BMA_DO_ASK));
    CHECK(!pc_bma_busy(&S));
}

static void test_busy_only_while_open(void)   /* fix round 2: the driver's tick runs only while this is true */
{
    pc_bma_init(&S, grow, drop);
    T = 0;
    CHECK(!pc_bma_busy(&S));
    link(true); feat(false);
    CHECK(!pc_bma_busy(&S));                        /* linked, nothing asked: idle */
    feat(true);
    CHECK(O.act & PC_BMA_DO_CA_CONNECT);
    CHECK(pc_bma_busy(&S));                         /* the cover-art connect is open (fix round 3: it has a wait) */
    ca(true);
    CHECK(pc_bma_busy(&S));                         /* an ask is out */
    attr(PC_BTMETA_TITLE, "A");
    CHECK(pc_bma_busy(&S));                         /* the answer window */
    tick(PC_BMA_SETTLE_MS + 10);
    CHECK(!pc_bma_busy(&S));                        /* answered: idle-linked */
    caps(true);
    CHECK(pc_bma_busy(&S));
    answer("A", "0000001");
    CHECK(O.act & PC_BMA_DO_GET);
    CHECK(pc_bma_busy(&S));                         /* the download */
    data(10, true);
    CHECK(!pc_bma_busy(&S));
    track();
    tick(PC_BMA_ASK_FORGET_MS);                     /* never answered: asked once more (fix round 3) */
    CHECK(pc_bma_busy(&S));
    tick(PC_BMA_ASK_FORGET_MS);                     /* forgotten: idle again */
    CHECK(!pc_bma_busy(&S));
    pc_bma_free(&S);
}

static void test_no_title_no_wait(void)
{
    pc_bma_init(&S, grow, drop);
    memset(&M, 0, sizeof M);
    T = 0;
    link(true); feat(false); caps(true);
    track();                                        /* nothing was known: no wait mark (the page keeps the phone's name) */
    CHECK_STR(M.title, "");
    answer("", NULL);
    CHECK_STR(M.title, "");
    pc_bma_free(&S);
}

int main(void)
{
    RUN(test_flow_and_cover);
    RUN(test_late_thumbnail_after_newer_track);
    RUN(test_two_handles_quickly);
    RUN(test_timeout);
    RUN(test_disconnect_mid_transfer);
    RUN(test_link_change_mid_transfer);
    RUN(test_old_answer_dropped);
    RUN(test_answers_back_to_back);
    RUN(test_ask_forgotten);
    RUN(test_lost_and_errors);
    RUN(test_no_mem_keeps_transfer);
    RUN(test_early_end_then_timeout);
    RUN(test_stale_timeout);
    RUN(test_connect_timeout);
    RUN(test_reask_after_lost_answer);
    RUN(test_busy_only_while_open);
    RUN(test_no_title_no_wait);
    pc_bma_free(&S);
    CHECK_EQ_INT(s_live, 0);
    return 0;
}
