// SPDX-License-Identifier: LGPL-2.1-or-later
/* Theme colour tokens (ui_tokens.h, ui_tokens.inc). */
#include "esp_attr.h"
#include "ui_tokens.h"
#include <stdlib.h>
#include <string.h>
#include "psram_alloc.h"

#define UNP(...) __VA_ARGS__
#define CORE(k, zh, en)
#define TOK(id, key, n, vals, zh, en) static const uint32_t D_##id[n] = { UNP vals };
#include "ui_tokens.inc"
#undef TOK
#define TOK(id, key, n, vals, zh, en) { key, n, D_##id },
static const struct { const char *key; uint8_t n; const uint32_t *def; } PAL[UI_PAL_COUNT] = {
#include "ui_tokens.inc"
};
#undef TOK
#undef CORE

static EXT_RAM_BSS_ATTR uint32_t *s_ov;      /* every palette's colours back to back (a copy of the defaults, then the file's entries); NULL = all defaults */
static EXT_RAM_BSS_ATTR uint8_t s_touched[UI_PAL_COUNT];   /* per palette: a theme file set at least one entry */
static EXT_RAM_BSS_ATTR int s_nov, s_total;   /* PSRAM: 0 new internal statics */

static int off(int id) { int o = 0; for (int i = 0; i < id; i++) o += PAL[i].n; return o; }
const uint32_t *ui_pal(ui_pal_id_t id)
{
    if (id < 0 || id >= UI_PAL_COUNT) id = (ui_pal_id_t)0;
    return s_ov ? s_ov + off(id) : PAL[id].def;
}

int ui_tok_set_key(const char *key, uint32_t rgb)
{
    char nm[40];
    size_t l = key ? strlen(key) : 0;
    int idx = 0;
    if (!l || l >= sizeof nm) return -1;
    memcpy(nm, key, l + 1);
    char *dot = strrchr(nm, '.');
    bool has_idx = false;
    if (dot && dot[1] >= '0' && dot[1] <= '9' && strlen(dot + 1) <= 2) { idx = atoi(dot + 1); *dot = 0; has_idx = true; }
    for (int i = 0; i < UI_PAL_COUNT; i++) {
        if (strcmp(nm, PAL[i].key)) continue;
        if (has_idx ? idx >= PAL[i].n : PAL[i].n != 1) return -1;
        if (!s_ov) {
            s_total = off(UI_PAL_COUNT);
            s_ov = psram_malloc((size_t)s_total * sizeof(uint32_t));
            if (!s_ov) return -1;
            for (int k = 0, o = 0; k < UI_PAL_COUNT; o += PAL[k].n, k++) memcpy(s_ov + o, PAL[k].def, PAL[k].n * sizeof(uint32_t));
        }
        s_ov[off(i) + idx] = rgb & 0xFFFFFF;
        s_nov++;
        s_touched[i] = 1;
        return 0;
    }
    return -1;
}

void ui_tok_clear(void) { psram_free(s_ov); s_ov = NULL; s_nov = 0; memset(s_touched, 0, sizeof s_touched); }
bool ui_tok_touched(ui_pal_id_t id) { return id >= 0 && id < UI_PAL_COUNT && s_touched[id]; }
int ui_tok_overrides(void) { return s_nov; }
