// SPDX-License-Identifier: LGPL-2.1-or-later
/* The broadcast radio's screen (boot mode "Broadcast radio"): the UI half of cast_mode_run, compiled by the firmware and the simulator. PLAY-ONLY: no station change, no settings.
 * One page at the bottom of the stack that is one menu ring with two cells, 說明 and Back to player (armed). The centre says the station and what it does, the broadcast, Wi-Fi, the volume and
 * how to leave. The knob is the volume as everywhere; a hold anywhere goes back to the player (the system's hold, taken by this page's gesture hook: there is no Back; the flicks do nothing).
 * The ask page (scr_cast_ask) is the radio page's confirm before the reboot. Pages only compose shared pieces (ui_ring, ui_help). LVGL task only. */
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "bsp/bsp_display.h"
#include "pc/pc_power.h"
#include "cast_mode.h"
#include "font.h"
#include "screens.h"
#include "ui.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"

/* the knob stays the volume (the default host path); the screen times out as the external mode's does; the card is ours */
static uint32_t host_idle_ms(void) { return pc_screen_timeout_ms(PC_PWR_USB); }
static const ui_host_t k_cast_host = { .name = "cast", .idle_ms = host_idle_ms, .cfg_writes = true, .haptics = true };

enum { C_HELP, C_EXIT, C_N };
static lv_obj_t *s_ring;
static char s_centre[300];

static void refresh_centre(void)
{
    cast_status_t s;
    cast_mode_status(&s);
    char l2[64], l3[64], b[300];
    if (s.phase == CAST_PH_NO_WIFI) snprintf(l2, sizeof l2, "%s", T(S_CAST_NO_WIFI) /* No Wi-Fi profile */);
    else if (s.phase == CAST_PH_WIFI) snprintf(l2, sizeof l2, "%s", T(S_CAST_WAIT_WIFI) /* Broadcast starts once Wi-Fi connects */);
    else if (s.phase == CAST_PH_WIFI_FAIL) snprintf(l2, sizeof l2, "%s", T(S_CAST_WIFI_FAIL) /* Wi-Fi cannot connect, check the profile */);
    else if (s.phase == CAST_PH_START) snprintf(l2, sizeof l2, "%s", T(S_CAST_STARTING) /* Broadcast starting... */);
    else if (s.phase == CAST_PH_FAIL) snprintf(l2, sizeof l2, "%s", T(S_CAST_FAILED) /* Broadcast failed to start */);
    else snprintf(l2, sizeof l2, "%s%s", T(S_BROADCASTING) /* On air: */, s.cast_name);
    if (s.wifi) snprintf(l3, sizeof l3, T(S_CAST_WIFI_DBM) /* Wi-Fi %d dBm  volume %d */, s.rssi, s.volume);
    else snprintf(l3, sizeof l3, T(S_CAST_WIFI_OFF) /* Wi-Fi not connected  volume %d */, s.volume);
    for (size_t k = strlen(s.now); k && s.now[k - 1] == '\n'; k--) s.now[k - 1] = 0;   /* the radio's "name\n" with no state: no empty line */
    snprintf(b, sizeof b, "%s%s%s\n%s\n%s", s.phase == CAST_PH_ON ? s.now : "", s.phase == CAST_PH_ON ? "\n" : "", l2, l3, T(S_CAST_HINT) /* Hold the screen to leave */);
    if (strcmp(b, s_centre)) { snprintf(s_centre, sizeof s_centre, "%s", b); ui_ring_set_center_text(s_ring, s_centre); }
}

static void pick(int i)
{
    if (i == C_EXIT) cast_mode_exit();
    else if (i == C_HELP) ui_help_open(&HELP_CAST);
}

static const ui_screen_t scr_cast;                      /* defined below */
static bool gesture(ui_gesture_t g)                     /* no Back, no quick settings, nothing to change: the flicks and a hold do nothing (no gesture reboots; the way out is the Back-to-player cell, armed, two taps). Only while this page is on top: the 說明 page above it uses the normal gestures */
{
    (void)g;
    return ui_top() == &scr_cast;
}

static void tick(lv_timer_t *t) { (void)t; refresh_centre(); }

static void page_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[C_N];
    labels[C_HELP] = ui_help_label();
    labels[C_EXIT] = T(S_PLAYER) /* Back to player */;
    s_ring = ui_ring_create_menu(p, labels, C_N, pick, NULL);
    ui_ring_set_system(s_ring, C_EXIT);
    ui_ring_set_item_arm(s_ring, C_EXIT, T(S_TAP_AGAIN_REBOOT_INTO_THE_PLAYER) /* Tap again: reboot to the player */);
    ui_gesture_hook(gesture);
    s_centre[0] = 0;
    ui_timer_bind(p, tick, 500, NULL);
    refresh_centre();
}
static void page_destroy(void) { ui_gesture_hook(NULL); s_ring = NULL; }
static const ui_screen_t scr_cast = { .name = "cast", .create = page_create, .destroy = page_destroy };

void cast_screen_start(lv_display_t *d, lv_indev_t *touch)
{
    font_init();
    ui_start_host(d, touch, &k_cast_host);
    ui_push(&scr_cast, NULL);
}

/* ---- the radio page's confirm: one question, two cells; the first is the reboot ---- */
static void ask_pick(int idx)
{
    if (idx == 0) { cast_mode_request(); return; }
    ui_pop();
}
static void ask_back(void) { ui_pop(); }
static void ask_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[2] = { T(S_CAST_SWITCH) /* Switch to broadcast radio */, T(S_NO) /* No */ };
    lv_obj_t *r = ui_ring_create(p, labels, 2, ask_pick, ask_back);
    ui_ring_set_system(r, 0);
    ui_ring_set_item_hint(r, 0, T(S_CAST_SWITCH_HINT) /* Reboot into broadcast radio\n(music stops) */);
    ui_ring_set_center_text(r, T(S_CAST_ASK) /* The station comes in over Wi-Fi\nand is broadcast with Auracast */);
}
const ui_screen_t scr_cast_ask = { .name = "cast_ask", .create = ask_create };

/* ---- console "cast ..." (the simulator, and the bench build): the model's calls and the screen's own gestures; LVGL lock held for the screen calls ---- */
static void print_status(void)
{
    cast_status_t s;
    cast_mode_status(&s);
    char now[sizeof s.now];
    snprintf(now, sizeof now, "%s", s.now);
    for (char *c = now; *c; c++) if (*c == '\n') *c = '|';
    printf("APP cast phase=%d name=%s wifi=%d rssi=%d station=%d/%d vol=%d bytes=%lu reconn=%lu underruns=%lu now=%s\n", s.phase, s.cast_name, s.wifi, s.rssi, s.station, s.n_stations, s.volume,
           (unsigned long)s.bytes, (unsigned long)s.reconnects, (unsigned long)s.underruns, now);
}
static void console_do(const char *a)
{
    int n = 0;
    if (!strcmp(a, "st")) print_status();
    else if (sscanf(a, "tap %d", &n) == 1) { if (ui_top() && !strcmp(ui_top()->name, "cast") && s_ring) ui_ring_tap(s_ring, n); }
    else if (!strcmp(a, "hold")) gesture(UI_G_HOLD);
    else if (!strncmp(a, "flick ", 6)) ui_flick_inject(!strcmp(a + 6, "up") ? UI_G_UP : !strcmp(a + 6, "down") ? UI_G_DOWN : !strcmp(a + 6, "left") ? UI_G_LEFT : UI_G_RIGHT);   /* test aid: the page takes every flick and does nothing */
    else printf("APP cast console: st | tap N | hold | flick up|down|left|right\n");
}
bool cast_mode_cmd(const char *c)
{
    if (strncmp(c, "cast ", 5)) return false;
    bsp_display_lock(-1);
    console_do(c + 5);
    bsp_display_unlock();
    return true;
}
