// SPDX-License-Identifier: LGPL-2.1-or-later
/* Bluetooth (spec 2026-11-09-bluetooth-design.md): pick a mode on the ring; the centre shows the status. */
#include "esp_attr.h"
#include <stdio.h>
#include <string.h>
#include "ui_theme.h"
#include "bt_svc.h"
#include "cfg.h"
#include "font.h"
#include "icons_gen.h"
#include "screens.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "sysmode.h"

/* the ring: modes, then Status, then Clear pairings, then 說明 as the last cell. The two last
 * cells are derived from the count, so a cell added before them moves them by changing BT_N only. The broadcast quality (cast_q) is on the web page. */
#define BT_STATUS BT_MODE_COUNT
#define BT_N (BT_MODE_COUNT + 3)
#define BT_HELP (BT_N - 1)
#define BT_FORGET (BT_HELP - 1)
static lv_obj_t *s_ring;
static const ui_screen_t scr_bt_devs;
static lv_timer_t *s_t;
static void status(void)
{
    char b[160];
    const char *st = bt_svc_status();
    snprintf(b, sizeof b, "%s%s%s", bt_svc_mode_label(bt_svc_mode()), st[0] ? "\n" : "", st);
    ui_ring_set_center_text(s_ring, b);
}
static void tick(lv_timer_t *t) { (void)t; if (ui_ring_selected(s_ring) < 0) status(); }   /* a selected cell keeps the centre: its name, or what running it does */

static void pick(int idx)
{
    if (idx == BT_STATUS) { ui_help_open(&HELP_STATUS); return; }   /* Status: the explain page with the pipeline as it is */
    if (idx == BT_HELP) { ui_help_open(&HELP_BT); return; }
    if (idx == BT_FORGET) {                          /* forget every paired phone / headphones */
        int n = bt_svc_forget_all();
        char m[96];
        if (n < 0) snprintf(m, sizeof m, "%s", T(S_TURN_A_BLUETOOTH_MODE_ON_FIRST) /* Turn on Bluetooth mode first to clear */);
        else snprintf(m, sizeof m, T(S_FORGOT_D_DEVICE_S) /* Cleared %d pairings */, n);
        ui_push_message(m);
        return;
    }
    if (!bt_svc_available()) { ui_push_message(T(S_BUILT_WITHOUT_BLUETOOTH) /* This firmware has no Bluetooth */); return; }
    if (idx != (int)bt_svc_mode()) {                 /* a new mode starts clean after a reboot (user decision 2026-11-09) */
        cfg_set_int("bt_mode", idx);
        sysmode_reboot();
        return;
    }
    status();
    if (idx == BT_MODE_HEADPHONES || idx == BT_MODE_LISTEN) ui_push(&scr_bt_devs, NULL);   /* pick headphones / a broadcast */
}
/* ---- headphones list (spec bluetooth 4.3): the status line is the title, "scan again" and the devices are list rows ---- */
static lv_obj_t *s_list, *s_note;
static lv_timer_t *s_dt;
static bt_dev_t s_devs[16];
static int s_n = -1;
static void dev_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0) { bt_svc_scan(); s_n = -1; return; }
    bt_svc_connect(s_devs[i].bda);
}
static void dev_row(const char *text, int user, bool sys)
{
    ui_list_row(s_list, text, sys ? UI_SYS : UI_PANEL, dev_cb, (void *)(intptr_t)user);
}
static void devs_tick(lv_timer_t *t)
{
    (void)t;
    lv_label_set_text(s_note, bt_svc_status());
    bt_dev_t d[16];
    int n = bt_svc_devices(d, 16);
    if (n == s_n && !memcmp(d, s_devs, (size_t)n * sizeof *d)) return;
    memcpy(s_devs, d, sizeof d);
    s_n = n;
    lv_obj_clean(s_list);
    dev_row(T(S_SCAN_AGAIN) /* Rescan */, -1, true);
    for (int i = 0; i < n; i++) dev_row(d[i].name, i, false);
}
static void devs_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list_titled(p, ""), &s_list);
    s_note = ui_list_title(s_list);
    lv_label_set_long_mode(s_note, LV_LABEL_LONG_MODE_DOTS);
    s_n = -1;
    ui_back_button(p);
    s_dt = ui_timer_bind(p, devs_tick, 1000, NULL);
    devs_tick(s_dt);
}
static void devs_destroy(void) { s_dt = NULL; }   /* bound to the root */
static bool devs_knob(int d) { return ui_list_knob(s_list, d); }
static const ui_screen_t scr_bt_devs = { .name = "bt_devs", .create = devs_create, .destroy = devs_destroy, .on_knob = devs_knob };

static void back(void) { ui_pop(); }
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[BT_N];
    for (int i = 0; i < BT_MODE_COUNT; i++) labels[i] = bt_svc_mode_label((bt_mode_t)i);
    labels[BT_STATUS] = ui_status_label();                           /* hires T7: Status */
    labels[BT_FORGET] = T(S_FORGET_PAIRED) /* Clear pairings */;
    labels[BT_HELP] = ui_help_label();
    static EXT_RAM_BSS_ATTR const char *icons[BT_N];                                  /* the modes in bt_mode_t order, then Status and Forget (BT_N is a constant: the array is filled each time) */
    icons[BT_MODE_OFF] = UI_ICON_BT_OFF; icons[BT_MODE_SPEAKER] = UI_ICON_BT_SPEAKER; icons[BT_MODE_HEADPHONES] = UI_ICON_BT_HEADSET;
    icons[BT_MODE_BROADCAST] = UI_ICON_BT_CAST; icons[BT_MODE_LISTEN] = UI_ICON_BT_LISTEN; icons[BT_STATUS] = UI_ICON_STATUS; icons[BT_FORGET] = UI_ICON_FORGET;
    s_ring = ui_ring_create(p, labels, BT_N, pick, back);   /* select first, then the centre or the same cell again: nothing here runs from one tap */
    ui_ring_set_icons(s_ring, icons, BT_N);
    char h[96];
    for (int i = 0; i < BT_MODE_COUNT; i++) {                        /* a mode other than the running one reboots */
        if (i == (int)bt_svc_mode()) continue;
        snprintf(h, sizeof h, T(S_REBOOT_INTO_S) /* Reboot to switch to %s */, bt_svc_mode_label((bt_mode_t)i));
        ui_ring_set_item_hint(s_ring, i, h);
    }
    ui_ring_set_item_now(s_ring, BT_STATUS);                         /* Status opens a page at the first tap: nothing to confirm */
    ui_ring_set_item_now(s_ring, BT_HELP);                           /* the help cell opens at the first tap */
    int paired = bt_svc_paired_count();
    if (paired > 0) {
        snprintf(h, sizeof h, T(S_FORGET_D_PAIRINGS) /* Clear %d pairings */, paired);
        ui_ring_set_item_hint(s_ring, BT_FORGET, h);
    }
    ui_ring_set_system(s_ring, BT_FORGET);
    s_t = ui_timer_bind(p, tick, 1000, NULL);
    status();
}
static void destroy(void) { s_t = NULL; }         /* bound to the root */
bool scr_bt_tap_ok(int idx) { return idx == -1 || idx == BT_STATUS || idx == BT_HELP; }
void scr_bt_tap(int idx) { if (ui_top() == &scr_bt && s_ring) { if (idx < 0) status(); else pick(idx); } }   /* console "bt tap N": a tap on cell N (-1 the centre) */
const ui_screen_t scr_bt = { .name = "bt", .create = create, .destroy = destroy };

/* ---- pairing confirmation (requirements notes): the phone and the board show the same six digits ---- */
/* a select-then-confirm ring of two cells, no Back (the phone decides; the timeout rejects): the centre asks, a selected cell says what it does, both with the digits */
static lv_obj_t *s_pring;
static lv_timer_t *s_pt;
static char s_pdig[7];
static void pair_texts(void)
{
    char b[96];
    snprintf(b, sizeof b, "%s\n%s", T(S_SAME_NUMBER_ON_THE_PHONE) /* Is it the same number on the phone? */, s_pdig);
    ui_ring_set_center_text(s_pring, b);
    snprintf(b, sizeof b, "%s\n%s", T(S_PAIR) /* Pair */, s_pdig);
    ui_ring_set_item_hint(s_pring, 0, b);
    snprintf(b, sizeof b, "%s\n%s", T(S_REJECT) /* Reject */, s_pdig);
    ui_ring_set_item_hint(s_pring, 1, b);
}
static void pair_pick(int idx)
{
    bool ok = idx == 0;
    bt_svc_pair_reply(ok);
    ui_pop();
    if (!ok) ui_push_message(T(S_PAIRING_REJECTED) /* Pairing rejected */);
}
static void pair_reject(void) { pair_pick(1); }          /* the bare Back of a ring that could not be built */
static void pair_tick(lv_timer_t *t)
{
    (void)t;
    if (bt_svc_pair_check_timeout()) { ui_pop(); ui_push_message(T(S_NO_ANSWER_PAIRING_REJECTED) /* No answer, pairing rejected */); return; }
    if (!bt_svc_pair_pending(NULL)) { ui_pop(); return; }           /* answered elsewhere (or a new request) */
    char d[7];
    if (bt_svc_pair_pending(d) && strcmp(d, s_pdig)) { memcpy(s_pdig, d, sizeof d); pair_texts(); }
}
static void pair_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[2] = { T(S_PAIR) /* Pair */, T(S_REJECT) /* Reject */ };
    strcpy(s_pdig, "------");
    bt_svc_pair_pending(s_pdig);
    s_pring = ui_ring_create(p, labels, 2, pair_pick, NULL);
    if (!s_pring) ui_back_button_do(p, pair_reject);    /* no memory: the way out is a Back that rejects */
    pair_texts();
    s_pt = ui_timer_bind(p, pair_tick, 1000, NULL);
}
static void pair_destroy(void)
{
    s_pt = NULL;                                         /* bound to the root */
    s_pring = NULL;
}
static bool pair_knob(int d) { (void)d; return true; }   /* the knob does nothing here (no accidental answers) */
const ui_screen_t scr_pair = { .name = "pair", .create = pair_create, .destroy = pair_destroy, .on_knob = pair_knob };
