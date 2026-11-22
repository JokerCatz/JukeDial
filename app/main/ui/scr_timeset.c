// SPDX-License-Identifier: LGPL-2.1-or-later
/* Set time: year, month, day, hour, minute as axis rows (ui_axis.h: a tap steps the row, then the knob / the keys step it both ways for 4 s),
 * the live clock on top, Apply sets the clock to that LOCAL time (clock_set_manual). Reached from Settings > System and from the clock menu. The draft lives
 * in PSRAM; nothing is stored in cfg. The time survives power-off only on a board with a valid battery clock (FakePod RTC, clock_rtc_keeps: the note says which).
 * Also here (folded in from the clock menu's "Set time now" cell): a row that syncs over Wi-Fi. */
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "time/clock.h"
#include "clock_svc.h"
#include "font.h"
#include "i18n.h"
#include "screens.h"
#include "ui.h"
#include "ui_axis.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_theme.h"
#include "pc/pc_dial.h"

enum { R_YEAR, R_MONTH, R_DAY, R_HOUR, R_MIN, R_APPLY, R_WIFI, R_N };
#define YEAR_LO 2025
#define YEAR_HI 2040
typedef struct { lv_obj_t *list, *live, *note, *row[R_N]; int v[R_APPLY]; } ts_t;   /* v: year, month, day, hour, minute */
EXT_RAM_BSS_ATTR static ts_t s;

static void set_text(lv_obj_t *label, const char *text) { if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text); }

static void live(void)
{
    char b[96], z[24];
    if (!s.live) return;
    if (!clock_time_valid()) { set_text(s.live, T(S_TIMESET_NOT_SET) /* Time not set yet */); return; }
    clock_civil_t c;
    clock_civil_from_utc(clock_now_utc(), clock_tz_min(), &c);
    clock_zone_label(z, sizeof z);
    snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d  %s", c.year, c.month, c.day, c.hour, c.minute, c.second, z);
    set_text(s.live, b);
}

static void refresh(void)
{
    static const char *const ICON[R_N] = { [R_YEAR] = UI_ICON_L_CALENDAR, [R_MONTH] = UI_ICON_L_CALENDAR, [R_DAY] = UI_ICON_L_CALENDAR, [R_HOUR] = UI_ICON_L_CLOCK, [R_MIN] = UI_ICON_L_CLOCK, [R_APPLY] = UI_ICON_L_CHECK, [R_WIFI] = UI_ICON_L_WIFI };
    char b[64], b2[96];
    for (int i = 0; i < R_N; i++) {
        if (!s.row[i]) continue;
        if (i == R_APPLY) snprintf(b, sizeof b, "%s", T(S_APPLY) /* Apply */);
        else if (i == R_WIFI) snprintf(b, sizeof b, "%s", T(S_TIMESET_WIFI) /* Sync over Wi-Fi */);
        else snprintf(b, sizeof b, "%s: %0*d", i == R_YEAR ? T(S_YEAR) /* Year */ : i == R_MONTH ? T(S_MONTH) /* Month */ : i == R_DAY ? T(S_DAY) /* Day */ : i == R_HOUR ? T(S_HOUR) /* Hour */ : T(S_MINUTE) /* Minute */, i == R_YEAR ? 4 : 2, s.v[i]);
        ui_list_row_set_text(s.row[i], ui_icon_text(b2, sizeof b2, ICON[i], b));
        ui_list_row_look(s.row[i], i == ui_axis_current_of(s.list) ? UI_LOOK_SEL : i >= R_APPLY ? UI_LOOK_SYS : UI_LOOK_NORMAL);
    }
}

static int ring(int i, int d, int n) { if (i < 0) i = 0; if (i >= n) i = n - 1; return ((i + d) % n + n) % n; }
static bool step(int r, int d)                                  /* the axis rows; year is bounded, the rest wrap; the day follows the month's length */
{
    /* every time row is a ring by touch and by knob (59 -> 0, December -> January, 2040 -> 2025): not ui_axis_idx, which stops a half tap at the ends */
    if (r == R_YEAR) s.v[R_YEAR] = YEAR_LO + ring(s.v[R_YEAR] - YEAR_LO, d, YEAR_HI - YEAR_LO + 1);
    else if (r == R_MONTH) s.v[R_MONTH] = 1 + ring(s.v[R_MONTH] - 1, d, 12);
    else if (r == R_HOUR) s.v[R_HOUR] = ring(s.v[R_HOUR], d, 24);
    else if (r == R_MIN) s.v[R_MIN] = ring(s.v[R_MIN], d, 60);
    else if (r == R_DAY) s.v[R_DAY] = 1 + ring(s.v[R_DAY] - 1, d, clock_days_in_month(s.v[R_YEAR], s.v[R_MONTH]));
    else return false;
    const int dim = clock_days_in_month(s.v[R_YEAR], s.v[R_MONTH]);
    if (s.v[R_DAY] > dim) s.v[R_DAY] = dim;                     /* 31 Jan -> Feb, or a leap year left */
    refresh();
    return true;
}
static void row_end(void) { refresh(); }

static void row_cb(lv_event_t *e)
{
    const int r = (int)(intptr_t)lv_event_get_user_data(e);
    if (r == R_WIFI) {                                          /* what the clock menu's old cell did: Wi-Fi on and SNTP; the page goes first, then the message (the ui design notes: ui_pop before ui_push_message) */
        clock_sync_now();
        ui_pop();
        ui_push_message(T(S_SYNCING_THE_TIME) /* Connecting Wi-Fi to set the time... */);
        return;
    }
    if (r != R_APPLY) { ui_list_row_axis_tap(e, s.list, r, step, row_end); return; }
    if (ui_axis_current_of(s.list) >= 0) ui_axis_end();
    if (clock_set_manual(s.v[R_YEAR], s.v[R_MONTH], s.v[R_DAY], s.v[R_HOUR], s.v[R_MIN])) { live(); ui_push_message(T(S_TIME_IS_SET) /* Time set */); }
    refresh();
}

static void tick(lv_timer_t *t) { (void)t; live(); }

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    memset(&s, 0, sizeof s);
    clock_civil_t c = { .year = 2026, .month = 1, .day = 1 };
    if (clock_time_valid()) clock_civil_from_utc(clock_now_utc(), clock_tz_min(), &c);
    s.v[R_YEAR] = c.year < YEAR_LO ? YEAR_LO : c.year > YEAR_HI ? YEAR_HI : c.year;
    s.v[R_MONTH] = c.month; s.v[R_DAY] = c.day; s.v[R_HOUR] = c.hour; s.v[R_MIN] = c.minute;
    ui_obj_guard(ui_list_titled(p, T(S_SET_TIME) /* Set time */), &s.list);
    s.live = ui_list_note(s.list, "");
    for (int i = 0; i < R_N; i++) { s.row[i] = ui_list_row(s.list, "", UI_PANEL, row_cb, (void *)(intptr_t)i); if (i != R_APPLY && i != R_WIFI) ui_list_row_axis(s.row[i]); }
    s.note = ui_list_note(s.list, clock_rtc_keeps() ? T(S_TIMESET_NOTE_RTC) /* Kept by the built-in clock */ : T(S_TIMESET_NOTE) /* Lost at power-off */);
    ui_list_help_row(s.list, &HELP_TIMESET);                   /* local time, Apply, what a Wi-Fi sync does */
    ui_back_button(p);
    ui_timer_bind(p, tick, 1000, NULL);
    live();
    refresh();
}
static void destroy(void) { memset(&s, 0, sizeof s); }
static bool on_knob(int d)
{
    if (ui_axis_knob(d)) return true;
    return ui_list_knob(s.list, d);
}
const ui_screen_t scr_timeset = { .name = "timeset", .create = create, .destroy = destroy, .on_knob = on_knob };

/* console "timeset [open | key year|month|day|hour|min|apply|wifi | note | rows]" */
void timeset_cmd(const char *a)
{
    if (!strcmp(a, "open")) { ui_push(&scr_timeset, NULL); return; }
    if (ui_top() != &scr_timeset) { printf("APP timeset rows=0\n"); return; }
    if (!strncmp(a, "key ", 4)) {
        static const struct { const char *k; int r; } K[] = { { "year", R_YEAR }, { "month", R_MONTH }, { "day", R_DAY }, { "hour", R_HOUR }, { "min", R_MIN }, { "apply", R_APPLY }, { "wifi", R_WIFI } };
        for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) if (!strcmp(a + 4, K[i].k)) { lv_obj_send_event(s.row[K[i].r], LV_EVENT_CLICKED, NULL); return; }
        printf("APP timeset key unknown %s\n", a + 4);
        return;
    }
    if (!strcmp(a, "note")) { printf("APP timeset note=%s\n", s.note ? lv_label_get_text(s.note) : ""); return; }
    printf("APP timeset draft=%04d-%02d-%02d %02d:%02d\n", s.v[R_YEAR], s.v[R_MONTH], s.v[R_DAY], s.v[R_HOUR], s.v[R_MIN]);
}
