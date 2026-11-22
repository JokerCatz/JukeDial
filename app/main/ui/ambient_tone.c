// SPDX-License-Identifier: LGPL-2.1-or-later
/* The ambient scenes' colours (ambient_tone.h). */
#include "ambient_tone.h"
#include "ui_theme.h"
#include "ui_tokens.h"

#define NSTYLE 3
/* the colours are theme tokens (ui_tokens.inc: amb_<scene>_<style>, 7 entries: the background, then PC_AM_TONES tones); scene and style order as the enum / the 風格 cell */
static const uint8_t PCT[AMBIENT_BRIGHT_N] = { 20, 35, 50, 65, 80 };

int ambient_bright_pct(int level) { return PCT[level < 0 ? 0 : level >= AMBIENT_BRIGHT_N ? AMBIENT_BRIGHT_N - 1 : level]; }
int ambient_bright_default(void) { return 3; }

/* brightness pct: a dark theme fades toward black; a light page has no black to fade into, the scene fades toward the page's own background */
static lv_color_t dim(uint32_t rgb, int pct, bool light) { return lv_color_mix(lv_color_hex(rgb), light ? UI_BG : lv_color_black(), (uint8_t)(pct * 255 / 100)); }

void ambient_palette(int scene, int style, int level, ambient_pal_t *out)
{
    const ui_pal_id_t id = (ui_pal_id_t)(UI_PAL_amb_lava_1 + (scene < 0 || scene >= PC_AM_SCENES ? 0 : scene) * NSTYLE + ((style % NSTYLE) + NSTYLE) % NSTYLE);
    const uint32_t *r = ui_pal(id);
    const bool light = ui_theme_is_light();
    int pct = ambient_bright_pct(level);
    /* a light theme that carries its own scene colours (gen_card_themes.py writes amb_* for every light card theme): they are authored as the full-strength look,
     * the default level (65) shows them as written, the others scale around it; a light theme file without them falls back to the dark defaults faded toward the page */
    if (light && ui_tok_touched(id)) pct = LV_MIN(100, pct * 100 / PCT[3]);
    out->bg = dim(r[0], pct, light);
    out->rim = dim(r[1], pct * 6 / 10, light);
    for (int i = 0; i < PC_AM_TONES; i++) out->tone[i] = dim(r[1 + i], pct, light);
}
