// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_volsel.h"
#include <string.h>
#include "pc/pc_volume.h"
#include "pc_test.h"

static const uint8_t A[6] = { 0x02, 0, 0, 0, 0, 0x01 }, B[6] = { 0x02, 0, 0, 0, 0, 0x02 };

static void test_defaults_and_clamp(void)
{
    CHECK_EQ_INT(PC_VOLSEL_BT_DEFAULT, 33);
    CHECK_EQ_INT(pc_volsel_wired(-1, 100), PC_VOL_DEFAULT);
    CHECK_EQ_INT(pc_volsel_wired(60, 100), 60);
    CHECK_EQ_INT(pc_volsel_wired(60, 50), 50);                /* volume_max clamps a stored value at read */
    CHECK_EQ_INT(pc_volsel_wired(-1, 20), 20);                /* and the default */
    CHECK_EQ_INT(pc_volsel_bt(NULL, A, 100), 33);
    CHECK_EQ_INT(pc_volsel_bt("", A, 100), 33);
    CHECK_EQ_INT(pc_volsel_bt("", A, 20), 20);
    CHECK_EQ_INT(pc_volsel_bt("020000000001:90", A, 60), 60);
    CHECK_EQ_INT(pc_volsel_bt("020000000001:90", NULL, 100), 33);
    CHECK_EQ_INT(pc_volsel_clamp(-5, 100), 0);
    CHECK_EQ_INT(pc_volsel_clamp(500, 900), 100);
    CHECK(pc_volsel_is_wired_value(PC_VOUT_WIRED));
    CHECK(pc_volsel_is_wired_value(PC_VOUT_CAST));            /* the broadcast keeps the wired value */
    CHECK(!pc_volsel_is_wired_value(PC_VOUT_BT));
    CHECK(!pc_volsel_is_wired_value(PC_VOUT_SPEAKER));       /* the jack in speaker mode has its own value */
    CHECK_EQ_INT(pc_volsel_spk(-1, 100), PC_VOLSEL_SPK_DEFAULT);
    CHECK_EQ_INT(PC_VOLSEL_SPK_DEFAULT, 60);
    CHECK_EQ_INT(pc_volsel_spk(90, 100), 90);
    CHECK_EQ_INT(pc_volsel_spk(90, 50), 50);                  /* volume_max clamps it at read */
    CHECK_EQ_INT(pc_volsel_spk(-1, 20), 20);                  /* and the default */
    char nm[20];
    CHECK_STR(pc_volsel_name(PC_VOUT_SPEAKER, NULL, nm, sizeof nm), "speaker");
}

static void test_store(void)
{
    char l[PC_VOLSEL_TEXT] = "";
    CHECK(pc_volsel_bt_put(l, sizeof l, A, 40));
    CHECK_STR(l, "020000000001:40");
    CHECK_EQ_INT(pc_volsel_bt(l, A, 100), 40);
    CHECK_EQ_INT(pc_volsel_bt(l, B, 100), 33);                /* another headset: the default */
    CHECK(pc_volsel_bt_put(l, sizeof l, B, 0));
    CHECK_STR(l, "020000000002:0,020000000001:40");
    CHECK_EQ_INT(pc_volsel_bt(l, B, 100), 0);                 /* a stored 0 is a value, not "absent" */
    CHECK(!pc_volsel_bt_put(l, sizeof l, B, 0));              /* nothing changed: no write */
    CHECK(pc_volsel_bt_touch(l, sizeof l, A));
    CHECK_STR(l, "020000000001:40,020000000002:0");
    CHECK(!pc_volsel_bt_touch(l, sizeof l, A));
    const uint8_t C[6] = { 9, 9, 9, 9, 9, 9 };
    CHECK(!pc_volsel_bt_touch(l, sizeof l, C));               /* a touch never adds */
    CHECK_EQ_INT(pc_volsel_bt_count(l), 2);
    CHECK(pc_volsel_bt_del(l, A));
    CHECK_STR(l, "020000000002:0");
    CHECK_EQ_INT(pc_volsel_bt(l, A, 100), 33);                /* forgotten: the default again */
    CHECK(!pc_volsel_bt_del(l, A));
    CHECK(pc_volsel_bt_del(l, B));
    CHECK_STR(l, "");
    CHECK(pc_volsel_bt_put(l, sizeof l, A, 250));
    CHECK_STR(l, "020000000001:100");
}

static void test_lru_bound(void)
{
    char l[PC_VOLSEL_TEXT] = "";
    for (int i = 0; i < 20; i++) {
        uint8_t d[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, (uint8_t)i };
        pc_volsel_bt_put(l, sizeof l, d, 100);                /* the longest entries */
        CHECK(strlen(l) < PC_VOLSEL_TEXT);
    }
    CHECK_EQ_INT(pc_volsel_bt_count(l), PC_VOLSEL_MAX);
    uint8_t first[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0 }, kept[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 5 }, last[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 19 };
    CHECK_EQ_INT(pc_volsel_bt(l, first, 100), 33);            /* the least recently used ones went */
    CHECK_EQ_INT(pc_volsel_bt(l, kept, 100), 100);
    CHECK_EQ_INT(pc_volsel_bt(l, last, 100), 100);
    pc_volsel_bt_touch(l, sizeof l, kept);                    /* used again: it outlives the next five */
    for (int i = 20; i < 30; i++) {
        uint8_t d[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, (uint8_t)i };
        pc_volsel_bt_put(l, sizeof l, d, 1);
    }
    CHECK_EQ_INT(pc_volsel_bt(l, kept, 100), 100);
    CHECK_EQ_INT(pc_volsel_bt_count(l), PC_VOLSEL_MAX);
    char small[40] = "";                                      /* a short buffer keeps what fits, newest first */
    pc_volsel_bt_put(small, sizeof small, A, 10);
    pc_volsel_bt_put(small, sizeof small, B, 20);
    pc_volsel_bt_put(small, sizeof small, kept, 30);
    CHECK_EQ_INT(pc_volsel_bt_count(small), 2);
    CHECK_EQ_INT(pc_volsel_bt(small, A, 100), 33);
}

static void test_broken_text(void)
{
    CHECK_EQ_INT(pc_volsel_bt("garbage", A, 100), 33);
    CHECK_EQ_INT(pc_volsel_bt("020000000001:999", A, 100), 33);       /* out of range: not an entry */
    CHECK_EQ_INT(pc_volsel_bt("02000000000:40", A, 100), 33);
    CHECK_EQ_INT(pc_volsel_bt("zz0000000001:40,020000000001:41", A, 100), 41);
    CHECK_EQ_INT(pc_volsel_bt("020000000001:41,020000000001:90", A, 100), 41);   /* a repeated address: the first */
    CHECK_EQ_INT(pc_volsel_bt("020000000001:4x", A, 100), 33);
    CHECK_EQ_INT(pc_volsel_bt_count(",,,"), 0);
    char l[PC_VOLSEL_TEXT] = "junk,020000000002:7";
    CHECK(pc_volsel_bt_put(l, sizeof l, A, 5));
    CHECK_STR(l, "020000000001:5,020000000002:7");            /* a rewrite drops the junk */
}

static void test_name(void)
{
    char b[24];
    CHECK_STR(pc_volsel_name(PC_VOUT_WIRED, NULL, b, sizeof b), "wired");
    CHECK_STR(pc_volsel_name(PC_VOUT_CAST, A, b, sizeof b), "cast");
    CHECK_STR(pc_volsel_name(PC_VOUT_BT, A, b, sizeof b), "bt:020000000001");
}

/* the rule the switch must keep: nothing in force across it is above the old sound or the new output's own value */
static void test_switch(void)
{
    for (int old_full = 0; old_full <= 100; old_full += 5)
        for (int new_full = 0; new_full <= 100; new_full += 5)
            for (int req = 0; req <= old_full; req += 5) {
                int next = pc_volsel_next(req, old_full, new_full);
                CHECK(next <= new_full);
                if (req == old_full) CHECK_EQ_INT(next, new_full);        /* nothing was holding it down: the new output's own value */
                else CHECK(next <= req || next == new_full);
                int lo = pc_volsel_across(req, next);
                CHECK(lo <= req && lo <= next && lo <= new_full);
            }
    CHECK_EQ_INT(pc_volsel_next(60, 60, 33), 33);             /* wired 60 -> a new headset */
    CHECK_EQ_INT(pc_volsel_next(33, 33, 60), 60);
    CHECK_EQ_INT(pc_volsel_next(12, 60, 33), 12);             /* an alarm's ramp at 12: stays 12 */
    CHECK_EQ_INT(pc_volsel_next(40, 60, 33), 33);
    CHECK_EQ_INT(pc_volsel_across(60, 33), 33);
    CHECK_EQ_INT(pc_volsel_across(33, 60), 33);
}

static void test_hold(void)
{
    pc_volhold_t h = { 0 };
    CHECK(!pc_volhold_on(&h));
    CHECK_EQ_INT(pc_volhold_apply(&h, 77), 77);               /* no hold: untouched */
    CHECK_EQ_INT(pc_volhold_req(&h, 77), 77);
    CHECK_EQ_INT(pc_volhold_end(&h), -1);
    pc_volhold_begin(&h, 33);
    CHECK(pc_volhold_on(&h));
    CHECK_EQ_INT(pc_volhold_req(&h, 33), 33);                 /* nothing asked yet */
    CHECK_EQ_INT(pc_volhold_end(&h), -1);                     /* ... so nothing to apply */
    pc_volhold_begin(&h, 33);
    CHECK_EQ_INT(pc_volhold_apply(&h, 60), 33);               /* kept back */
    CHECK_EQ_INT(pc_volhold_req(&h, 33), 60);
    CHECK_EQ_INT(pc_volhold_apply(&h, 10), 10);               /* lower always passes */
    CHECK_EQ_INT(pc_volhold_apply(&h, 62), 33);
    pc_volhold_begin(&h, 50);                                 /* a second begin never raises the ceiling */
    CHECK_EQ_INT(pc_volhold_apply(&h, 62), 33);
    pc_volhold_begin(&h, 20);
    CHECK_EQ_INT(pc_volhold_apply(&h, 62), 20);
    CHECK_EQ_INT(pc_volhold_end(&h), 62);                     /* the newest request */
    CHECK(!pc_volhold_on(&h));
    CHECK_EQ_INT(pc_volhold_apply(&h, 90), 90);
    pc_volhold_begin(&h, 0);                                  /* a ceiling of 0 is a hold */
    CHECK(pc_volhold_on(&h));
    CHECK_EQ_INT(pc_volhold_apply(&h, 5), 0);
    CHECK_EQ_INT(pc_volhold_end(&h), 5);
    for (int cap = 0; cap <= 100; cap += 7)
        for (int v = 0; v <= 100; v += 3) {
            pc_volhold_t g = { 0 };
            pc_volhold_begin(&g, cap);
            CHECK(pc_volhold_apply(&g, v) <= cap);
            CHECK(pc_volhold_apply(&g, v) <= v);
            CHECK_EQ_INT(pc_volhold_end(&g), v);
        }
}

int main(void)
{
    RUN(test_defaults_and_clamp);
    RUN(test_store);
    RUN(test_lru_bound);
    RUN(test_broken_text);
    RUN(test_name);
    RUN(test_switch);
    RUN(test_hold);
    return 0;
}
