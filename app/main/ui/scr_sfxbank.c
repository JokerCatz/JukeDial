// SPDX-License-Identifier: LGPL-2.1-or-later
/* Sound bank (sfx task 2), in Tools: every entry of the chiptune sound bank (sfx_bank.def), a header per group, a tap
 * plays it (sfx_audition: whatever the toysfx setting says, the tap asks for it) over the music; 說明 last. Composed of shared pieces only.
 * Built with -msmall-data-limit=0 (its statics are PSRAM .bss). Console: `sfxbank rows`, `sfxbank tap <name|row>` (tools_cmd: `tools sfxbank`). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "screens.h"
#include "sfx.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_theme.h"

static lv_obj_t *s_list;
static lv_obj_t *s_row[PC_SFX_ENTRIES];

static const char *group(int g)
{
    switch (g) {
    case PC_SFX_G_UI: return T(S_INTERFACE) /* Interface */;
    case PC_SFX_G_TOY: return T(S_TOYS) /* Tools */;
    case PC_SFX_G_GAME: return T(S_GAMES_2) /* Games */;
    default: return T(S_MORE_CHIP_COLOURS) /* Other chip voices */;
    }
}

static void row_cb(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    bool ok = sfx_audition(id);
    printf("APP sfxbank play %s ok=%d\n", pc_sfx_name(sfx_bank(), id) ? pc_sfx_name(sfx_bank(), id) : "?", ok ? 1 : 0);
}

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const pc_sfx_bank_t *b = sfx_bank();
    memset(s_row, 0, sizeof s_row);
    ui_obj_guard(ui_list_titled(p, T(S_SOUND_BANK) /* Sound bank */), &s_list);
    if (!pc_sfx_count(b)) ui_list_note(s_list, T(S_NO_SOUNDS) /* No sounds */);
    for (int g = 0; g < PC_SFX_GROUPS; g++) {
        bool head = false;
        for (int id = 0; id < pc_sfx_count(b); id++) {
            const pc_sfx_entry_t *e = pc_sfx_entry(b, id);
            if (e->group != g) continue;
            if (!head) { ui_list_header(s_list, group(g)); head = true; }
            s_row[id] = ui_list_row(s_list, T_RAW(e->zh, e->en), UI_PANEL, row_cb, (void *)(intptr_t)id);
        }
    }
    ui_list_help_row(s_list, &HELP_SFXBANK);
    ui_back_button(p);
}

static void destroy(void) { s_list = NULL; memset(s_row, 0, sizeof s_row); }
static bool on_knob(int d) { return ui_list_knob(s_list, d); }

const ui_screen_t scr_sfxbank = { .name = "tools_sfxbank", .create = create, .destroy = destroy, .on_knob = on_knob };

void sfxbank_cmd(const char *a)
{
    const pc_sfx_bank_t *b = sfx_bank();
    if (ui_top() != &scr_sfxbank) { printf("APP sfxbank not open\n"); return; }
    if (!strcmp(a, "rows")) {
        for (int id = 0; id < pc_sfx_count(b); id++) {
            const pc_sfx_entry_t *e = pc_sfx_entry(b, id);
            printf("APP sfxbank row %d %s group=%s label=%s\n", id, e->name, pc_sfx_group_key(e->group), T_RAW(e->zh, e->en));
        }
        return;
    }
    if (!strncmp(a, "tap ", 4)) {
        const char *w = a + 4;
        char *end;
        long n = strtol(w, &end, 10);
        int id = *w && !*end ? (int)n : pc_sfx_id(b, w);
        if (id >= 0 && id < pc_sfx_count(b) && s_row[id]) lv_obj_send_event(s_row[id], LV_EVENT_CLICKED, NULL);
        else printf("APP sfxbank no row %s\n", w);
        return;
    }
    printf("APP sfxbank unknown %s\n", a);
}
