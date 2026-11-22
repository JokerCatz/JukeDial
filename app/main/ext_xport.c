// SPDX-License-Identifier: LGPL-2.1-or-later
/* 外接模式 T3: transport glue (ext_xport.h). */
#include "ext_xport.h"
#include <stdio.h>
#include "esp_attr.h"
#include "esp_timer.h"
#include "ext_ble.h"
#include "pc/pc_xport.h"
#include "usb_mode.h"

EXT_RAM_BSS_ATTR static pc_xport_t s_x;
EXT_RAM_BSS_ATTR static bool s_ok;
EXT_RAM_BSS_ATTR static int s_last_sender, s_last_state, s_pump_sender;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void ext_xport_start(void)
{
    pc_xport_init(&s_x);
    s_ok = ext_ble_init() == 0;
    s_last_sender = PC_XS_USB;
    s_last_state = PC_XP_USB;
    s_pump_sender = PC_XS_USB;
    printf("APP ext xport start ble=%d\n", s_ok);
}
bool ext_xport_ble_ok(void) { return s_ok; }
int ext_xport_sender(void) { return pc_xport_sender(&s_x); }
int ext_xport_ask(void) { return pc_xport_ask(&s_x); }

void ext_xport_poll(void)
{
    if (!s_ok) return;                                   /* no BLE: never asked, USB stays */
    uint32_t t = now_ms();
    pc_xport_usb(&s_x, usb_mode_mounted(), t);
    pc_xport_tick(&s_x, t);
    if (s_x.state != s_last_state) {
        printf("APP ext xport state=%d sender=%d ask=%d\n", s_x.state, pc_xport_sender(&s_x), pc_xport_ask(&s_x));
        if (s_last_state == PC_XP_ASK_USB && s_x.state == PC_XP_USB) ext_ble_disconnect();   /* the way back to USB: the Bluetooth link goes */
        s_last_state = s_x.state;
    }
    if (pc_xport_advertise(&s_x) != ext_ble_advertising()) ext_ble_advertise(pc_xport_advertise(&s_x));
}
void ext_xport_answer(bool yes)
{
    printf("APP ext xport answer=%d\n", yes);
    pc_xport_answer(&s_x, yes);
    ext_xport_poll();
}
bool ext_xport_request(void)
{
    if (!s_ok) return false;
    int before = s_x.state;
    pc_xport_request(&s_x, now_ms());
    return s_x.state != before;
}
bool ext_xport_link_ok(void)
{
    int s = pc_xport_sender(&s_x);
    return s == PC_XS_USB ? usb_mode_mounted() : s == PC_XS_BLE ? ext_ble_connected() : false;
}
void ext_xport_pump(uint32_t t, ext_send_sink_fn usb_sink, bool usb_up)
{
    int s = pc_xport_sender(&s_x);
    ext_send_sink_fn prev = s_pump_sender == PC_XS_USB ? (usb_up ? usb_sink : NULL) : s_pump_sender == PC_XS_BLE ? (ext_ble_connected() ? ext_ble_sink : NULL) : NULL;
    if (s != s_pump_sender) { ext_send_flush_up(prev); s_pump_sender = s; }   /* the queue never crosses from one transport to the other; all-up first, so no key stays held on the one we leave */
    if (s == PC_XS_USB && usb_up) ext_send_pump(t, usb_sink);
    else if (s == PC_XS_BLE && ext_ble_connected()) ext_send_pump(t, ext_ble_sink);
    else if (ext_send_pending()) ext_send_flush();                         /* no link: nothing to release on */
}
