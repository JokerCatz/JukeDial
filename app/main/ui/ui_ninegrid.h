// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The nine-grid of invisible tap zones (the FakePod's now-playing family: the cover page in the styles 全封面 and 封面＋資訊, and the spectrum page). The round board never attaches it.
 *
 *   [ 切換組合 ] [   選單   ] [ 下一頁 ▶ ]        zone 0 1 2
 *   [  左功能  ] [播放／暫停] [  右功能  ]             3 4 5
 *   [ ◀ 上一首 ] [   關閉   ] [ 下一首 ▶ ]             6 7 8
 *
 * Hidden: the page is clean and a tap runs nothing except showing the labels. Shown (for lab nowb_hide_ms, default 4 s; every tap on a zone restarts it): a tap on a zone runs it; 關閉 hides at once.
 * The centre is play / pause, the top and bottom rows are fixed, the middle row's two zones are the current 組合 (a set); 切換組合 cycles the sets and shows "<set name> i/n". The labels carry the state
 * of a toggle ("隨機：開"). While the labels are shown ui_visual_paused() is true (the picture underneath stands still). The labels are LVGL objects made at the first show and deleted at the hide
 * (nothing is kept between); the one state block lives in PSRAM. A page attaches with one call and gives a small table; it does no hit-testing of its own. All calls from the LVGL task. */
typedef struct {
    const char *(*label)(void);          /* the text now, state in it; NULL: this zone is not available for the playing source (drawn dim, a tap does nothing) */
    void (*run)(void);
    const char *icon;                    /* a UI_ICON_* string put before the text, or NULL */
} ui_ng_cell_t;
typedef struct {
    const char *(*name)(void);           /* the set's name ("播放模式"): the 切換組合 zone shows "<name> i/n" */
    ui_ng_cell_t left, right;
} ui_ng_set_t;
typedef struct {
    int id;                              /* which page: the chosen set is kept per id for the life of the boot (0 cover page, 1 spectrum page) */
    const ui_ng_set_t *const *sets;      /* at least one */
    int n_sets;
    void (*menu)(void);                  /* 選單: the page's own options ring / popup, unchanged */
    void (*next_page)(void);             /* 下一頁: cover -> spectrum -> lyrics -> cover; NULL: the zone is not available */
    bool (*passthru)(void);              /* NULL or false: the grid rules. True (a ring of the page is open, the album photos are on): a tap goes straight to menu(), as before the grid */
} ui_ng_def_t;

extern const ui_ng_set_t UI_NG_PLAYMODE;   /* the shared set 播放模式: 隨機 on / off (left), 循環 all / one (right); the source's own mode_kind / mode_step, nothing new */

void ui_ninegrid_attach(lv_obj_t *page, const ui_ng_def_t *def, bool bands);   /* makes `page` take taps; bands: a title band and a time band show with the cells (the page itself has no text) */
bool ui_ninegrid_shown(void);            /* the labels are up (ui_visual_paused) */
void ui_ninegrid_hide(void);
void ui_ninegrid_console(const char *arg);   /* "" prints `APP ninegrid ...`; "tap N" is a tap on zone N (0..8) obeying the hidden rule */
