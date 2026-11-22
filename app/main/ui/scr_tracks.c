// SPDX-License-Identifier: LGPL-2.1-or-later
/* The song tree. One level of the library at a time: the first row shuffles this folder (with its sub-folders), then its own songs, then its sub-folders (the playing order); a long level
 * is PAGED (PAGE_ROWS items a page: the side flick turns the page, the knob runs on into the next one, the bubble says "n-N", all in ui_list_pager). A folder row opens the
 * next level IN this page (the page stack is eight deep, the tree up to nine levels), Back / the up flick goes up one level and, from the first level, leaves.
 * The music root is the same page with Rescan as its last row (scr_mp3.c is its name in the menu). */
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "ui_theme.h"
#include "font.h"
#include "library_svc.h"
#include "mp3_ctl.h"
#include "crashnet.h"
#include "pc/pc_utf.h"
#include "psram_alloc.h"
#include "screens.h"
#include "ui_kit.h"

#define PAGE_ROWS 40     /* items (songs, then folders) per page: LVGL builds a page in a few ms whatever the library holds (lib-stress: 200 rows 10 ms, 2,000 rows 0.7 s) */

typedef struct {
    lv_obj_t *list;
    int path[PC_LIB_DEPTH + 2];     /* the folders from the level this page was opened at (path[0]) down to the one shown; -1 = the music root */
    int depth;                      /* index of the shown level in path */
    bool top;                       /* the menu's music page: Rescan last, a song tap does not pop it */
    int first, own, nchild, items;  /* the shown level: own songs tracks [first, first + own), sub-folders, items = nchild + own */
    size_t skip;                    /* the 【artist】 prefix every title of this level starts with, said once */
    unsigned lib_gen;               /* library_gen() when the level was made: a rescan under a page that is not on top replaces the library (a page turn then starts again at the root) */
} tree_t;
EXT_RAM_BSS_ATTR static tree_t s_inst[2];   /* [0] the menu's music page, [1] the song list the playing page opens; both can be on the stack at once */

/* a row's user data: the instance in bit 0, the folder / song number above it */
#define PACK(inst, v) ((void *)(intptr_t)((((intptr_t)(v)) << 1) | (inst)))
static tree_t *inst_of(lv_event_t *e, int *v)
{
    const intptr_t d = (intptr_t)lv_event_get_user_data(e);
    if (v) *v = (int)(d >> 1);
    return &s_inst[d & 1];
}
static int level(const tree_t *s) { return s->path[s->depth]; }
static int pages(const tree_t *s) { return s->items ? (s->items + PAGE_ROWS - 1) / PAGE_ROWS : 1; }

static void go_now(const tree_t *s)
{
    if (!s->top) ui_pop();                           /* this page */
    now_open();                                      /* on to the playing page */
}
static void shuffle_cb(lv_event_t *e)
{
    tree_t *s = inst_of(e, NULL);
    ui_arm_cancel();
    printf("APP pick mp3 %d\n", level(s));
    mp3_ctl_play_folder(level(s));                   /* a user action: it may replace what plays (rule R20) */
    go_now(s);
}
static void song_cb(lv_event_t *e)
{
    int t;
    tree_t *s = inst_of(e, &t);
    ui_arm_cancel();
    if (mp3_ctl_current() >= 0 && mp3_ctl_folder() == level(s)) mp3_ctl_play_track(t);   /* the same pool: no new one */
    else mp3_ctl_play_folder_track(level(s), t);
    go_now(s);
}
static void level_open(tree_t *s, int folder, int focus_item);
static void folder_cb(lv_event_t *e)
{
    int f;
    tree_t *s = inst_of(e, &f);
    ui_arm_cancel();
    printf("APP pick mp3 %d\n", f);
    if (s->depth + 1 >= (int)(sizeof s->path / sizeof s->path[0])) return;
    s->path[++s->depth] = f;
    level_open(s, f, -1);
}
static void rescan_cb(lv_event_t *e)
{
    if (ui_arm_tap(lv_event_get_target_obj(e), T(S_TAP_AGAIN_RESCAN) /* Tap again: rescan */)) ui_push(&scr_scan, NULL);
}

/* a library swapped in by a rescan while this page lay under another: the numbers it holds mean nothing now, go back to the root level */
static void fill(int page, void *u);
static int sync(tree_t *s)
{
    if (s->lib_gen == library_gen()) return 0;
    s->lib_gen = library_gen();
    s->depth = 0;
    s->path[0] = -1;
    pc_library_level(&g_lib, -1, &s->first, &s->own, &s->nchild);
    s->items = s->nchild + s->own;
    s->skip = 0;
    ui_list_pager(s->list, 0, pages(s), fill, s);          /* the pager's page count and bubble follow the new level */
    return 1;
}

/* one page of rows */
static void fill(int page, void *u)
{
    tree_t *s = u;
    if (sync(s)) page = 0;
    const int inst = (int)(s - s_inst);
    const int cur = mp3_ctl_current(), scope = cur >= 0 ? mp3_ctl_folder() : -3;
    const int lv = level(s);
    char b[160];   /* ICON-B */
    if (g_lib.n) {
        const bool all = lv < 0;
        ui_list_row(s->list, ui_icon_text(b, sizeof b, UI_ICON_L_SHUFFLE, all ? T(S_SHUFFLE_ALL) /* Shuffle all */ : T(S_SHUFFLE_THIS_FOLDER) /* Shuffle this folder */),
                    scope == (all ? -1 : lv) ? UI_SEL : UI_SYS, shuffle_cb, PACK(inst, 0));
    } else if (s->top) ui_list_note(s->list, T(S_NO_SONGS_RESCAN) /* No songs, please rescan */);
    const int from = page * PAGE_ROWS, to = from + PAGE_ROWS < s->items ? from + PAGE_ROWS : s->items;
    int c = -1;                                       /* this level's own songs first, then its sub-folders (the playing order) */
    for (int i = from; i < to; i++) {
        if (i < s->own) {
            const int t = s->first + i;
            const char *title = pc_str(&g_lib, g_lib.tracks[t].title);
            if (crashnet_marked(pc_str(&g_lib, g_lib.tracks[t].path))) {   /* crash safety net: it was playing at a crash; not played by itself, a tap plays it deliberately and clears the mark */
                char two[140];
                snprintf(two, sizeof two, "%s\n%s", title + pc_title_strip(title, s->skip), T(S_CRASH_SKIPPED) /* Crashed last time it played, skipped */);
                lv_obj_t *row = ui_list_row_wrap(s->list, ui_icon_text(b, sizeof b, UI_ICON_L_WARN, two), t == cur ? UI_SEL : UI_PANEL, song_cb, PACK(inst, t));
                if (t != cur) ui_list_row_look(row, UI_LOOK_DIM);
            } else
            ui_list_row(s->list, ui_icon_text(b, sizeof b, UI_ICON_L_MUSIC, title + pc_title_strip(title, s->skip)), t == cur ? UI_SEL : UI_PANEL, song_cb, PACK(inst, t));   /* the spaces after 】 go too */
        } else {
            if (c < 0) c = pc_library_child(&g_lib, lv, i - s->own);
            if (c < 0) break;                          /* the walk ran out (the loader checks the counts; never index with -1) */
            ui_list_row(s->list, ui_icon_text(b, sizeof b, UI_ICON_L_FOLDER, pc_str(&g_lib, g_lib.folders[c].name)),
                        scope >= 0 && pc_library_folder_in(&g_lib, c, scope) ? UI_SEL : UI_PANEL, folder_cb, PACK(inst, c));
            c = pc_library_next_sibling(&g_lib, c, lv);
        }
    }
    if (s->top && lv < 0) {                          /* Rescan: last row of the music root only */
        char rb[64];
        ui_list_row_wrap(s->list, ui_icon_text(rb, sizeof rb, UI_ICON_L_RESCAN, T(S_RESCAN_LIBRARY) /* Rescan library */), UI_SYS, rescan_cb, PACK(inst, 0));   /* its texts are long: wraps (child 0, for ui_arm_tap) */
    }
}

/* the item of the shown level the playing song is in (its own song, or the sub-folder holding it); -1 when it is not here */
static int playing_item(const tree_t *s)
{
    const int cur = mp3_ctl_current();
    if (cur < 0) return -1;
    if (cur >= s->first && cur < s->first + s->own) return cur - s->first;
    int k = s->own;
    for (int c = pc_library_first_child(&g_lib, level(s)); c >= 0; c = pc_library_next_sibling(&g_lib, c, level(s)), k++)
        if (cur >= g_lib.folders[c].first && cur < g_lib.folders[c].first + g_lib.folders[c].sub_count) return k;
    return -1;
}

/* show `folder` as the level; focus_item: the item to land on (its page, in view), -1 = the playing one if it is here, else the first page */
static void level_open(tree_t *s, int folder, int focus_item)
{
    s->lib_gen = library_gen();
    pc_library_level(&g_lib, folder, &s->first, &s->own, &s->nchild);
    s->items = s->nchild + s->own;
    s->skip = 0;
    const char **titles = s->own >= 2 ? psram_malloc((size_t)s->own * sizeof *titles) : NULL;
    if (titles) {
        for (int i = 0; i < s->own; i++) titles[i] = pc_str(&g_lib, g_lib.tracks[s->first + i].title);
        s->skip = pc_title_common_prefix(titles, s->own);
        psram_free(titles);
    }
    if (focus_item < 0) focus_item = playing_item(s);
    const int page = focus_item >= 0 && focus_item < s->items ? focus_item / PAGE_ROWS : 0;
    lv_obj_clean(s->list);
    fill(page, s);
    ui_list_pager(s->list, page, pages(s), fill, s);
    lv_obj_update_layout(s->list);
    if (focus_item >= 0 && focus_item < s->items) {         /* the row to bring into view */
        const int shuffle_rows = g_lib.n ? 1 : 0;
        lv_obj_t *row = lv_obj_get_child(s->list, shuffle_rows + (focus_item - page * PAGE_ROWS));
        if (row) ui_list_show(s->list, row);
    }
}

static void back_up(tree_t *s)
{
    if (s->depth <= 0) { ui_pop(); return; }
    const int from = s->path[s->depth--];
    int f0, own, nch;                                       /* the folder just left is the item to land on: after the level's own songs */
    pc_library_level(&g_lib, level(s), &f0, &own, &nch);
    int k = own;
    for (int c = pc_library_first_child(&g_lib, level(s)); c >= 0 && c != from; c = pc_library_next_sibling(&g_lib, c, level(s))) k++;
    ui_arm_cancel();
    level_open(s, level(s), k);
}
static void back0(void) { back_up(&s_inst[0]); }
static void back1(void) { back_up(&s_inst[1]); }

void scr_tracks_create(lv_obj_t *p, int folder, bool top)
{
    tree_t *s = &s_inst[top ? 0 : 1];
    memset(s, 0, sizeof *s);
    s->top = top;
    s->path[0] = folder >= 0 && folder < g_lib.nfolders ? folder : -1;
    ui_obj_guard(ui_list(p), &s->list);
    ui_back_button_do(p, top ? back0 : back1);
    level_open(s, s->path[0], -1);
}
bool scr_tracks_knob(bool top, int d) { ui_arm_cancel(); return ui_list_knob(s_inst[top ? 0 : 1].list, d); }   /* scrolling cancels the armed rescan */
void scr_tracks_resume(bool top) { ui_list_pager_resume(s_inst[top ? 0 : 1].list); }

static void create(lv_obj_t *p, void *arg) { scr_tracks_create(p, (int)(intptr_t)arg, false); }
static bool on_knob(int d) { return scr_tracks_knob(false, d); }
static void on_resume(void) { scr_tracks_resume(false); }

const ui_screen_t scr_tracks = { .name = "tracks", .create = create, .on_knob = on_knob, .on_resume = on_resume };
