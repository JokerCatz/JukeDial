// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_plug.h"
#include "pc_test.h"

static int ticks_until_ask(pc_plug_t *p, bool pw, bool dbg, int max)
{
    for (int i = 1; i <= max; i++) if (pc_plug_tick(p, pw, dbg)) return i;
    return 0;
}

static void test_otg_side_asks_once(void)
{
    pc_plug_t p = { 0 };
    CHECK_EQ_INT(ticks_until_ask(&p, true, false, 10), PC_PLUG_ASK_AFTER);
    CHECK_EQ_INT(ticks_until_ask(&p, true, false, 100), 0);         /* answered: not again while it stays in */
}

static void test_debug_side_never_asks(void)
{
    pc_plug_t p = { 0 };
    CHECK_EQ_INT(ticks_until_ask(&p, true, true, 100), 0);
}

static void test_battery_never_asks(void)
{
    pc_plug_t p = { 0 };
    CHECK_EQ_INT(ticks_until_ask(&p, false, false, 100), 0);
}

static void test_replug_asks_again(void)
{
    pc_plug_t p = { 0 };
    ticks_until_ask(&p, true, false, 10);
    pc_plug_tick(&p, false, false);                                  /* unplugged */
    CHECK_EQ_INT(ticks_until_ask(&p, true, false, 10), PC_PLUG_ASK_AFTER);
}

static void test_flip_from_debug_side_asks(void)
{
    pc_plug_t p = { 0 };
    ticks_until_ask(&p, true, false, 10);                            /* asked, said no */
    pc_plug_tick(&p, true, true);                                    /* turned to the debug side (power never dropped) */
    CHECK_EQ_INT(ticks_until_ask(&p, true, false, 10), PC_PLUG_ASK_AFTER);   /* and back */
}

static void test_short_gap_does_not_ask(void)
{
    pc_plug_t p = { 0 };
    pc_plug_tick(&p, true, true);
    CHECK(!pc_plug_tick(&p, true, false));                           /* the host paused SOF for a moment */
    CHECK(!pc_plug_tick(&p, true, false));
    CHECK(!pc_plug_tick(&p, true, true));
    CHECK_EQ_INT(ticks_until_ask(&p, true, true, 10), 0);
}

/* USB drive mode (final review I-1): "in use" as soon as the computer has the drive; the storage starts at USB and the first connect sends no event */
static void test_usb_where(void)
{
    CHECK_EQ_INT(pc_usb_where(false, false), -1);                    /* waiting for the computer */
    CHECK_EQ_INT(pc_usb_where(true, false), 1);                      /* the first plug-in: mounted, no storage event yet */
    CHECK_EQ_INT(pc_usb_where(true, true), 0);                       /* ejected, the cable still in */
    CHECK_EQ_INT(pc_usb_where(false, true), -1);                     /* unplugged after the eject: waiting again */
}

int main(void)
{
    RUN(test_otg_side_asks_once);
    RUN(test_debug_side_never_asks);
    RUN(test_battery_never_asks);
    RUN(test_replug_asks_again);
    RUN(test_flip_from_debug_side_asks);
    RUN(test_short_gap_does_not_ask);
    RUN(test_usb_where);
    return 0;
}
