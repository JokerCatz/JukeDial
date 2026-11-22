// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_xport.h"
#include "pc_test.h"

static pc_xport_t x;
static void expect(int sender, bool adv, int ask)
{
    CHECK_EQ_INT(pc_xport_sender(&x), sender);
    CHECK(pc_xport_advertise(&x) == adv);
    CHECK_EQ_INT(pc_xport_ask(&x), ask);
}
static void test_start(void)
{
    pc_xport_init(&x);
    expect(PC_XS_USB, false, 0);
    pc_xport_usb(&x, false, 0);                    /* no cable yet is not an unplug */
    expect(PC_XS_USB, false, 0);
    pc_xport_usb(&x, true, 10);
    expect(PC_XS_USB, false, 0);
}
static void test_unplug_yes_replug_yes(void)
{
    pc_xport_init(&x);
    pc_xport_usb(&x, true, 0);
    pc_xport_usb(&x, false, 100);
    expect(PC_XS_NONE, false, 1);                  /* asking: nobody sends, nobody advertises */
    pc_xport_answer(&x, true);
    expect(PC_XS_BLE, true, 0);
    pc_xport_usb(&x, true, 200);
    expect(PC_XS_BLE, true, 2);                    /* asking to go back: BLE still the one sender */
    pc_xport_answer(&x, true);
    expect(PC_XS_USB, false, 0);
}
static void test_cancel_paths(void)
{
    pc_xport_init(&x);
    pc_xport_usb(&x, true, 0);
    pc_xport_usb(&x, false, 100);
    pc_xport_answer(&x, false);
    expect(PC_XS_USB, false, 0);                   /* stays on the USB side */
    pc_xport_tick(&x, 1000000);
    pc_xport_usb(&x, false, 1000001);              /* no new edge: no new question */
    expect(PC_XS_USB, false, 0);
    pc_xport_usb(&x, true, 2000000);
    pc_xport_usb(&x, false, 2000100);              /* a new unplug asks again */
    expect(PC_XS_NONE, false, 1);
    pc_xport_answer(&x, true);
    pc_xport_usb(&x, true, 3000000);
    expect(PC_XS_BLE, true, 2);
    pc_xport_answer(&x, false);
    expect(PC_XS_BLE, true, 0);                    /* stays Bluetooth */
    pc_xport_usb(&x, true, 3000100);
    expect(PC_XS_BLE, true, 0);
    pc_xport_usb(&x, false, 3000200);
    pc_xport_usb(&x, true, 3000300);               /* the next plug-in asks again */
    expect(PC_XS_BLE, true, 2);
}
static void test_timeout(void)
{
    pc_xport_init(&x);
    pc_xport_usb(&x, true, 0);
    pc_xport_usb(&x, false, 1000);
    pc_xport_tick(&x, 1000 + PC_XPORT_ASK_MS - 1);
    expect(PC_XS_NONE, false, 1);
    pc_xport_tick(&x, 1000 + PC_XPORT_ASK_MS);
    expect(PC_XS_USB, false, 0);                   /* no answer = no */
    pc_xport_answer(&x, true);                     /* no question open: nothing */
    expect(PC_XS_USB, false, 0);
}
static void test_moot(void)
{
    pc_xport_init(&x);
    pc_xport_usb(&x, true, 0);
    pc_xport_usb(&x, false, 10);
    pc_xport_usb(&x, true, 20);                    /* the cable is back before the answer */
    expect(PC_XS_USB, false, 0);
    pc_xport_usb(&x, false, 30);
    pc_xport_answer(&x, true);
    pc_xport_usb(&x, true, 40);
    CHECK_EQ_INT(pc_xport_ask(&x), 2);
    pc_xport_usb(&x, false, 50);                   /* gone again while asked: stay on Bluetooth, no question */
    expect(PC_XS_BLE, true, 0);
}
static void test_request(void)
{
    pc_xport_init(&x);
    pc_xport_usb(&x, true, 0);
    pc_xport_request(&x, 5);                       /* cable in: nothing to switch to */
    expect(PC_XS_USB, false, 0);
    pc_xport_usb(&x, false, 10);
    pc_xport_answer(&x, false);
    pc_xport_request(&x, 20);                      /* the cell asks again */
    expect(PC_XS_NONE, false, 1);
    pc_xport_answer(&x, true);
    pc_xport_request(&x, 30);                      /* Bluetooth, no cable: no way back */
    expect(PC_XS_BLE, true, 0);
    pc_xport_usb(&x, true, 40);
    pc_xport_answer(&x, false);
    pc_xport_request(&x, 50);
    expect(PC_XS_BLE, true, 2);
}
static void test_never_both(void)
{
    /* every state: the sender is one value; advertising only with the BLE sender */
    for (int s = 0; s < 4; s++) {
        x.state = (uint8_t)s;
        CHECK(!(pc_xport_advertise(&x) && pc_xport_sender(&x) == PC_XS_USB));
        CHECK(pc_xport_advertise(&x) == (pc_xport_sender(&x) == PC_XS_BLE));
    }
}
int main(void)
{
    RUN(test_start); RUN(test_unplug_yes_replug_yes); RUN(test_cancel_paths); RUN(test_timeout); RUN(test_moot); RUN(test_request); RUN(test_never_both);
    return 0;
}
