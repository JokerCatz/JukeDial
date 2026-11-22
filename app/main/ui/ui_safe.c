// SPDX-License-Identifier: LGPL-2.1-or-later
/* The safe-mode screen (UI unify T13, spec 2.8). A shared piece: it composes the slot texts of ui_kit on the display's own screen, with fixed colours and
 * the two built-in fonts that always exist; nothing here reads the theme, cfg or the card, and nothing is clickable (no touch or knob is wired in this mode).
 * The single timer is a one-shot that turns the backlight off after 60 s (a screen left on in a crash loop would only drain the battery). */
#include <stdio.h>
#include "bsp/bsp_display.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_safe.h"

#define SAFE_OFF_MS 60000
/* night3 F: the three lines are one block centred on the glass: the upper line sits as far above the centre as the lower one is below it. The square panel's UPPER slot moved (-80 -> -96, night2) while
 * LOWER stayed at +80, so the block went uneven; the shift is the difference (0 on the round panel: -62 / +62, unchanged) */
#define SAFE_UPPER_SHIFT (-UI_SLOT_LOWER_Y - UI_SLOT_UPPER_Y)

static void backlight_off(lv_timer_t *t)
{
    (void)t;                                              /* repeat count 1: LVGL deletes the timer after this call */
    bsp_display_backlight_set(0);
}

static lv_obj_t *line(lv_obj_t *scr, ui_slot_t s, const lv_font_t *f, const char *text)
{
    lv_obj_t *l = ui_text(scr, s, f);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_text(l, text);
    return l;
}

void ui_safe_show(lv_display_t *d, const char *why, unsigned crashes)
{
    lv_obj_t *scr = lv_display_get_screen_active(d);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    char b[96];
    snprintf(b, sizeof b, "%s\ncount %u", why ? why : "", crashes);
    lv_obj_t *head = line(scr, UI_SLOT_UPPER, &lv_font_montserrat_20, "SAFE MODE");
#if SAFE_UPPER_SHIFT != 0
    if (head) lv_obj_set_style_translate_y(head, SAFE_UPPER_SHIFT, 0);
#else
    (void)head;
#endif   /* the three lines are one block centred on the glass (round: the title slot sat 150 px above the centre, near the edge; 28 px would not fit its 139 px chord either) */
    line(scr, UI_SLOT_CENTER, &lv_font_montserrat_20, b);
    line(scr, UI_SLOT_LOWER, &lv_font_montserrat_20, "Reflash over USB");
    lv_timer_t *t = lv_timer_create(backlight_off, SAFE_OFF_MS, NULL);
    if (t) lv_timer_set_repeat_count(t, 1);
}

/* Over a running image (console `safeshow`): the same screen on a fresh empty one, so the player's pages are neither visible nor reachable. */
void ui_safe_show_fresh(lv_display_t *d, const char *why, unsigned crashes)
{
    lv_obj_t *fresh = lv_obj_create(NULL);
    if (fresh) lv_screen_load(fresh);                      /* no memory: shown on the current screen instead */
    ui_safe_show(d, why, crashes);
}
