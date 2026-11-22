// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extsend.h"
#include "pc_test.h"

static pc_extsend_t q;
static pc_extkeys_cfg_t cfg;
static pc_extkeys_res_t res;

/* drain with a 1 ms clock: record the time each event went out */
typedef struct { uint32_t t; pc_extsend_ev e; } rec_t;
static rec_t rec[600];
static int drain(uint32_t t0)
{
    int n = 0;
    for (uint32_t t = t0; t < t0 + 20000 && pc_extsend_count(&q) > 0; t++) {
        const pc_extsend_ev *e = pc_extsend_peek(&q, t);
        if (!e) continue;
        rec[n].t = t; rec[n].e = *e; n++;
        pc_extsend_pop(&q, t);
    }
    return n;
}
static const pc_extkeys_key_t *key(const char *src, int layer, int k)
{
    pc_extkeys_parse(src, strlen(src), &cfg, &res);
    CHECK(res.ok);
    return &cfg.layers[layer].keys[k];
}

static void test_default_keys(void)
{
    pc_extkeys_default(&cfg);
    pc_extsend_init(&q);
    CHECK(pc_extsend_key(&q, &cfg.layers[0].keys[0]));
    CHECK_EQ_INT(pc_extsend_count(&q), 2);
    int n = drain(100);
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_INT(rec[0].e.kind, PC_XEV_CONS); CHECK_EQ_INT(rec[0].e.code, 0xCD); CHECK_EQ_INT(rec[0].t, 100);
    CHECK_EQ_INT(rec[1].e.kind, PC_XEV_CONS); CHECK_EQ_INT(rec[1].e.code, 0);
    CHECK_EQ_INT(rec[1].t, 100 + PC_EXTSEND_HOLD_MS);          /* the release waits the hold time */
    CHECK_EQ_INT(pc_extsend_count(&q), 0);
    CHECK(pc_extsend_peek(&q, 1000) == NULL);
}
static void test_combo(void)
{
    pc_extsend_init(&q);
    CHECK(pc_extsend_key(&q, key("layer a\nkey 複製 combo ctrl+c\n", 0, 0)));
    int n = drain(0);
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_INT(rec[0].e.kind, PC_XEV_KBD); CHECK_EQ_INT(rec[0].e.mod, 0x01); CHECK_EQ_INT(rec[0].e.code, 0x06);
    CHECK_EQ_INT(rec[1].e.mod, 0); CHECK_EQ_INT(rec[1].e.code, 0);
}
static void test_text(void)
{
    pc_extsend_init(&q);
    CHECK(pc_extsend_key(&q, key("layer a\nkey 簽名 text \"Hi\\n\"\n", 0, 0)));   /* H (shift+h), i, Enter */
    int n = drain(0);
    CHECK_EQ_INT(n, 6);
    CHECK_EQ_INT(rec[0].e.mod, 0x02); CHECK_EQ_INT(rec[0].e.code, 0x0B);
    CHECK_EQ_INT(rec[2].e.mod, 0); CHECK_EQ_INT(rec[2].e.code, 0x0C);
    CHECK_EQ_INT(rec[4].e.code, 0x28);
    CHECK_EQ_INT(rec[5].t, 5 * PC_EXTSEND_HOLD_MS);   /* 3 characters, 6 events 12 ms apart: the last goes out at 60 ms */
}
static void test_text_double_letter(void)
{
    pc_extsend_init(&q);
    CHECK(pc_extsend_key(&q, key("layer a\nkey ll text \"ll\"\n", 0, 0)));
    CHECK_EQ_INT(drain(0), 4);                                 /* l down, l up, l down, l up: never two downs in a row */
    CHECK_EQ_INT(rec[1].e.code, 0); CHECK_EQ_INT(rec[2].e.code, 0x0F);
}
static void test_macro_timing(void)
{
    pc_extsend_init(&q);
    CHECK(pc_extsend_key(&q, key("layer a\nkey 巨集 macro ctrl+a, wait 50, ctrl+c\n", 0, 0)));
    int n = drain(1000);
    CHECK_EQ_INT(n, 5);                                        /* ctrl+a down, up, WAIT, ctrl+c down, up */
    CHECK_EQ_INT(rec[2].e.kind, PC_XEV_WAIT);
    CHECK_EQ_INT(rec[0].t, 1000);
    CHECK_EQ_INT(rec[2].t, 1000 + PC_EXTSEND_HOLD_MS + PC_EXTSEND_GAP_MS);
    CHECK_EQ_INT(rec[3].t, rec[2].t + 50);                     /* the wait: ctrl+c goes out exactly 50 ms after the WAIT event */
    CHECK_EQ_INT(rec[3].e.code, 0x06); CHECK_EQ_INT(rec[3].e.mod, 0x01);
    CHECK_EQ_INT(rec[4].t, rec[3].t + PC_EXTSEND_HOLD_MS);
}
static void test_knob(void)
{
    pc_extsend_init(&q);
    CHECK_EQ_INT(pc_extsend_knob(&q, PC_KNOB_VOLUME, 2), 2);
    CHECK_EQ_INT(drain(0), 4);
    CHECK_EQ_INT(rec[0].e.kind, PC_XEV_CONS); CHECK_EQ_INT(rec[0].e.code, 0xE9);
    CHECK_EQ_INT(pc_extsend_knob(&q, PC_KNOB_VOLUME, -1), 1);
    drain(0);
    CHECK_EQ_INT(rec[0].e.code, 0xEA);
    CHECK_EQ_INT(pc_extsend_knob(&q, PC_KNOB_SCROLL, 1), 1);
    drain(0);
    CHECK_EQ_INT(rec[0].e.kind, PC_XEV_WHEEL); CHECK_EQ_INT((int16_t)rec[0].e.code, -1);     /* forward = one notch down, no release event */
    CHECK_EQ_INT(pc_extsend_count(&q), 0);
    CHECK_EQ_INT(pc_extsend_knob(&q, PC_KNOB_SCROLL, -3), 3);
    CHECK_EQ_INT(drain(0), 3); CHECK_EQ_INT((int16_t)rec[2].e.code, 1); CHECK_EQ_INT(rec[2].t - rec[0].t, 2 * PC_EXTSEND_GAP_MS);
    CHECK_EQ_INT(pc_extsend_knob(&q, PC_KNOB_ARROWS, -1), 1);
    drain(0);
    CHECK_EQ_INT(rec[0].e.code, 0x50);
    CHECK_EQ_INT(pc_extsend_knob(&q, PC_KNOB_NONE, 3), 0);
    CHECK_EQ_INT(pc_extsend_count(&q), 0);
    CHECK_EQ_INT(pc_extsend_knob(&q, PC_KNOB_VOLUME, 100), PC_EXTSEND_KNOB_MAX);   /* a burst is cut */
    pc_extsend_flush(&q);
    CHECK_EQ_INT(pc_extsend_count(&q), 0);
}
static void test_full_refused_whole(void)
{
    pc_extsend_init(&q);
    pc_extkeys_default(&cfg);
    static pc_extkeys_key_t big;
    memset(&big, 0, sizeof big);
    big.act = PC_XACT_TEXT; big.n = 64;
    memset(big.u.text, 'a', 64);
    int ok = 0;
    while (pc_extsend_key(&q, &big)) ok++;
    CHECK_EQ_INT(ok, 1);                                       /* 128 events each: 255 free, the second does not fit */
    CHECK_EQ_INT(pc_extsend_count(&q), 128);                   /* nothing of the refused one is in the queue */
    CHECK_EQ_INT(q.drops, 1);
    CHECK(pc_extsend_key(&q, &cfg.layers[0].keys[0]));         /* a short one still fits */
    CHECK_EQ_INT(pc_extsend_count(&q), 130);
    CHECK_EQ_INT(drain(0), 130);
    for (int i = 0; i < 300; i++) CHECK(pc_extsend_key(&q, &cfg.layers[0].keys[0]) && drain(0) == 2);   /* the indices wrap past CAP */
}
static void test_not_due(void)
{
    pc_extsend_init(&q);
    pc_extkeys_default(&cfg);
    CHECK(pc_extsend_key(&q, &cfg.layers[0].keys[0]));
    CHECK(pc_extsend_peek(&q, 5) != NULL);
    pc_extsend_pop(&q, 5);
    CHECK(pc_extsend_peek(&q, 5 + PC_EXTSEND_HOLD_MS - 1) == NULL);
    CHECK(pc_extsend_peek(&q, 5 + PC_EXTSEND_HOLD_MS) != NULL);
    CHECK(pc_extsend_peek(&q, 5 + PC_EXTSEND_HOLD_MS)->code == 0);
}
static void test_bad_keys(void)
{
    pc_extsend_init(&q);
    pc_extkeys_key_t k;
    memset(&k, 0, sizeof k);
    k.act = PC_XACT_CONSUMER;
    CHECK(!pc_extsend_key(&q, &k));                            /* consumer 0 is not a key */
    k.act = PC_XACT_COMBO;
    CHECK(!pc_extsend_key(&q, &k));
    k.act = 99;
    CHECK(!pc_extsend_key(&q, &k));
    CHECK_EQ_INT(pc_extsend_count(&q), 0);
    k.act = PC_XACT_COMBO; k.mod = 0x08;                       /* modifiers only (a held Win key press) is a key */
    CHECK(pc_extsend_key(&q, &k));
}

static void test_text_api(void)
{
    pc_extsend_init(&q);
    CHECK(pc_extsend_text(&q, "a.b/\n"));
    CHECK_EQ_INT(drain(0), 10);
    CHECK_EQ_INT(rec[2].e.code, 0x37); CHECK_EQ_INT(rec[6].e.code, 0x38);
    CHECK(!pc_extsend_text(&q, ""));
    char big[200];
    memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = 0;
    CHECK(!pc_extsend_text(&q, big));                          /* 199 characters = 398 events: refused whole */
    CHECK_EQ_INT(pc_extsend_count(&q), 0);
}

/* the pump: a link that is not ready for 100 ms drops a press (and what follows up to the next release) but never a release; a flush with the link up sends all-up first */
static int g_ready_at, g_now;
static pc_extsend_ev g_sent[64];
static int g_nsent;
static bool sink_gate(const pc_extsend_ev *e)
{
    if (g_now < g_ready_at) return false;
    g_sent[g_nsent++] = *e;
    return true;
}
static void run_pump(int from, int to) { for (g_now = from; g_now <= to; g_now++) pc_extsend_pump(&q, (uint32_t)g_now, sink_gate); }
static void test_pump_never_drops_release(void)
{
    pc_extsend_init(&q);
    pc_extkeys_default(&cfg);
    g_nsent = 0; g_ready_at = 0;
    CHECK(pc_extsend_key(&q, &cfg.layers[0].keys[0]));       /* press 0xCD, release */
    run_pump(0, 0);                                           /* the press went out */
    CHECK_EQ_INT(g_nsent, 1);
    g_ready_at = 1000;                                        /* the link stalls with the release at the head */
    run_pump(1, 600);
    CHECK_EQ_INT(g_nsent, 1);
    CHECK_EQ_INT(pc_extsend_count(&q), 1);                    /* the release is still queued after 600 ms of refusals */
    g_ready_at = 0;
    run_pump(601, 700);
    CHECK_EQ_INT(g_nsent, 2); CHECK_EQ_INT(g_sent[1].kind, PC_XEV_CONS); CHECK_EQ_INT(g_sent[1].code, 0);
    /* a press that cannot go out is dropped with its text up to the next release, the release then goes */
    pc_extsend_init(&q);
    g_nsent = 0; g_ready_at = 100000;
    CHECK(pc_extsend_text(&q, "ab"));                        /* a down, a up, b down, b up */
    run_pump(0, 99);
    CHECK_EQ_INT(pc_extsend_count(&q), 4);
    run_pump(100, 100);                                      /* stall limit: the press 'a' is skipped, the head is the release */
    CHECK_EQ_INT(pc_extsend_count(&q), 3);
    CHECK_EQ_INT(g_nsent, 0);
    g_ready_at = 0;
    run_pump(101, 400);
    CHECK_EQ_INT(g_nsent, 3);                                /* a-release, b-press, b-release */
    CHECK_EQ_INT(g_sent[0].code, 0); CHECK_EQ_INT(g_sent[1].code, 0x05); CHECK_EQ_INT(g_sent[2].code, 0);
}
static void test_flush_up(void)
{
    pc_extsend_init(&q);
    pc_extkeys_default(&cfg);
    g_nsent = 0; g_ready_at = 0; g_now = 0;
    CHECK(pc_extsend_key(&q, &cfg.layers[0].keys[0]));
    pc_extsend_flush_up(&q, sink_gate);
    CHECK_EQ_INT(pc_extsend_count(&q), 0);
    CHECK_EQ_INT(g_nsent, 2);                                /* all-up keyboard + consumer release, whatever was queued */
    CHECK_EQ_INT(g_sent[0].kind, PC_XEV_KBD); CHECK_EQ_INT(g_sent[0].mod, 0); CHECK_EQ_INT(g_sent[0].code, 0);
    CHECK_EQ_INT(g_sent[1].kind, PC_XEV_CONS); CHECK_EQ_INT(g_sent[1].code, 0);
    pc_extsend_flush_up(&q, NULL);                           /* link down: just the flush */
    CHECK_EQ_INT(g_nsent, 2);
}

int main(void)
{
    RUN(test_default_keys); RUN(test_combo); RUN(test_text); RUN(test_text_double_letter); RUN(test_macro_timing);
    RUN(test_knob); RUN(test_full_refused_whole); RUN(test_not_due); RUN(test_bad_keys); RUN(test_text_api); RUN(test_pump_never_drops_release); RUN(test_flush_up);
    return 0;
}
