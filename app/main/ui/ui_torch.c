// SPDX-License-Identifier: LGPL-2.1-or-later
/* The flashlight, the toys ring's last cell: a pure white page (#FFFFFF, whatever the theme) at the full backlight, no text, no chrome.
 * The page asks ui.c for full light (ui_backlight_full): only the LIVE backlight goes to 100 while this page is the top one; cfg "brightness" is never written, so the quick settings
 * (a page over it) show the real value, closing them lights it again, and every way out (up flick, hold-home, destroy by a theme rebuild, sleep / wake, reboot) ends at the user's own
 * brightness. Sleep: the backlight goes to 0 by ui.c and a wake lights it at 100 again while this page is still on top. Music goes on; the knob stays the volume.
 * A piece: it needs one raw white fill that the theme must not recolour. No timer, no static state. Console: `tools torch`. */
#include <stdio.h>
#include "screens.h"
#include "ui.h"

static void torch_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    lv_obj_set_style_bg_color(p, lv_color_hex(0xFFFFFF), 0);   /* the page root is the whole panel: white everywhere */
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    ui_backlight_full(&scr_torch);
    printf("APP torch on\n");
}
static void torch_destroy(void)
{
    ui_backlight_full(NULL);                                    /* back to the saved brightness, on whatever path the page went */
    printf("APP torch off\n");
}
const ui_screen_t scr_torch = { .name = "tools_torch", .create = torch_create, .destroy = torch_destroy };
