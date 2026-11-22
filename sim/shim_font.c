// SPDX-License-Identifier: LGPL-2.1-or-later
/* esp_lv_adapter_ft_font_* for the host: what esp_lvgl_adapter does with LVGL 9 (esp_lv_adapter.c, ft_font_init / get / deinit),
 * minus the adapter's own bookkeeping. LVGL 9.5 initialises FreeType itself inside lv_init(). The path goes through
 * sim_path() first: the system FreeType opens the file with its own fopen, which the link-time wrap cannot see. */
#include <stdio.h>
#include "pc/pc_paths.h"
#include <stdlib.h>
#include <string.h>
#include "esp_lv_adapter.h"
#include "fontpak.h"
#include "sim.h"

typedef struct node {
    lv_font_info_t info;
    char *path;          /* info.name points here */
    lv_font_t *font;
    struct node *next;
} node_t;

static node_t *g_fonts;   /* every font made; the firmware never deletes them (the adapter does at its own teardown), sim_font_shutdown() does */

esp_err_t esp_lv_adapter_ft_font_init(const esp_lv_adapter_ft_font_config_t *config, esp_lv_adapter_ft_font_handle_t *handle)
{
    if (handle) *handle = NULL;
    if (!config || !config->name || config->size == 0) return ESP_ERR_INVALID_ARG;
    node_t *n = calloc(1, sizeof *n);
    if (!n) return ESP_ERR_NO_MEM;
    char mapped[512], flash[256];
    const char *name = config->name;
    if (!strncmp(name, FONTPAK_PREFIX, strlen(FONTPAK_PREFIX))) {   /* the flash pack (--mode usb): fake_fontpak.c maps <root>/_player/fonts */
        snprintf(flash, sizeof flash, PC_FONTS_DIR "/%s", name + strlen(FONTPAK_PREFIX));
        name = flash;
    }
    const char *mp = sim_path(name, mapped, sizeof mapped);
    if (!mp) goto fail;   /* a ".." path outside the root: refused like a missing file */
    n->path = strdup(mp);
    if (!n->path) goto fail;
    lv_freetype_init_font_info(&n->info);
    n->info.name = n->path;
    n->info.size = config->size;
    switch (config->style) {
    case ESP_LV_ADAPTER_FT_FONT_STYLE_ITALIC: n->info.style = LV_FREETYPE_FONT_STYLE_ITALIC; break;
    case ESP_LV_ADAPTER_FT_FONT_STYLE_BOLD: n->info.style = LV_FREETYPE_FONT_STYLE_BOLD; break;
    case ESP_LV_ADAPTER_FT_FONT_STYLE_BOLD_ITALIC: n->info.style = LV_FREETYPE_FONT_STYLE_BOLD | LV_FREETYPE_FONT_STYLE_ITALIC; break;
    default: n->info.style = LV_FREETYPE_FONT_STYLE_NORMAL; break;
    }
    n->font = lv_freetype_font_create_with_info(&n->info);
    if (!n->font) goto fail;
    n->next = g_fonts;
    g_fonts = n;
    if (handle) *handle = n;
    return ESP_OK;
fail:
    free(n->path);
    free(n);
    return ESP_FAIL;
}

const lv_font_t *esp_lv_adapter_ft_font_get(esp_lv_adapter_ft_font_handle_t handle) { return handle ? ((node_t *)handle)->font : NULL; }

esp_err_t esp_lv_adapter_ft_font_deinit(esp_lv_adapter_ft_font_handle_t handle)
{
    node_t *n = handle;
    if (!n) return ESP_ERR_INVALID_ARG;
    for (node_t **pp = &g_fonts; *pp; pp = &(*pp)->next)
        if (*pp == n) { *pp = n->next; break; }
    lv_freetype_font_delete(n->font);
    free(n->path);
    free(n);
    return ESP_OK;
}

void sim_font_shutdown(void) { while (g_fonts) esp_lv_adapter_ft_font_deinit(g_fonts); }
