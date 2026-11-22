// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdio.h>
#include <string.h>
#include "cfg.h"
#include "font.h"
#include "icons_gen.h"
#include "screens.h"
#include "ui_ring.h"
#include "instrument.h"
#include "player_ctl.h"

enum { M_MP3, M_CHIP, M_NOISE, M_RADIO, M_CLOCK, M_ALBUM, M_TOOLS, M_INST, M_FAVS, M_FX, M_BT, M_SETTINGS, M_COUNT };

/* a source page; when that source owns playback now, the now-playing page goes on top of it (Back still leads to the list). mask: the NOW_SRC_* values of this entry */
static void open_source(const ui_screen_t *page, unsigned mask)
{
    ui_push(page, NULL);
    const now_ops_t *o = player_ctl_now();
    if (o && o->playing() && (mask >> o->src & 1u)) now_open();
}

static void pick(int idx)
{
    printf("APP menu pick=%d\n", idx);
    if (idx == M_MP3 && !g_sd_ok) { ui_push_warning(T(S_NO_TF_CARD) /* No TF card */); return; }
    if (idx == M_MP3) { open_source(&scr_mp3, 1u << NOW_SRC_MP3); return; }
    if (idx == M_NOISE) { if (!g_sd_ok) { ui_push_warning(T(S_NO_TF_CARD) /* No TF card */); return; } open_source(&scr_noise, 1u << NOW_SRC_NOISE | 1u << NOW_SRC_COLOR); return; }
    if (idx == M_CHIP) { if (!g_sd_ok) { ui_push_warning(T(S_NO_TF_CARD) /* No TF card */); return; } open_source(&scr_chip, 1u << NOW_SRC_CHIP); return; }
    if (idx == M_FAVS) { ui_push(&scr_favs, NULL); return; }
    if (idx == M_FX) { ui_push(&scr_fx, NULL); return; }
    if (idx == M_SETTINGS) { ui_push(&scr_settings, NULL); return; }
    if (idx == M_BT) { open_source(&scr_bt, 1u << NOW_SRC_BT); return; }   /* the phone's stream (speaker mode) or a broadcast (listening) plays: its playing page, as every other source; nothing streams: the Bluetooth page with the waiting status */
    if (idx == M_RADIO) { open_source(&scr_radio, 1u << NOW_SRC_RADIO); return; }
    if (idx == M_CLOCK) { ui_push(&scr_clock, NULL); return; }
    if (idx == M_ALBUM) { if (!g_sd_ok) { ui_push_warning(T(S_NO_TF_CARD) /* No TF card */); return; } ui_push(&scr_album, NULL); return; }
    if (idx == M_TOOLS) { ui_push(&scr_tools, NULL); return; }   /* Fortune is inside Tools (2026-11-11) */
    if (idx == M_INST) { instrument_request(); return; }   /* another app: reboots into it */
}

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[M_COUNT] = { T(S_SONGS_2) /* Songs */, "Chiptune", T(S_NOISE_2) /* White noise */, T(S_RADIO) /* Radio */, T(S_CLOCK_2) /* Clock */, T(S_PHOTOS) /* Photos */, T(S_TOOLS) /* Tools */, T(S_INSTRUMENT) /* Instrument */, T(S_FAVS) /* Favourites */, T(S_SOUND_FX) /* Effects */, T(S_BLUETOOTH_2) /* Bluetooth */, T(S_SETUP) /* Settings */ };
    static const char *const icons[M_COUNT] = { [M_MP3] = UI_ICON_SONGS, [M_CHIP] = UI_ICON_CHIP, [M_NOISE] = UI_ICON_NOISE, [M_RADIO] = UI_ICON_RADIO, [M_CLOCK] = UI_ICON_CLOCK, [M_ALBUM] = UI_ICON_PHOTOS,
        [M_TOOLS] = UI_ICON_TOOLS, [M_INST] = UI_ICON_INSTRUMENT, [M_FAVS] = UI_ICON_FAVS, [M_FX] = UI_ICON_EFFECTS, [M_BT] = UI_ICON_BLUETOOTH, [M_SETTINGS] = UI_ICON_SETTINGS };
    lv_obj_t *r = ui_ring_create(p, labels, M_COUNT, pick, NULL);
    ui_ring_set_icons(r, icons, M_COUNT);
    ui_ring_set_item_hint(r, M_INST, T(S_REBOOTS_INTO_THE_INSTRUMENT_MUSIC_STOPS) /* Reboot into instrument mode (music stops) */);   /* the pick reboots at once */
}

const ui_screen_t scr_menu = { .name = "menu", .create = create };

void ui_restore_last(void)
{
    const char *l = cfg_str("last_screen", "");
    if (!g_sd_ok) return;
    if (!strcmp(l, "mp3")) ui_push(&scr_mp3, NULL);
    else if (!strcmp(l, "chip")) ui_push(&scr_chip, NULL);
    else if (!strcmp(l, "noise")) ui_push(&scr_noise, NULL);
    else if (!strcmp(l, "radio")) ui_push(&scr_radio, NULL);
    else if (!strcmp(l, "clock")) ui_push(&scr_clock, NULL);
}
