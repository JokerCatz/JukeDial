// SPDX-License-Identifier: LGPL-2.1-or-later
/* Speaker mode is an exclusive source: while the device is a Bluetooth speaker, a local source (songs, chiptune, radio, white
 * noise) is not started silently. Its play function asks spk_excl_gate() first; in speaker mode the pick is kept and this page asks ONE question:
 * leave speaker mode and play? Modes change by reboot (scr_bt.c), so the text says so. Confirm: the kept pick is started (the gate is open for
 * that one call), bt_mode goes to off and the normal mode reboot runs: its resume record (sysmode.c) is what was just started, so the next boot
 * plays it. Cancel (the cell, Back): the pick is dropped; the mode and the phone's stream are as they were.
 * One question, two cells: only shared pieces (ui_ring), as scr_cast_ask. LVGL task only. */
#include <stdio.h>
#include "esp_attr.h"
#include "bt_svc.h"
#include "cfg.h"
#include "font.h"
#include "player_ctl.h"
#include "screens.h"
#include "sysmode.h"
#include "ui.h"
#include "ui_ring.h"

static EXT_RAM_BSS_ATTR struct { spk_redo_fn redo; int a, b; bool open; } s_k;   /* the kept pick; open: its redo runs now */

static void drop_stopped_now(void)                   /* the caller of a refused pick may have opened the playing page: with nothing playing it goes again */
{
    const now_ops_t *o = player_ctl_now();
    if (o && o->playing()) return;
    if (ui_top() == &scr_viz) ui_pop();
    if (ui_top() == &scr_now) ui_pop();
}
static void ask_pick(int idx)
{
    spk_redo_fn redo = s_k.redo;
    ui_pop();                                        /* destroy drops the kept pick: taken above */
    if (idx != 0 || !redo) { printf("APP spk ask cancel\n"); drop_stopped_now(); return; }
    printf("APP spk ask leave\n");
    s_k.open = true;
    redo(s_k.a, s_k.b);
    s_k.open = false;
    cfg_set_int("bt_mode", BT_MODE_OFF);             /* as the Bluetooth page's Off cell: the mode ends by a reboot */
    sysmode_reboot();
}
static void ask_back(void) { ask_pick(-1); }
static void ask_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[2] = { T(S_SPK_LEAVE_PLAY) /* 離開並播放 */, T(S_CANCEL) /* 取消 */ };
    lv_obj_t *r = ui_ring_create(p, labels, 2, ask_pick, ask_back);
    ui_ring_set_system(r, 0);
    ui_ring_set_item_hint(r, 0, T(S_SPK_LEAVE_HINT) /* 重新開機離開藍牙喇叭模式\n然後播放 */);
    ui_ring_set_center_text(r, T(S_SPK_LEAVE_ASK) /* 離開藍牙喇叭模式並播放？\n需要重新開機 */);
}
static void ask_destroy(void) { if (!s_k.open) s_k.redo = NULL; }   /* left any other way (hold = home, a message over a full stack): the pick is dropped */
const ui_screen_t scr_spk_ask = { .name = "spk_ask", .create = ask_create, .destroy = ask_destroy };

static void ask_open(void *arg)
{
    (void)arg;
    if (!s_k.redo || ui_top() == &scr_spk_ask) return;
    ui_push(&scr_spk_ask, NULL);
    if (ui_top() != &scr_spk_ask) s_k.redo = NULL;   /* the push was refused (a full stack): nothing is kept */
}

bool spk_excl_gate(spk_redo_fn redo, int a, int b)
{
    if (s_k.open || bt_svc_mode() != BT_MODE_SPEAKER) return false;
    s_k.redo = redo; s_k.a = a; s_k.b = b;
    printf("APP spk ask\n");
    lv_async_call_cancel(ask_open, NULL);
    lv_async_call(ask_open, NULL);                   /* after the caller's own page moves (a list popped, the playing page opened): the question ends on top */
    return true;
}
