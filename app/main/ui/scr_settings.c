// SPDX-License-Identifier: LGPL-2.1-or-later
/* Settings (spec 4.7): sound preset (+ what it cannot do here), sleep timer, rescan, brightness,
 * haptic on/off, toy sounds (sfx.h), the Bluetooth audio and Wired output pages (BTAUDIO). Strength, length, volume limit, ReplayGain, cover options: the web page. Tap a row to change it.
 * A row with three or more values is an axis (rule R21, ui_axis.h): the tap steps it, then the knob steps it both ways for 4 s instead of scrolling.
 * Brightness and volume limit are bounded axes (UI unify T5): the knob stops at the ends, a tap at the top goes back to the bottom (ui_axis_step). */
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "ui_theme.h"
#include "bsp/bsp_display.h"
#include "cfg.h"
#include "cpu_clock.h"
#include "font.h"
#include "i18n.h"
#include "rate_test.h"
#include "sd_format.h"
#include "screens.h"
#include "sleep_timer.h"
#include "speed_svc.h"
#include "power_pol.h"
#include "sysmode.h"
#include "usb_mode.h"
#include "ui_axis.h"
#include "ui_help.h"
#include "now_fp.h"
#include "wifi_svc.h"
#include "ui_kit.h"
#include "ui_tour.h"
#include "bsp/bsp_display.h"
#include "pc/pc_backend.h"
#include "pc/pc_dial.h"
#include "pc/pc_speed.h"

enum { R_SLEEP, R_AUTOOFF, R_THEME, R_STYLE, R_NOWFP, R_COVERS, R_BRIGHT, R_HAPTIC, R_TOYSFX, R_BTAUDIO, R_WIRED,   /* BTAUDIO: two groups, each its own page (the wired ceiling / hard limit moved into R_WIRED) */
       R_XFADE, R_AMIGA, R_RATETEST, R_CARDTEST, R_WIFI, R_USB, R_REBOOT, R_RESCAN, R_LANG, R_ROTATE, R_EXPERIMENTAL, R_CPUCLK, R_TOUR, R_TIME, R_N };
static lv_obj_t *s_rows[R_N], *s_col;
EXT_RAM_BSS_ATTR static lv_obj_t *s_card;         /* Card unusable (cardfmt): the first row, only while the card may be formatted; PSRAM (no internal RAM) */
static const int SLEEP_STEPS[] = { 0, 15, 30, 60, 90 };


/* ICON-B: each row's leading LINE icon (one icon per concept: the same ones the quick settings and the status line use) */
static const char *const ROW_ICON[R_N] = {
    [R_SLEEP] = UI_ICON_L_MOON, [R_THEME] = UI_ICON_L_THEME, [R_STYLE] = UI_ICON_L_STYLE, [R_NOWFP] = UI_ICON_L_DISC, [R_COVERS] = UI_ICON_L_PHOTO, [R_BRIGHT] = UI_ICON_L_BRIGHT,
    [R_HAPTIC] = UI_ICON_L_VIBRATE, [R_TOYSFX] = UI_ICON_L_TOYSFX, [R_BTAUDIO] = UI_ICON_L_BLUETOOTH, [R_WIRED] = UI_ICON_L_WIRED, [R_XFADE] = UI_ICON_L_CROSSFADE, [R_AMIGA] = UI_ICON_L_MUSIC,
    [R_RATETEST] = UI_ICON_L_GAUGE, [R_CARDTEST] = UI_ICON_L_SD, [R_WIFI] = UI_ICON_L_WIFI, [R_USB] = UI_ICON_L_USB, [R_REBOOT] = UI_ICON_L_REBOOT, [R_RESCAN] = UI_ICON_L_RESCAN,
    [R_LANG] = UI_ICON_L_LANG, [R_ROTATE] = UI_ICON_L_ROTATE, [R_EXPERIMENTAL] = UI_ICON_L_WARN, [R_CPUCLK] = UI_ICON_L_WARN, [R_TOUR] = UI_ICON_L_TOUR, [R_TIME] = UI_ICON_L_CLOCK, [R_AUTOOFF] = UI_ICON_L_MOON,
};

/* The categories: the top page lists six, each opens scr_setgroup with only its rows (same row code). A row hidden on a board stays hidden. */
enum { G_DISPLAY, G_SOUND, G_POWER, G_CARDCONN, G_ADVANCED, G_SYSTEM, G_N };
static const int8_t GROUP_ROWS[G_N][7] = {
    [G_DISPLAY] = { R_BRIGHT, R_THEME, R_STYLE, R_NOWFP, R_ROTATE, R_LANG, -1 },
    [G_SOUND] = { R_WIRED, R_BTAUDIO, R_XFADE, R_AMIGA, R_TOYSFX, R_HAPTIC, -1 },
    [G_POWER] = { R_SLEEP, R_AUTOOFF, -1 },
    [G_CARDCONN] = { R_RESCAN, R_COVERS, R_CARDTEST, R_USB, R_WIFI, -1 },
    [G_ADVANCED] = { R_RATETEST, R_EXPERIMENTAL, R_CPUCLK, -1 },
    [G_SYSTEM] = { R_TIME, R_TOUR, R_REBOOT, -1 },
};
static const char *const GROUP_KEY[G_N] = { "display", "sound", "power", "card", "advanced", "system" };
static const char *const GROUP_ICON[G_N] = { UI_ICON_L_BRIGHT, UI_ICON_L_WIRED, UI_ICON_L_MOON, UI_ICON_L_SD, UI_ICON_L_GAUGE, UI_ICON_L_REBOOT };
static const char *group_name(int g)
{
    switch (g) {
    case G_DISPLAY: return T(S_SETG_DISPLAY);
    case G_SOUND: return T(S_SETG_SOUND);
    case G_POWER: return T(S_SETG_POWER);
    case G_CARDCONN: return T(S_SETG_CARDCONN);
    case G_ADVANCED: return T(S_SETG_ADVANCED);
    default: return T(S_SETG_SYSTEM);
    }
}
static int group_of(int r) { for (int g = 0; g < G_N; g++) for (int k = 0; GROUP_ROWS[g][k] >= 0; k++) if (GROUP_ROWS[g][k] == r) return g; return 0; }
/* rows that open another page (or the Wi-Fi help page) or are system actions: UI_SYS, the value rows keep UI_PANEL */
static bool row_opens(int r) { return r == R_BTAUDIO || r == R_WIRED || r == R_COVERS || r == R_CARDTEST || r == R_RATETEST || r == R_TOUR || r == R_TIME || r == R_WIFI || r == R_RESCAN || r == R_USB || r == R_REBOOT; }
EXT_RAM_BSS_ATTR static lv_obj_t *s_tcol, *s_cat[G_N];   /* the top page's list and category rows (the group page owns s_col / s_rows) */
static int8_t s_group;                                    /* the group page now built (console key routing) */

static void refresh(void)
{
    char b[96], b2[112];
    for (int i = 0; i < R_N; i++) {
        if (!s_rows[i]) continue;                                          /* R_AUTOOFF on the round board: no such row (it has no PMU) */
        switch (i) {
        case R_SLEEP: if (sleep_timer_minutes()) snprintf(b, sizeof b, "%s: %d %s", T(S_SLEEP) /* Sleep */, sleep_timer_minutes(), T(S_MIN_2) /* min */); else snprintf(b, sizeof b, "%s: %s", T(S_SLEEP) /* Sleep */, T(S_OFF) /* Off */); break;
        case R_THEME: snprintf(b, sizeof b, "%s: %s", T(S_THEME) /* Colour scheme */, ui_theme_label()); break;
        case R_STYLE: snprintf(b, sizeof b, "%s: %s", T(S_MENU_STYLE) /* Menu style */, ui_menu_style_label()); break;
        case R_NOWFP: snprintf(b, sizeof b, "%s: %s", T(S_NOWFP_STYLE) /* Now playing */, nowfp_style_label()); break;   /* FakePod only: three values */
        case R_COVERS: scr_covers_row_label(b, sizeof b); break;   /* hires T8e: opens the Cover cache page */
        case R_BRIGHT: snprintf(b, sizeof b, "%s: %d%%", T(S_BRIGHTNESS) /* Brightness */, cfg_brightness()); break;
        case R_HAPTIC: snprintf(b, sizeof b, "%s: %s", T(S_HAPTICS) /* Haptics */, cfg_int("haptic_on", 1) ? T(S_ON) /* On */ : T(S_OFF) /* Off */); break;   /* on / off only: the strength and the length are on the web page */
        case R_TOYSFX: snprintf(b, sizeof b, "%s: %s", T(S_TOY_SOUNDS) /* Toy sounds */, cfg_int("toysfx", 1) ? T(S_ON) /* On */ : T(S_OFF) /* Off */); break;   /* sfx.h */
        case R_BTAUDIO: snprintf(b, sizeof b, "%s", T(S_BT_AUDIO) /* Bluetooth audio */); break;   /* opens its page: preferences + the headset's best and the actual */
        case R_WIRED: snprintf(b, sizeof b, "%s", T(S_WIRED_OUTPUT) /* Wired output */); break;      /* opens its page: the wired ceiling, hard limit, slot width, DAC options */
        case R_XFADE: {                                                   /* Track B: 0 / 2 / 4 / 6 s; off with its reason when the mode is too heavy */
            const char *why = speed_svc_why();
            if (speed_svc_xfade_s() && !why) { char s[24]; snprintf(s, sizeof s, T(S_XFADE_SEC) /* %d s */, speed_svc_xfade_s()); snprintf(b, sizeof b, "%s: %s", T(S_CROSSFADE) /* Crossfade */, s); }
            else if (speed_svc_xfade_s()) snprintf(b, sizeof b, "%s: %s (%s)", T(S_CROSSFADE) /* Crossfade */, T(S_OFF) /* Off */, why);
            else snprintf(b, sizeof b, "%s: %s", T(S_CROSSFADE) /* Crossfade */, T(S_OFF) /* Off */);
            break;
        }
        case R_AMIGA: {                                                   /* Amiga tone: off / A500 / A500 + LED / A1200 */
            static const char *const NAME[PC_AMIGA_N] = { NULL, "A500", "A500 + LED", "A1200" };
            const int m = cfg_int("amiga_mix", 0);
            snprintf(b, sizeof b, "%s: %s", T(S_AMIGA_TONE) /* Amiga tone */, m > 0 && m < PC_AMIGA_N ? NAME[m] : T(S_OFF) /* Off */);
            break;
        }
        case R_CARDTEST: snprintf(b, sizeof b, "%s", T(S_CT_TITLE) /* Card diagnostics */); break;
        case R_RATETEST: snprintf(b, sizeof b, "%s", T(S_RATE_TEST) /* Sample rate test */); break;
        case R_WIFI: snprintf(b, sizeof b, "Wi-Fi: %s", wifi_svc_connected() ? T(S_CONNECTED_3) /* Connected */ : T(S_SET_UP_ON_PC) /* Set up on a computer */); break;   /* never the name or the password */
        case R_USB: snprintf(b, sizeof b, "%s", T(S_USB_DRIVE_MODE) /* USB drive mode */); break;
        case R_REBOOT: snprintf(b, sizeof b, "%s", T(S_REBOOT) /* Reboot */); break;
        case R_LANG: snprintf(b, sizeof b, "%s: %s", T(S_LANGUAGE) /* Language */, i18n_lang_label()); break;   /* i18n A: Traditional Chinese / Simplified Chinese / English, the value in its own name */
        case R_ROTATE: snprintf(b, sizeof b, "%s: %s%s", T(S_ROTATION) /* Screen orientation */, cfg_rotation() ? "180°" : "0°", cfg_rotation() != bsp_display_rotation() ? T(S_AFTER_REBOOT) /* (applies after reboot) */ : ""); break;   /* two values, read at boot (pc_rotate.h) */
        case R_EXPERIMENTAL: snprintf(b, sizeof b, "%s: %s", T(S_EXPERIMENTAL_FORMATS) /* Experimental formats */, cfg_int("exp_fmt", 0) ? T(S_ON) /* On */ : T(S_OFF) /* Off */); break;   /* format tiers: default off */
        case R_CPUCLK: snprintf(b, sizeof b, "%s: %d MHz", T(S_CPU_CLOCK) /* Experimental CPU clock */, cpu_clock_norm(cfg_int("cpu_mhz", CPU_CLOCK_DEFAULT_MHZ))); break;   /* the saved value (the real clock is on the Status page) */
        case R_AUTOOFF: {                                                  /* FakePod only (hidden on the round board): off / 10 / 30 / 60 min idle */
            const int m = power_pol_autooff_min();
            if (m) snprintf(b, sizeof b, "%s: %d %s", T(S_AUTO_POWER_OFF) /* Auto power-off */, m, T(S_MIN_2) /* min */);
            else snprintf(b, sizeof b, "%s: %s", T(S_AUTO_POWER_OFF) /* Auto power-off */, T(S_OFF) /* Off */);
            break;
        }
        case R_TIME: snprintf(b, sizeof b, "%s", T(S_SET_TIME) /* Set time */); break;   /* opens its page */
        case R_TOUR: snprintf(b, sizeof b, "%s", T(S_TOUR_REPLAY) /* Gesture tutorial */); break;   /* GS: plays the gesture tutorial again */
        default: snprintf(b, sizeof b, "%s", T(S_RESCAN_LIBRARY) /* Rescan library */); break;
        }
        lv_label_set_text(lv_obj_get_child(s_rows[i], 0), ui_icon_text(b2, sizeof b2, ROW_ICON[i], b));
        lv_obj_set_style_bg_color(s_rows[i], i == ui_axis_current_of(s_col) ? UI_SEL : row_opens(i) ? UI_SYS : UI_PANEL, 0);
        ui_list_row_axis_sync(s_rows[i]);
    }
}

static int index_in(const int *v, int n, int x) { int i = 0; while (i < n && v[i] != x) i++; return i; }   /* n: not one of them (a step goes on from the last) */

EXT_RAM_BSS_ATTR static bool s_xf_note;                                  /* crossfade was turned on during this borrow: its note is due */
static void row_end(void)                                                 /* the borrow ended: the row's own colour again */
{
    refresh();
    if (s_xf_note) { s_xf_note = false; if (speed_svc_xfade_s()) ui_push_message(T(S_XFADE_RATE_NOTE) /* Tracks with different sample rates do not crossfade */); }
}
/* the multi-value rows: step by d (signed, wraps); false: not such a row */
static bool row_step(int r, int d)
{
    switch (r) {
    case R_SLEEP: sleep_timer_set(SLEEP_STEPS[ui_axis_idx(index_in(SLEEP_STEPS, 5, sleep_timer_minutes()), d, 5)]); break;
    case R_THEME:
        ui_theme_step(d);                                                  /* rebuilds every screen, this one too: the borrow goes with the old list */
        ui_axis_borrow(s_col, R_THEME, row_step, row_end, false);          /* ... and is taken again on the new one */
        break;
    case R_BRIGHT: {
        int v = ui_axis_step(cfg_brightness(), 10, 100, 5, d, ui_axis_from_tap());
        cfg_set_int("brightness", v);
        bsp_display_backlight_set((uint8_t)v);
        break;
    }
    case R_LANG:
        i18n_set_lang(pc_lang_norm((int)i18n_lang() + (d > 0 ? 1 : PC_LANG_COUNT - 1)));    /* three values: wraps; rebuilds every screen, this one too */
        ui_axis_borrow(s_col, R_LANG, row_step, row_end, false);
        break;
    case R_XFADE: {
        const int was = speed_svc_xfade_s();
        if (!ui_axis_would_wrap(was, pc_xfade_step(was, d), d)) speed_svc_xfade_step(d);
        if (!was && speed_svc_xfade_s()) s_xf_note = true;   /* said once it was turned on, when the row is let go (row_end): a 44.1k / 48k library skips about half its joins */
        break;
    }   /* a half tap stops at 0 / the longest */
    case R_AMIGA: cfg_set_int("amiga_mix", ui_axis_idx(cfg_int("amiga_mix", 0), d, PC_AMIGA_N)); break;   /* four values: the next block of a playing module takes it (cfg.c pushes it to the backend) */
    case R_AUTOOFF: if (!ui_axis_would_wrap(power_pol_autooff_min(), pc_apo_step(power_pol_autooff_min(), d), d)) power_pol_autooff_step(d); break;
    case R_NOWFP: if (ui_axis_clamping()) { const int c = nowfp_style(), n = ui_axis_idx(c, d, 3); if (n != c) nowfp_style_step(n - c); } else nowfp_style_step(d); break;                              /* the next now-playing page takes it */
    case R_CPUCLK: {                                                       /* the list runs 320, 240, 160: a step by +1 goes DOWN in MHz; a half tap stops at the ends */
        const int c = cpu_clock_norm(cfg_int("cpu_mhz", CPU_CLOCK_DEFAULT_MHZ)), n = cpu_clock_step(c, d);
        if (!ui_axis_would_wrap(c, n, -d)) cfg_set_int("cpu_mhz", n);
        break;
    }   /* three values, wraps; cfg_set_str applies it (cpu_clock.c) */
    default: return false;
    }
    refresh();
    return true;
}
static bool row_is_axis(int r) { return r == R_XFADE || r == R_AMIGA || r == R_AUTOOFF || r == R_NOWFP || r == R_CPUCLK || r == R_SLEEP || r == R_BRIGHT || r == R_THEME || r == R_LANG; }

static void row_cb(lv_event_t *e)
{
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    if (r != R_USB && r != R_RESCAN && r != R_REBOOT && r != R_EXPERIMENTAL && r != R_CPUCLK) ui_arm_cancel();              /* a tap anywhere else cancels an armed row (the two-tap rows: ui_arm_tap) */
    if (r == R_XFADE && speed_svc_why()) { ui_push_message(speed_svc_why()); return; }   /* Track B: off now (honest degrade): the tap says why, in full (the row clips it) */
    if (r == R_CPUCLK && ui_axis_current_of(s_col) == R_CPUCLK && cpu_clock_norm(cfg_int("cpu_mhz", CPU_CLOCK_DEFAULT_MHZ)) == CPU_CLOCK_DEFAULT_MHZ) {   /* experimental: from 320 a tap on the selected row says what it risks (the row wraps), the next one steps (a first tap only selects it); the knob and later taps need no more */
        if (!ui_arm_tap(s_rows[R_CPUCLK], T(S_CPU_CLOCK_ARM) /* Experimental: a lower CPU clock ... Tap again to turn on */)) return;
    }
    if (row_is_axis(r)) { ui_list_row_axis_tap(e, s_col, r, row_step, row_end); return; }
    if (ui_axis_current_of(s_col) >= 0) ui_axis_end();             /* another row: the knob scrolls again */
    if (r == R_BTAUDIO) { ui_push(&scr_btaudio, NULL); return; }
    if (r == R_WIRED) { ui_push(&scr_wiredout, NULL); return; }
    if (r == R_TIME) { ui_push(&scr_timeset, NULL); return; }
    if (r == R_WIFI) {
        ui_help_open(&HELP_WIFI);                                  /* the QR and the address: the name and password are typed on the computer */
        return;
    } else if (r == R_STYLE) {
        ui_menu_style_next();                                      /* two values: a plain tap; rebuilds every screen */
        return;
    } else if (r == R_HAPTIC) {                                    /* two values: a plain tap; the strength and the length are on the web page */
        cfg_set_int("haptic_on", !cfg_int("haptic_on", 1));
        ui_haptic_tap();                                           /* silent when it was just turned off */
    } else if (r == R_ROTATE) {                                    /* two values: a plain tap; the screen turns at the next boot */
        cfg_set_int("rotate", !cfg_rotation());
    } else if (r == R_EXPERIMENTAL) {                              /* two values: off -> on takes two taps (the first says what it risks); on -> off one; the scan and the next open read it */
        if (!cfg_int("exp_fmt", 0)) { if (!ui_arm_tap(s_rows[R_EXPERIMENTAL], T(S_EXPERIMENTAL_ARM) /* These readers are less mature: a damaged file may reboot the player. Tap again to turn on */)) return; }
        cfg_set_int("exp_fmt", !cfg_int("exp_fmt", 0));
    } else if (r == R_TOYSFX) {                                    /* two values: a plain tap; the next sfx_play reads it */
        cfg_set_int("toysfx", !cfg_int("toysfx", 1));
    } else if (r == R_TOUR) {                                       /* GS: the tutorial again; finishing it stores tour_done as before */
        ui_push(&scr_tour, NULL);
        return;
    } else if (r == R_COVERS) {                                    /* the page has the mode, Pause and Build now (hires T8e) */
        ui_push(&scr_covers, NULL);
        return;
    } else if (r == R_CARDTEST) {                                  /* a plain row: the page runs the tests and cancels them when it goes */
        ui_push(&scr_cardtest, NULL);
        return;
    } else if (r == R_RATETEST) {                                  /* a plain row: the page starts the test and leaves it when it goes (hires T11) */
        rate_test_want();
        ui_push(&scr_ratetest, NULL);
        return;
    } else if ((r == R_USB || r == R_REBOOT) && sd_format_state() == SDF_RUN) {   /* review m4: no restart while the card is being written */
        printf("APP settings busy=format\n");
        ui_push_message(T(S_FORMATTING_PLEASE_WAIT) /* Formatting, please wait */);
        return;
    } else if (r == R_USB) {                                       /* first tap: say what happens, 4 s to confirm */
        if (ui_arm_tap(s_rows[R_USB], T(S_TAP_AGAIN_REBOOT_INTO_USB_DRIVE_MODE) /* Tap again: reboot into drive mode */)) usb_mode_request();   /* reboots: the card goes to the computer */
        return;
    } else if (r == R_REBOOT) {                                    /* what plays is stored and carries on after the restart (sysmode_reboot) */
        if (ui_arm_tap(s_rows[R_REBOOT], T(S_TAP_AGAIN_REBOOT) /* Tap again: reboot */)) sysmode_reboot_msg(T(S_RESTARTING) /* Rebooting... */);   /* no mode is switched: a plain text (final review M3) */
        return;
    } else if (r == R_RESCAN) {                                    /* a rescan (playback goes on, hires T8e): the same two taps */
        if (ui_arm_tap(s_rows[R_RESCAN], T(S_TAP_AGAIN_RESCAN) /* Tap again: rescan */)) ui_push(&scr_scan, NULL);
        return;
    }
    refresh();
}

static void card_cb(lv_event_t *e)
{
    (void)e;
    ui_arm_cancel();
    ui_push(&scr_tfcard, NULL);
}

static void cat_cb(lv_event_t *e)
{
    ui_arm_cancel();
    ui_push(&scr_setgroup, (void *)(intptr_t)(1 + (int)(intptr_t)lv_event_get_user_data(e)));   /* arg: group + 1 */
}

/* console "settings tap N": the N-th row of the open page (the top page: a category) */
void scr_settings_tap(int row)
{
    int n = 0;
    if (ui_top() == &scr_settings) { if (row >= 0 && row < G_N && s_cat[row]) lv_obj_send_event(s_cat[row], LV_EVENT_CLICKED, NULL); return; }
    if (ui_top() != &scr_setgroup) return;
    for (int k = 0; GROUP_ROWS[s_group][k] >= 0; k++) if (s_rows[GROUP_ROWS[s_group][k]] && n++ == row) { lv_obj_send_event(s_rows[GROUP_ROWS[s_group][k]], LV_EVENT_CLICKED, NULL); return; }
}

static void open_group(int g)                                      /* console: the group page on top, from wherever the settings are open */
{
    if (ui_top() == &scr_setgroup) { if (s_group == g) return; ui_pop(); }
    if (ui_top() == &scr_settings) ui_push(&scr_setgroup, (void *)(intptr_t)(g + 1));
}

void scr_settings_group(const char *name)
{
    int g = -1;
    if (name[0] >= '1' && name[0] <= '6' && !name[1]) g = name[0] - '1';
    for (int i = 0; i < G_N; i++) if (!strcmp(name, GROUP_KEY[i])) g = i;
    if (g < 0) { printf("APP settings group unknown %s\n", name); return; }
    open_group(g);
}

/* console "settings key <name>": the same click by the row's name, so a task that adds or removes a row renumbers no test (each task keeps only its own rows' names) */
static const struct { const char *key; int row; } KEYS[] = {
    { "sleep", R_SLEEP }, { "theme", R_THEME }, { "style", R_STYLE }, { "nowfp", R_NOWFP }, { "covers", R_COVERS }, { "bright", R_BRIGHT },
    { "haptic", R_HAPTIC }, { "toysfx", R_TOYSFX }, { "btaudio", R_BTAUDIO }, { "wired", R_WIRED },
    { "xfade", R_XFADE }, { "amiga", R_AMIGA }, { "ratetest", R_RATETEST }, { "cardtest", R_CARDTEST },
    { "wifi", R_WIFI }, { "usb", R_USB }, { "reboot", R_REBOOT }, { "rescan", R_RESCAN }, { "lang", R_LANG }, { "rotate", R_ROTATE }, { "experimental", R_EXPERIMENTAL }, { "cpuclk", R_CPUCLK }, { "tour", R_TOUR }, { "time", R_TIME }, { "autooff", R_AUTOOFF },
};
void scr_settings_tap_key(const char *key)
{
    if (!strcmp(key, "card")) {                                    /* cardfmt: the row exists only while the card may be formatted (top page) */
        if (ui_top() == &scr_setgroup) ui_pop();
        if (ui_top() == &scr_settings && s_card) lv_obj_send_event(s_card, LV_EVENT_CLICKED, NULL); else printf("APP settings card none\n");
        return;
    }
    for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
        if (!strcmp(KEYS[i].key, key)) {                           /* the row's category page first (as a tap path), then the click */
            if (ui_top() != &scr_settings && ui_top() != &scr_setgroup) return;
            open_group(group_of(KEYS[i].row));
            if (ui_top() == &scr_setgroup && s_rows[KEYS[i].row]) lv_obj_send_event(s_rows[KEYS[i].row], LV_EVENT_CLICKED, NULL);
            return;
        }
    printf("APP settings key unknown %s\n", key);
}

/* console "settings rows": one line with the count, one per row label (the page on top: the six categories, or one category's rows) */
void scr_settings_rows(void)
{
    bool top = ui_top() == &scr_settings, grp = ui_top() == &scr_setgroup;
    int n = 0;
    for (int i = 0; top && i < G_N; i++) if (s_cat[i]) n++;
    for (int i = 0; grp && i < R_N; i++) if (s_rows[i]) n++;
    printf("APP settings rows=%d\n", n);
    if (top && s_card) printf("APP settings card=%s\n", lv_label_get_text(lv_obj_get_child(s_card, 0)));
    for (int i = 0; top && i < G_N; i++) if (s_cat[i]) printf("APP settings row=%s\n", lv_label_get_text(lv_obj_get_child(s_cat[i], 0)));
    for (int k = 0; grp && GROUP_ROWS[s_group][k] >= 0; k++) if (s_rows[GROUP_ROWS[s_group][k]]) printf("APP settings row=%s\n", lv_label_get_text(lv_obj_get_child(s_rows[GROUP_ROWS[s_group][k]], 0)));
}

static void create(lv_obj_t *p, void *arg)                         /* the top page: [card row], six categories, 說明 */
{
    (void)arg;
    ui_obj_guard(ui_list(p), &s_tcol);
    s_card = NULL;
    if (sd_format_guide_row()) {                                   /* the card failed to mount (any reason): its page first, with what to do (cardfmt / cardsafe); the format row is only for gpt / nofs */
        char b[64];
        scr_tfcard_row_label(b, sizeof b);
        s_card = ui_list_row_wrap(s_tcol, b, UI_SYS, card_cb, NULL);
    }
    for (int g = 0; g < G_N; g++) {
        char b[96];
        s_cat[g] = ui_list_row(s_tcol, ui_icon_text(b, sizeof b, GROUP_ICON[g], group_name(g)), UI_SYS, cat_cb, (void *)(intptr_t)g);
    }
    ui_list_help_row(s_tcol, &HELP_SETTINGS);                      /* the last row: 說明 (the plug sides, ReplayGain, the settings page QR) */
    ui_back_button(p);
}

static void gcreate(lv_obj_t *p, void *arg)                        /* one category: its rows, the same row code */
{
    const int g = (int)(intptr_t)arg - 1 < 0 ? 0 : (int)(intptr_t)arg - 1;
    s_group = (int8_t)g;
    ui_obj_guard(ui_list(p), &s_col);
    memset(s_rows, 0, sizeof s_rows);
    for (int k = 0; GROUP_ROWS[g][k] >= 0; k++) {
        const int i = GROUP_ROWS[g][k];
        if (i == R_NOWFP && !nowfp_available()) continue;          /* FakePod only: the round board keeps its ring page, no row */
        if (i == R_AUTOOFF && !power_pol_available()) continue;    /* FakePod only: the round board has no PMU, nothing to set, no greyed row */
        if (i == R_EXPERIMENTAL || i == R_CPUCLK) s_rows[i] = ui_list_row_wrap(s_col, "", UI_PANEL, row_cb, (void *)(intptr_t)i);   /* its armed text is long: the row wraps */
        else s_rows[i] = ui_list_row(s_col, "", UI_PANEL, row_cb, (void *)(intptr_t)i);
        if (row_is_axis(i)) ui_list_row_axis(s_rows[i]);
    }
    ui_back_button(p);
    refresh();
}

static bool gon_knob(int d)
{
    ui_arm_cancel();                                               /* scrolling cancels an armed row */
    if (ui_axis_knob(d)) return true;                              /* a multi-value row has the knob for 4 s */
    return ui_list_knob(s_col, d);
}
static bool on_knob(int d) { ui_arm_cancel(); return ui_list_knob(s_tcol, d); }

static void gresume(void) { if (s_col) refresh(); }                /* back from Cover cache etc.: its mode may have changed (hires T8e); s_col NULL: the rows were another instance's */
static void resume(void)                                           /* back from the card page: a card that was formatted (or reads now) is no longer "cannot be used" */
{
    if (s_card && !sd_format_guide_row()) { ui_list_row_set_text(s_card, T(S_CARD) /* Memory card */); ui_list_row_look(s_card, UI_LOOK_NORMAL); }
}

static void destroy(void) { s_card = NULL; }

const ui_screen_t scr_settings = { .name = "settings", .create = create, .destroy = destroy, .on_knob = on_knob, .on_resume = resume };
const ui_screen_t scr_setgroup = { .name = "setgroup", .create = gcreate, .on_knob = gon_knob, .on_resume = gresume };
