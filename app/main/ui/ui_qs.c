// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "bsp/bsp_display.h"
#include "bt_svc.h"
#include "cfg.h"
#include "vol_out.h"
#include "clock_svc.h"
#include "time/clock.h"
#include "font.h"
#include "power_svc.h"
#include "screens.h"
#include "sysmode.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_qs.h"
#include "ui_theme.h"
#include "wifi_svc.h"

/* The quick-settings page (ui_qs.h). Layout in 360-design units (ui_scale makes it big on the square panel); every row fits the round glass at its y. */
#define ROW_W      280
#define BTN_W      48
#define BTN_H      48
#define SW_W       104
#define IDLE_MS    4000
#define BT_PREV    "bt_prev"           /* cfg: the Bluetooth mode that a switch-off (or airplane) remembers, 1 = speaker when nothing was stored */

enum { K_BM, K_BP, K_VM, K_VP, K_BT, K_WIFI, K_AIR, K_N };
EXT_RAM_BSS_ATTR static lv_obj_t *s_btn[K_N];
EXT_RAM_BSS_ATTR static lv_obj_t *s_bval, *s_vval, *s_status, *s_why;

static int S(int v) { return ui_scale(v); }
static bool host_is(const char *n) { return !strcmp(ui_host_name(), n); }
static bool managed_elsewhere(void) { return host_is("inst") || host_is("ext"); }   /* these boot modes own the radios */
static bool bt_on(void) { return bt_svc_mode() != BT_MODE_OFF; }
static bool wifi_allowed(void) { return cfg_int("wifi_allow", 1) != 0; }
/* airplane = BT off + Wi-Fi denied AS ONE SWITCH (M8): cfg air_on is set by it and cleared by the Bluetooth and Wi-Fi switches, so two separate switch-offs are not "airplane",
 * and the state it saved (air_bt: the Bluetooth mode, air_wifi: the Wi-Fi allow) is what it restores. */
static bool air_on(void) { return cfg_int("air_on", 0) && !bt_on() && !wifi_allowed(); }
EXT_RAM_BSS_ATTR static uint32_t s_msg_t0;                 /* I4: a Bluetooth mode change is a reboot: the first tap arms (UI_HOLD_MS), the line says so */
EXT_RAM_BSS_ATTR static bool s_msg;
static bool reboot_confirm(int k)                          /* the shared two-tap (ui_arm_tap): true = the second tap, go */
{
    if (ui_arm_tap(s_btn[k], font_has_icons() ? UI_ICON_L_REBOOT : T(S_TAP_AGAIN_2) /* Tap again */))   /* icon switches: the power icon (the icon font has no words), the line below says why */ { s_msg = false; return true; }
    s_msg = true; s_msg_t0 = lv_tick_get();
    if (s_why) lv_label_set_text(s_why, T(S_QS_REBOOT_APPLY) /* Applies after reboot */);
    return false;
}

/* what a switch cannot do here, and why: NULL = it works */
static const char *why_not(int k)
{
    if (managed_elsewhere() && (k == K_BT || k == K_WIFI || k == K_AIR)) return T(S_QS_WHY_MODE) /* This mode manages the radio itself */;
    if (k == K_BT && !bt_svc_available()) return T(S_BUILT_WITHOUT_BLUETOOTH) /* This firmware has no Bluetooth */;
    if (k == K_WIFI && bt_svc_mode() == BT_MODE_BROADCAST) return T(S_QS_WHY_CAST) /* On air: not enough memory for Wi-Fi */;
    return NULL;
}

static void status_text(char *b, size_t n)
{
    char t[16] = "--:--";
    int64_t now = clock_now_utc();
    if (now > 1000000000) { clock_civil_t v; clock_civil_from_utc(now, clock_tz_min(), &v); snprintf(t, sizeof t, "%02d:%02d", v.hour, v.minute); }
    bt_mode_t m = bt_svc_mode();
    if (m == BT_MODE_HEADPHONES && !vol_out_is_bt()) m = BT_MODE_OFF;   /* BTVOL: the output the volume row below belongs to: headphones mode with no headset streaming plays (and sets) the wired output */
    const char *out = m == BT_MODE_HEADPHONES || m == BT_MODE_BROADCAST ? bt_svc_mode_label(m) : T(S_QS_OUT_WIRED) /* Wired output */;
    pc_power_t p = power_svc_state();
    char pct[16] = "";                                                  /* F2: the FakePod's gauge; the round board answers -1 and shows the source only */
    const int bp = power_svc_batt_pct();
    if (bp >= 0 && p != PC_PWR_UNKNOWN) snprintf(pct, sizeof pct, " %d%%%s", bp, power_svc_charging() ? "+" : "");
    const char *ic = font_has_icons() ? UI_ICON_L_CLOCK : "", *io = font_has_icons() ? (m == BT_MODE_HEADPHONES || m == BT_MODE_BROADCAST ? UI_ICON_L_BLUETOOTH : UI_ICON_L_VOLUME) : "", *ib = font_has_icons() ? (power_svc_charging() ? UI_ICON_L_CHARGE : UI_ICON_L_BATTERY) : "";   /* ICON-B: time / output / power */
    snprintf(b, n, "%s %s  %s %s%s%s %s%s", ic, t, io, out, p == PC_PWR_UNKNOWN ? "" : "\n", p == PC_PWR_UNKNOWN ? "" : ib, p == PC_PWR_USB ? T(S_QS_POWER_USB) : p == PC_PWR_BATT ? T(S_QS_POWER_BATT) : "", pct);
}

static void look(lv_obj_t *b, bool on, bool usable)
{
    lv_obj_set_style_bg_color(b, !usable ? UI_PANEL : on ? UI_ON : UI_PANEL2, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), usable ? UI_FG : UI_DIM, 0);
}

static const char *why_now(void)                           /* the one line under the switches: what one cannot do, else that a reboot is asked for */
{
    const char *why = NULL;
    for (int k = K_BT; k <= K_AIR && !why; k++) why = why_not(k);
    if (!why && s_msg && lv_tick_elaps(s_msg_t0) < UI_HOLD_MS) why = T(S_QS_REBOOT_APPLY) /* Applies after reboot */;
    return why;
}

static void refresh(void)
{
    if (!s_bval) return;
    lv_label_set_text_fmt(s_bval, "%d", cfg_brightness());   /* the row icon names it */
    lv_label_set_text_fmt(s_vval, "%d", app_volume());
    char b[96];
    status_text(b, sizeof b);
    lv_label_set_text(s_status, b);
    const char *why = NULL;
    look(s_btn[K_BT], bt_on(), !why_not(K_BT));
    look(s_btn[K_WIFI], wifi_allowed(), !why_not(K_WIFI));
    look(s_btn[K_AIR], air_on(), !why_not(K_AIR));
    why = why_now();
    lv_label_set_text(s_why, why ? why : "");
}

static void step_bright(int d)
{
    int v = cfg_brightness() + d * 10;
    v = v < 10 ? 10 : v > 100 ? 100 : v;
    cfg_set_int("brightness", v);
    bsp_display_backlight_set((uint8_t)v);
}
static void step_vol(int d)
{
    int mx = cfg_int("volume_max", 100), v = app_volume() + d;
    v = v < 0 ? 0 : v > mx ? mx : v;
    app_set_volume(v);
    if (ui_volume_hook) ui_volume_hook(v);
}
static void set_wifi(bool allow)
{
    cfg_set_int("wifi_allow", allow);
    if (!allow) wifi_svc_set(false);                       /* every holder lets go, the radio goes down now */
}
/* I4 (the bluetooth design notes: a mode change starts clean after a reboot): the switch stores bt_mode and reboots after the second tap; the running image never changes mode */
static void bt_store_and_reboot(bt_mode_t m)
{
    if (bt_on()) cfg_set_int(BT_PREV, (int)bt_svc_mode());    /* a switch-off (or airplane) remembers the mode it left */
    cfg_set_int("bt_mode", (int)m);
    sysmode_reboot();
}
static void act(int k)
{
    if (k != K_BM && k != K_BP && k != K_VM && k != K_VP && why_not(k)) { refresh(); return; }
    switch (k) {
    case K_BM: step_bright(-1); break;
    case K_BP: step_bright(1); break;
    case K_VM: step_vol(-1); break;
    case K_VP: step_vol(1); break;
    case K_BT: {
        if (!reboot_confirm(K_BT)) break;
        const int p = cfg_int(BT_PREV, BT_MODE_SPEAKER);
        cfg_set_int("air_on", 0);
        bt_store_and_reboot(bt_on() ? BT_MODE_OFF : (bt_mode_t)(p > 0 && p < BT_MODE_COUNT ? p : BT_MODE_SPEAKER));
        return;
    }
    case K_WIFI:
        cfg_set_int("air_on", 0);
        set_wifi(!wifi_allowed());
        break;
    case K_AIR:
        if (air_on()) {                                       /* back to what it was: a Bluetooth that was off stays off, a Wi-Fi that was denied stays denied */
            const int m = cfg_int("air_bt", 0);
            const bool bt_back = m > 0 && m < BT_MODE_COUNT && m != (int)bt_svc_mode();
            if (bt_back && !reboot_confirm(K_AIR)) break;
            cfg_set_int("air_on", 0);
            set_wifi(cfg_int("air_wifi", 1) != 0);
            if (bt_back) { cfg_set_int("bt_mode", m); sysmode_reboot(); return; }
        } else {
            const bool bt_off = bt_on();
            if (bt_off && !reboot_confirm(K_AIR)) break;
            cfg_set_int("air_bt", bt_on() ? (int)bt_svc_mode() : 0);
            cfg_set_int("air_wifi", wifi_allowed());
            cfg_set_int("air_on", 1);
            set_wifi(false);
            if (bt_off) { bt_store_and_reboot(BT_MODE_OFF); return; }
        }
        break;
    }
    refresh();
}
static void btn_cb(lv_event_t *e) { act((int)(intptr_t)lv_event_get_user_data(e)); }
static void page_tap(void) { ui_qs_close(); }

static void idle_cb(lv_timer_t *t)
{
    (void)t;
    if (ui_top() != &scr_qs) return;
    if (lv_display_get_inactive_time(NULL) > IDLE_MS) ui_qs_close();
    else refresh();                                       /* the clock moves on; the power source changes */
}

static lv_obj_t *button(lv_obj_t *p, int k, int w, int x, int y, const char *text)
{
    lv_obj_t *l;
    lv_obj_t *b = ui_button(p, S(w), S(BTN_H), S(x), S(y), btn_cb, (void *)(intptr_t)k, &l);
    lv_label_set_text(l, text);
    lv_obj_set_style_bg_color(b, UI_PANEL2, 0);
    s_btn[k] = b;
    return b;
}
static lv_obj_t *name_label(lv_obj_t *p, const char *text, int x, int y, int w, lv_text_align_t al)
{
    lv_obj_t *l = ui_label(p, NULL, S(y), S(w));
    lv_obj_align(l, LV_ALIGN_CENTER, S(x), S(y));
    lv_obj_set_style_text_align(l, al, 0);
    lv_label_set_text(l, text);
    return l;
}

#define ICON_PX 28  /* the page's one icon size (360 units) */
#define VAL_X   3   /* the value font: UNSCII 8 times this */
static void icon_font(lv_obj_t *l)
{
    const lv_font_t *f = font_icon(S(ICON_PX));
    if (f) lv_obj_set_style_text_font(l, f, 0);
}
/* a round switch: the icon alone (the name when there is no icon font) */
static void round_switch(lv_obj_t *p, int k, int x, int y, const char *icon, const char *name)
{
    lv_obj_t *l;
    lv_obj_t *b = ui_button(p, S(68), S(68), S(x), S(y), btn_cb, (void *)(intptr_t)k, &l);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, UI_PANEL2, 0);
    if (font_has_icons()) { icon_font(l); lv_label_set_text(l, icon); } else lv_label_set_text(l, name);
    s_btn[k] = b;
}
/* a step button: the minus / plus icon */
static lv_obj_t *step_button(lv_obj_t *p, int k, int w, int x, int y, bool plus)
{
    lv_obj_t *b = button(p, k, w, x, y, font_has_icons() ? (plus ? UI_ICON_L_PLUS : UI_ICON_L_MINUS) : plus ? "+" : "-");
    if (font_has_icons()) icon_font(lv_obj_get_child(b, 0));
    return b;
}
static lv_obj_t *row_icon(lv_obj_t *p, const char *icon, const char *name, int x, int y, int w)
{
    lv_obj_t *l = name_label(p, font_has_icons() ? icon : name, x, y, w, LV_TEXT_ALIGN_CENTER);
    if (font_has_icons()) icon_font(l);
    return l;
}

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    memset(s_btn, 0, sizeof s_btn);
    ui_page_tap(p, page_tap);                            /* a tap on the empty page closes it (the buttons take their own taps) */
    s_status = name_label(p, "", 0, -106, 220, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(s_status, UI_DIM, 0);
    for (int r = 0; r < 2; r++) {
        const int y = r ? 18 : -42;   /* (B): one capsule per row, [- icon value +], no row names */
        lv_obj_t *cap = lv_obj_create(p);
        lv_obj_remove_style_all(cap);
        lv_obj_set_size(cap, S(272), S(56));
        lv_obj_align(cap, LV_ALIGN_CENTER, 0, S(y));
        lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(cap, UI_PANEL2, 0);
        lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
        lv_obj_remove_flag(cap, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *m = step_button(p, r ? K_VM : K_BM, 48, -108, y, false);
        lv_obj_t *pl = step_button(p, r ? K_VP : K_BP, 48, 108, y, true);
        lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_radius(pl, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(m, UI_PANEL, 0);
        lv_obj_set_style_bg_color(pl, UI_PANEL, 0);
        row_icon(p, r ? UI_ICON_L_VOLUME : UI_ICON_L_BRIGHT, r ? T(S_VOLUME) : T(S_BRIGHTNESS), -38, y, 44);
        lv_obj_t *v = name_label(p, "", 20, y, 80, LV_TEXT_ALIGN_RIGHT);   /* right-aligned: the ones digits of both rows line up (monospaced UNSCII) */
        lv_obj_set_style_text_font(v, font_unscii8_xn(VAL_X), 0);   /* the numbers in UNSCII 8 x3, hard pixel edges (the bubble's look) */
        if (r) s_vval = v; else s_bval = v;
    }
    round_switch(p, K_BT, -84, 86, UI_ICON_L_BLUETOOTH, T(S_BLUETOOTH_2) /* Bluetooth */);
    round_switch(p, K_WIFI, 0, 86, UI_ICON_L_WIFI, T(S_QS_WIFI));
    round_switch(p, K_AIR, 84, 86, UI_ICON_L_PLANE, T(S_QS_AIRPLANE) /* Airplane */);
    s_why = name_label(p, "", 0, 136, 200, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(s_why, UI_DIM, 0);
    ui_timer_bind(p, idle_cb, 500, NULL);
    ui_back_set(NULL);                                   /* the up flick closes it itself (ui.c) */
    refresh();
}

static void destroy(void) { s_bval = s_vval = s_status = s_why = NULL; memset(s_btn, 0, sizeof s_btn); }

const ui_screen_t scr_qs = { .name = "qs", .create = create, .destroy = destroy };

bool ui_qs_is(const ui_screen_t *s) { return s == &scr_qs; }
void ui_qs_show(void)
{
    if (ui_top() == &scr_qs) return;
    lv_display_trigger_activity(NULL);   /* opening counts as activity: opened without a touch (console, web) the 4 s idle close would fire at once */
    ui_push(&scr_qs, NULL);
}
void ui_qs_close(void) { if (ui_top() == &scr_qs) ui_pop(); }

void ui_qs_cmd(const char *arg)
{
    static const char *const names[K_N] = { "bright-", "bright+", "vol-", "vol+", "bt", "wifi", "air" };
    if (!strcmp(arg, "close")) { ui_qs_close(); return; }
    if (!strncmp(arg, "tap ", 4)) {
        for (int k = 0; k < K_N; k++) if (!strcmp(arg + 4, names[k]) && ui_top() == &scr_qs) { act(k); break; }
    } else if (!*arg) { ui_qs_show(); return; }
    const char *why = why_now();
    printf("APP qs open=%d bright=%d vol=%d bt=%d wifi=%d air=%d why=%s\n", ui_top() == &scr_qs, cfg_brightness(), app_volume(), bt_on(), wifi_allowed(), air_on(), why ? why : "-");
}
