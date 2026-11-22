// SPDX-License-Identifier: LGPL-2.1-or-later
/* The FakePod's charge screen (FakePod only; charge_mode.h): the boot mode that shows the battery while the cable charges it, and the same page in its low-battery wording when the
 * boot refuses to start below 2 %. One menu ring: the percent and the state in the centre, two cells (start the player, power off: armed). Pages only compose shared pieces (ui_ring). LVGL task only. */
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "charge_mode.h"
#include "font.h"
#include "screens.h"
#include "ui.h"
#include "ui_kit.h"
#include "ui_ring.h"
#include "ui_theme.h"

enum { CH_START, CH_OFF, CH_N };
static EXT_RAM_BSS_ATTR lv_obj_t *s_ring;                 /* PSRAM: no internal RAM for a page that only the FakePod uses */
static EXT_RAM_BSS_ATTR char s_centre[160];

static void refresh_centre(void)
{
    charge_status_t s;
    charge_mode_status(&s);
    char b[160], pct[16];
    if (s.pct >= 0) snprintf(pct, sizeof pct, "%d %%", s.pct); else snprintf(pct, sizeof pct, "-- %%");
    if (s.lowbat) snprintf(b, sizeof b, T(S_LOWBAT_BOOT) /* Battery too low (%d %%): please charge */, s.pct);
    else snprintf(b, sizeof b, "%s\n%s\n%s", pct, s.full ? T(S_CHG_FULL) /* Fully charged */ : s.charging ? T(S_CHG_CHARGING) /* Charging */ : T(S_CHG_IDLE) /* Not charging */, T(S_CHG_HINT) /* Press the power key to start */);
    if (strcmp(b, s_centre)) { snprintf(s_centre, sizeof s_centre, "%s", b); ui_ring_set_center_text(s_ring, s_centre); }
}

static void pick(int i)
{
    charge_status_t s;
    charge_mode_status(&s);
    if (s.lowbat) return;                                   /* nothing to start: the unit powers off by itself */
    if (i == CH_START) charge_mode_start_player();
    else if (i == CH_OFF) charge_mode_power_off();
}

static void tick(lv_timer_t *t) { (void)t; refresh_centre(); }

static void page_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[CH_N] = { T(S_CHG_START) /* Start player */, T(S_POWER_OFF) /* Power off */ };
    s_ring = ui_ring_create_menu(p, labels, CH_N, pick, NULL);
    ui_ring_set_system(s_ring, CH_OFF);
    ui_ring_set_item_arm(s_ring, CH_OFF, T(S_CHG_TAP_OFF) /* Tap again: power off */);
    s_centre[0] = 0;
    ui_timer_bind(p, tick, 500, NULL);
    refresh_centre();
}
static void page_destroy(void) { s_ring = NULL; }
const ui_screen_t scr_charge = { .name = "charge", .create = page_create, .destroy = page_destroy };

void charge_screen_start(lv_display_t *d, lv_indev_t *touch, const ui_host_t *h)
{
    font_init();
    ui_start_host(d, touch, h);
    ui_push(&scr_charge, NULL);
}
