// SPDX-License-Identifier: LGPL-2.1-or-later
/* Tools list toys (spec an internal note section 7): Lunch (shop, then a dish;
 * "Skip today" skips a shop for this visit), Pick a person (one of a group; "No repeat" = nobody twice until everyone has had a
 * turn), Idea generator (one phrase from each of three columns, read together). Lists are glyph packs
 * (spec 2026-11-11-glyphpack-design.md) from /sdcard/lists/ (kind in the pack header); with none of a kind the
 * built-in example from flash (tools/gen/make_lists.py -> app/main/assets/lists/) is used, so it works without a card.
 * Reels: the shared reel module (reel.c); each cell is drawn once into the PSRAM strip with pc_gp_draw565
 * (host-tested). Picks: pc_listpick (host-tested). Internal RAM ~0: built with -msmall-data-limit=0 (statics in
 * PSRAM .bss), strips / index / widgets in PSRAM; an open TF pack holds one unbuffered FILE. */
#include <dirent.h>
#include "pc/pc_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc/pc_glyphpack.h"
#include "pc/pc_listpick.h"
#include "pc/pc_lowmem.h"
#include "pc/pc_upload.h"
#include "cfg.h"
#include "cpu_load.h"
#include "font.h"
#include "glyph_text.h"
#include "ledrim.h"
#include "lowmem.h"
#include "misc/cache/instance/lv_image_cache.h"   /* not in lvgl.h; harmless with LV_CACHE_DEF_SIZE 0 */
#include "psram_alloc.h"
#include "reel.h"
#include "screens.h"
#include "ui_help.h"
#include "ui_theme.h"
#include "ui_kit.h"

enum { LT_LUNCH, LT_PEOPLE, LT_IDEAS, LT_N };
static const uint8_t KIND[LT_N] = { PC_GP_KIND_LUNCH, PC_GP_KIND_PEOPLE, PC_GP_KIND_COLUMNS };
static const char *const BUILTIN_NAME[LT_N] = { "builtin:lunch", "builtin:people", "builtin:ideas" };
extern const uint8_t lunch_zip_start[] asm("_binary_lunch_zip_start");
extern const uint8_t lunch_zip_end[] asm("_binary_lunch_zip_end");
extern const uint8_t people_zip_start[] asm("_binary_people_zip_start");
extern const uint8_t people_zip_end[] asm("_binary_people_zip_end");
extern const uint8_t ideas_zip_start[] asm("_binary_ideas_zip_start");
extern const uint8_t ideas_zip_end[] asm("_binary_ideas_zip_end");

static gt_pack_t *open_any(int tool, const char *path)
{
    if (strncmp(path, "builtin:", 8)) return gt_pack_open(path);
    const uint8_t *a = tool == LT_LUNCH ? lunch_zip_start : tool == LT_PEOPLE ? people_zip_start : ideas_zip_start;
    const uint8_t *b = tool == LT_LUNCH ? lunch_zip_end : tool == LT_PEOPLE ? people_zip_end : ideas_zip_end;
    return gt_pack_open_mem(path, a, (uint32_t)(b - a));
}

static lv_obj_t *box(lv_obj_t *p, int w, int h, lv_color_t c, lv_opa_t opa, int radius)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* ================= a pack's title as a picture (the choosers' rows) ================= */
extern const uint8_t vsample_start[] asm("_binary_sample_zip_start");
extern const uint8_t vsample_end[] asm("_binary_sample_zip_end");
#define TITLE_W 220
#define TITLE_H 30

static gt_pack_t *open_pack_path(const char *path)       /* a file, or one of the four built-in examples (builtin:lunch|people|ideas|vocab) */
{
    if (strncmp(path, "builtin:", 8)) return gt_pack_open(path);
    static const struct { const char *name; const uint8_t *a, *b; } B[] = {
        { "builtin:lunch", lunch_zip_start, lunch_zip_end }, { "builtin:people", people_zip_start, people_zip_end },
        { "builtin:ideas", ideas_zip_start, ideas_zip_end }, { "builtin:vocab", vsample_start, vsample_end } };
    for (size_t i = 0; i < sizeof B / sizeof *B; i++)
        if (!strcmp(path, B[i].name)) return gt_pack_open_mem(path, B[i].a, (uint32_t)(B[i].b - B[i].a));
    return NULL;
}

static void title_strip_deleted(lv_event_t *e)
{
    lv_image_dsc_t *d = lv_event_get_user_data(e);
    lv_image_cache_drop(d);
    psram_free(d);
}

/* node 0 of the pack at 24 px into a TITLE_W x TITLE_H strip on `bg`; NULL when it cannot be done */
static lv_image_dsc_t *title_strip(const char *path, lv_color_t bg)
{
    gt_pack_t *pk = open_pack_path(path);
    if (!pk) return NULL;
    const pc_gp_t *gp = gt_pack_gp(pk);
    lv_image_dsc_t *d = NULL;
    pc_gp_node_t nd;
    int size = pc_gp_size_for_px(gp, 24);
    uint32_t gl = pc_gp_max_glyph_bytes(gp, size);
    uint8_t *gbuf = psram_malloc(gl ? gl : 1);
    uint32_t bytes = TITLE_W * TITLE_H * 2;
    if (gbuf && !pc_gp_node(gp, 0, &nd) && (d = psram_calloc(1, sizeof *d + bytes))) {
        uint16_t *px = (uint16_t *)(d + 1), c = lv_color_to_u16(bg);
        for (uint32_t i = 0; i < TITLE_W * TITLE_H; i++) px[i] = c;
        if (pc_gp_draw565(gp, size, nd.text, nd.len, 1, lv_color_to_u16(UI_FG), px, TITLE_W, TITLE_W, TITLE_H, gbuf, gl) < 0) { psram_free(d); d = NULL; }
        else {
            d->header.magic = LV_IMAGE_HEADER_MAGIC;
            d->header.cf = LV_COLOR_FORMAT_RGB565;
            d->header.w = TITLE_W;
            d->header.h = TITLE_H;
            d->header.stride = TITLE_W * 2;
            d->data_size = bytes;
            d->data = (const uint8_t *)px;
        }
    }
    psram_free(gbuf);
    gt_pack_close(pk);                                    /* the picture is all that is kept: no pack stays open */
    return d;
}

lv_obj_t *pack_title_row(lv_obj_t *list, const char *pack_path, lv_color_t bg, lv_event_cb_t cb, void *user)
{
    lv_image_dsc_t *d = title_strip(pack_path, bg);
    if (!d) {                                             /* no title picture: the base name as plain text */
        const char *slash = strrchr(pack_path, '/'), *base = slash ? slash + 1 : strchr(pack_path, ':') ? strchr(pack_path, ':') + 1 : pack_path;
        return ui_list_row(list, base, bg, cb, user);
    }
    lv_obj_t *row = ui_list_row(list, NULL, bg, cb, user);
    lv_obj_t *img = lv_image_create(row);
    lv_image_set_src(img, d);
    lv_obj_center(img);
    lv_obj_add_event_cb(img, title_strip_deleted, LV_EVENT_DELETE, d);
    return row;
}

/* ================= which lists ================= */
#define MAXLISTS 12
#define MORE_CAP 99                              /* stop examining files once this many did not fit */
typedef struct { int tool; char path[PC_LIST_PATH_MAX]; } lt_arg_t;
static lt_arg_t s_arg[LT_N];                     /* the tool screens' args: stable (a theme change re-creates with them) */
static char s_found[MAXLISTS][PC_LIST_PATH_MAX];
static int s_more;                               /* matching files beyond MAXLISTS: not listed */

/* packs of kind in /sdcard/lists/ (valid ones only), sorted by name */
static int find_packs(int kind)
{
    int n = 0, seen = 0;
    uint32_t t0 = lv_tick_get();
    DIR *d = opendir(PC_LISTS_DIR);
    struct dirent *e;
    s_more = 0;
    while (d && s_more < MORE_CAP && (e = readdir(d))) {
        char p[PC_LIST_PATH_MAX];
        if (pc_list_path(e->d_name, p, sizeof p)) continue;
        gt_pack_t *g = gt_pack_open(p);
        seen++;
        if (!g) continue;
        if (pc_gp_kind(gt_pack_gp(g)) == kind) { if (n < MAXLISTS) snprintf(s_found[n++], PC_LIST_PATH_MAX, "%s", p); else s_more++; }
        gt_pack_close(g);
    }
    if (d) closedir(d);
    qsort(s_found, (size_t)n, PC_LIST_PATH_MAX, (int (*)(const void *, const void *))strcmp);
    printf("APP lists scan kind=%d files=%d match=%d ms=%u\n", kind, seen, n, (unsigned)(lv_tick_get() - t0));
    return n;
}

static const ui_screen_t scr_ltool, scr_lchoose;

/* ================= list chooser ================= */
/* two or more lists of the kind: one scrolling row each with the list's title (a picture, drawn once) */
static struct {
    int tool, n;
    lv_obj_t *list;
} C;

static void choose_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_arg[C.tool].tool = C.tool;
    snprintf(s_arg[C.tool].path, sizeof s_arg[C.tool].path, "%s", s_found[i]);
    printf("APP lists choose %s\n", s_found[i]);
    ui_push(&scr_ltool, &s_arg[C.tool]);
}

static void choose_create(lv_obj_t *p, void *arg)
{
    C.tool = (int)(intptr_t)arg;
    ui_obj_guard(ui_list(p), &C.list);
    for (int i = 0; i < C.n; i++) pack_title_row(C.list, s_found[i], UI_PANEL, choose_cb, (void *)(intptr_t)i);
    if (s_more) {
        char m[64], n[8];
        snprintf(n, sizeof n, s_more >= MORE_CAP ? "%d+" : "%d", s_more);
        snprintf(m, sizeof m, T(S_S_MORE_NOT_SHOWN) /* ...%s more not listed */, n);
        ui_list_note(C.list, m);
    }
    ui_list_entry_row(C.list, T(S_EDIT_LISTS_ON_PC) /* Edit the lists on a computer */, &HELP_LISTS);
    ui_back_button(p);
}

static bool choose_knob(int d) { return ui_list_knob(C.list, d); }
static const ui_screen_t scr_lchoose = { .name = "tools_lists", .create = choose_create, .on_knob = choose_knob };

/* the ring's entry: no list of the kind -> the built-in one; one -> it; more -> the chooser */
void lists_open(int tool)
{
    int n = find_packs(KIND[tool]);
    printf("APP lists tool=%d packs=%d\n", tool, n);
    if (n >= 2) { C.n = n; ui_push(&scr_lchoose, (void *)(intptr_t)tool); return; }
    s_arg[tool].tool = tool;
    snprintf(s_arg[tool].path, sizeof s_arg[tool].path, "%s", n ? s_found[0] : BUILTIN_NAME[tool]);
    ui_push(&scr_ltool, &s_arg[tool]);
}

/* ================= pick-a-person rounds (no repeat), kept until reboot ================= */
#define MAXROUNDS 8
typedef struct { char path[PC_LIST_PATH_MAX]; uint32_t group, n; uint8_t *bits; } round_t;
static round_t s_round[MAXROUNDS];
static int s_round_next;

static uint8_t *round_bits(const char *path, uint32_t group, uint32_t n)
{
    for (int i = 0; i < MAXROUNDS; i++)
        if (s_round[i].bits && s_round[i].group == group && s_round[i].n == n && !strcmp(s_round[i].path, path)) return s_round[i].bits;
    round_t *r = &s_round[s_round_next];
    s_round_next = (s_round_next + 1) % MAXROUNDS;
    psram_free(r->bits);
    r->bits = psram_calloc(1, pc_lp_bitbytes(n ? n : 1));
    snprintf(r->path, sizeof r->path, "%s", path);
    r->group = group;
    r->n = n;
    return r->bits;
}


/* ================= the tool screen ================= */
#define MAXREELS 3
#define CAP1 24            /* stops on a single reel (lunch, people); a larger list gets the result swapped in */
#define CAP3 16            /* stops on each idea reel (one length for the three) */
#define SYM_REEL 32        /* strip symbol = reel * SYM_REEL + stop */
#define REEL_Y (-15)       /* the reel sits between the HEAD pill and the FOOT row: 3 rows of 50 (the 40 px text needs 50) reach 2 px under both, the pills cover the dim rows' ends; the ideas: 3 cells of 44 and 2 gaps of 4 */
#define FRAME_PAD 4
typedef struct {
    uint32_t parent;       /* the node whose children are on this reel */
    int len;
    uint16_t map[CAP1];    /* stop -> child offset */
    uint8_t strip[CAP1];   /* reel * SYM_REEL + stop */
} lreel_t;

static struct {
    int tool;
    char path[PC_LIST_PATH_MAX];
    gt_pack_t *pk;
    const pc_gp_t *gp;
    lv_obj_t *root, *head, *headbtn, *reelbox, *hint, *btn[2], *card, *cardtxt;
    lv_timer_t *card_tmr;
    reel_view_t *rv;
    int nreels, rows, cell_w, cell_h, gap;
    lreel_t r[MAXREELS];
    const uint8_t *strips[MAXREELS];
    int stops[MAXREELS], pick[MAXREELS];
    uint16_t *items, *ids;  /* scratch: eligible children; the combined idea text */
    uint32_t items_cap, ids_cap;
    uint8_t *gbuf;          /* one glyph bitmap */
    uint32_t gcap;
    uint16_t fg;
    int phase;              /* lunch: 0 shops, 1 the dishes of shop */
    int shop, group;
    uint8_t *skip;          /* lunch: shops skipped this visit (Skip today) */
    bool spinning, spun, norep, new_round;
    int spins, build_ms;
    cpu_snap_t c0;
    float load[2];
} S;

static uint32_t child_node(uint32_t parent, int k)
{
    pc_gp_node_t n;
    return pc_gp_node(S.gp, parent, &n) || k < 0 || k >= n.nchild ? 0 : n.first_child + (uint32_t)k;
}
static int nchild(uint32_t node) { pc_gp_node_t n; return pc_gp_node(S.gp, node, &n) ? 0 : n.nchild; }
static bool builtin(void) { return !strncmp(S.path, "builtin:", 8); }

/* a cell: the item's text at 40 px on one line when it fits, else 24 px on as many lines as the cell holds */
static void cell_draw(void *user, int sym, uint16_t *dst, int stride, int w, int h)
{
    (void)user;
    int ri = sym / SYM_REEL, k = sym % SYM_REEL;
    if (ri >= S.nreels || k >= S.r[ri].len) return;
    pc_gp_node_t nd;
    if (pc_gp_node(S.gp, child_node(S.r[ri].parent, S.r[ri].map[k]), &nd)) return;
    int big = pc_gp_size_for_px(S.gp, ui_scale_pct(40, 130)), small = pc_gp_size_for_px(S.gp, ui_scale_pct(24, 130));
    pc_gp_size_t sb, ss;
    pc_gp_size(S.gp, big, &sb);
    pc_gp_size(S.gp, small, &ss);
    pc_gp_line_t L[1];
    int pad = 10, iw = w - 2 * pad, size = small, lines = h / (ss.line_h ? ss.line_h : 1);
    if (sb.line_h <= h && pc_gp_layout(S.gp, big, nd.text, nd.len, iw, 1, L) == 1 && !L[0].ellipsis) { size = big; lines = 1; }
    if (lines < 1) lines = 1;
    int rc = pc_gp_draw565(S.gp, size, nd.text, nd.len, lines, S.fg, dst + pad, stride, iw, h, S.gbuf, S.gcap);
    if (rc < 0) printf("APP lists cell draw rc=%d\n", rc);
}

static void buttons(const char *a, const char *b)       /* the answer choice row: a word per cell, NULL hides the cell */
{
    for (int i = 0; i < 2; i++) {
        const char *t = i ? b : a;
        if (!S.btn[i]) continue;
        if (t) { ui_choice_set(S.btn[i], t, UI_LOOK_NORMAL); lv_obj_remove_flag(S.btn[i], LV_OBJ_FLAG_HIDDEN); }
        else lv_obj_add_flag(S.btn[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (a || b) lv_obj_add_flag(S.hint, LV_OBJ_FLAG_HIDDEN);
}

static void hint_text(const char *first, bool empty)       /* the way to edit the lists is told only when there is nothing to pick */
{
    if (empty) lv_label_set_text_fmt(S.hint, "%s\n%s", first, T(S_EDIT_SETTINGS_PAGE_LISTS) /* Edit on the settings web page under "Lists" */);
    else lv_label_set_text(S.hint, first);
    buttons(NULL, NULL);
    lv_obj_remove_flag(S.hint, LV_OBJ_FLAG_HIDDEN);
}

static uint32_t group_node(void) { return child_node(0, S.group); }
static uint8_t *drawn_bits(void) { return round_bits(S.path, (uint32_t)S.group, (uint32_t)nchild(group_node())); }

static void people_buttons(void)
{
    static char b[48];
    ui_arm_cancel();                                    /* the label is rewritten below: nothing stays armed with other text */
    int all = pc_lp_count(S.gp, group_node(), NULL), left = pc_lp_left(S.gp, group_node(), drawn_bits());
    snprintf(b, sizeof b, T(S_RESET_D_D) /* Again %d/%d */, S.norep ? left : all, all);
    buttons(S.norep ? T(S_NO_REPEAT) /* No repeat ✓ */ : T(S_REPEATS_OK) /* Repeats allowed */, b);
}

static void head_set(uint32_t node) { glyph_text_set_node(S.head, S.pk, node, 24, 1); lv_obj_center(S.head); }

/* the reels of the current phase: strips of the pickable children (weight > 0, not skipped) */
static bool build_reels(void)
{
    uint32_t t0 = lv_tick_get();
    if (S.rv) { reel_destroy(S.rv); S.rv = NULL; lv_obj_clean(S.reelbox); }
    int top = nchild(0);
    if (S.tool == LT_IDEAS) S.nreels = top < MAXREELS ? top : MAXREELS;
    else S.nreels = 1;
    for (int i = 0; i < S.nreels; i++) {
        lreel_t *r = &S.r[i];
        int n;
        if (S.tool == LT_LUNCH && S.phase == 0) { r->parent = 0; n = pc_lp_shops(S.gp, S.skip, S.items, (int)S.items_cap); }
        else {
            r->parent = S.tool == LT_LUNCH ? child_node(0, S.shop) : S.tool == LT_PEOPLE ? group_node() : child_node(0, i);
            n = pc_lp_items(S.gp, r->parent, NULL, S.items, (int)S.items_cap);
        }
        r->len = S.tool == LT_IDEAS ? pc_lp_strip_fixed(ui_rng(), S.items, n, CAP3, r->map) : pc_lp_strip(ui_rng(), S.items, n, CAP1, 12, r->map);
        if (r->len < 1) { S.nreels = 0; return false; }
        for (int k = 0; k < r->len; k++) r->strip[k] = (uint8_t)(i * SYM_REEL + k);
        S.strips[i] = r->strip;
    }
    if (S.nreels < 1) return false;
    const reel_desc_t d = { .nreels = S.nreels, .rows = S.rows, .cell_w = S.cell_w, .cell_h = S.cell_h, .gap = S.gap,
                            .vertical = S.tool == LT_IDEAS, .len = S.r[0].len, .strips = S.strips, .draw_cell = cell_draw,
                            .bg = ui_theme()->panel2 };
    S.rv = reel_create(S.reelbox, &d);
    if (!S.rv) return false;
    lv_obj_center(reel_obj(S.rv));
    for (int i = 0; i < S.nreels; i++) S.stops[i] = (int)pc_rng_below(ui_rng(), (uint32_t)S.r[i].len);
    reel_show(S.rv, S.stops);
    S.build_ms = (int)(lv_tick_get() - t0);
    printf("APP lists reels tool=%d phase=%d n=%d len=%d build_ms=%d\n", S.tool, S.phase, S.nreels, S.r[0].len, S.build_ms);
    return true;
}

static void stat_print(const char *what)
{
    int fr, ms, sa, sm;
    reel_stat(S.rv, &fr, &ms, &sa, &sm);
    printf("APP lists %s tool=%d phase=%d picks=%d,%d,%d spins=%d frames=%d ms=%d step_avg=%d step_max=%d load0=%.0f load1=%.0f build_ms=%d lowmem=%d\n",
           what, S.tool, S.phase, S.pick[0], S.pick[1], S.pick[2], S.spins, fr, ms, sa, sm, (double)S.load[0], (double)S.load[1], S.build_ms, lowmem_level());
}

/* idea: the three phrases read together, on a card over the reels */
static void card_show(void)
{
    uint32_t n = 0;
    for (int i = 0; i < S.nreels; i++) {
        pc_gp_node_t nd;
        if (pc_gp_node(S.gp, child_node(S.r[i].parent, S.pick[i]), &nd)) continue;
        for (uint32_t k = 0; k < nd.len && n < S.ids_cap; k++) if (nd.text[k] != PC_GP_NL) S.ids[n++] = nd.text[k];
    }
    const int P40 = ui_scale_pct(40, 130), P24 = ui_scale_pct(24, 130);
    int big = pc_gp_size_for_px(S.gp, P40), px = P40, lines = 3;
    pc_gp_line_t L[3];
    int nl = pc_gp_layout(S.gp, big, S.ids, n, lv_obj_get_content_width(S.cardtxt), 3, L);
    if (nl == 3 && L[2].ellipsis) { px = P24; lines = 5; }
    glyph_text_set(S.cardtxt, S.pk, S.ids, n, px, lines);
    lv_obj_center(S.cardtxt);
    lv_obj_remove_flag(S.card, LV_OBJ_FLAG_HIDDEN);
}
static void card_tmr_cb(lv_timer_t *t) { (void)t; S.card_tmr = NULL; card_show(); }
static void card_hide(void)
{
    if (S.card_tmr) { lv_timer_delete(S.card_tmr); S.card_tmr = NULL; }
    if (S.card) lv_obj_add_flag(S.card, LV_OBJ_FLAG_HIDDEN);
}

static void on_landed(void *u, int reel) { (void)u; (void)reel; if (ui_screen_on()) ui_haptic_tick(); }

static void on_done(void *u)
{
    (void)u;
    cpu_snap_t b;
    cpu_snap(&b);
    cpu_load(&S.c0, &b, S.load);
    S.spinning = false;
    S.spun = true;
    S.spins++;
    if (ui_screen_on()) ui_haptic_strong();
    ledrim_flash(ui_theme()->a1, 600);
    if (S.tool == LT_LUNCH && S.phase == 0) buttons(T(S_NOT_TODAY) /* Skip today */, T(S_THIS_ONE_2) /* This one */);
    else if (S.tool == LT_LUNCH) buttons(T(S_OTHER_SHOP) /* Another shop */, T(S_AGAIN) /* Draw another dish */);
    else if (S.tool == LT_PEOPLE) { people_buttons(); if (S.new_round) ledrim_chase(ui_theme()->a2, 1); }
    else if (lowmem_level() >= PC_LOWMEM_STILL) card_show();
    else { S.card_tmr = lv_timer_create(card_tmr_cb, 450, NULL); lv_timer_set_repeat_count(S.card_tmr, 1); }
    stat_print("result");
}

static void spin(void)
{
    if (!S.rv) return;
    if (S.spinning) { reel_stop_early(S.rv); return; }
    card_hide();
    S.new_round = false;
    for (int i = 0; i < S.nreels; i++) {
        lreel_t *r = &S.r[i];
        int k;
        if (S.tool == LT_LUNCH && S.phase == 0) k = pc_lp_pick_shop(ui_rng(), S.gp, S.skip);
        else if (S.tool == LT_PEOPLE) { uint8_t *d = drawn_bits(); k = d ? pc_lp_draw(ui_rng(), S.gp, r->parent, d, S.norep, &S.new_round) : -1; }
        else k = pc_lp_pick(ui_rng(), S.gp, r->parent, NULL);
        if (k < 0) return;
        bool rep = false;
        int slot = pc_lp_slot(ui_rng(), r->map, r->len, (uint16_t)k, reel_top(S.rv, i), S.rows, &rep);
        if (rep) reel_redraw_cell(S.rv, i, slot);
        S.pick[i] = k;
        S.stops[i] = (slot - S.rows / 2 + r->len) % r->len;     /* the result in the middle row */
    }
    S.spinning = true;
    buttons(NULL, NULL);
    lv_obj_add_flag(S.hint, LV_OBJ_FLAG_HIDDEN);
    cpu_snap(&S.c0);
    const reel_cbs_t cb = { .reel_landed = on_landed, .done = on_done };
    reel_spin(S.rv, S.stops, &cb);
}

static void lunch_phase(int phase)
{
    S.phase = phase;
    head_set(phase ? child_node(0, S.shop) : 0);
    if (!build_reels()) printf("APP lists lunch nothing to pick\n");
}

static void btn_press(int i)
{
    if (S.spinning || !S.btn[i] || lv_obj_has_flag(S.btn[i], LV_OBJ_FLAG_HIDDEN)) return;
    printf("APP lists btn %d tool=%d phase=%d\n", i, S.tool, S.phase);
    if (S.tool == LT_LUNCH && S.phase == 0) {
        if (i == 0) {                                     /* Skip today: without this shop, spin again */
            pc_lp_setbit(S.skip, (uint32_t)S.pick[0]);
            if (!pc_lp_shop_count(S.gp, S.skip)) {
                memset(S.skip, 0, pc_lp_bitbytes((uint32_t)nchild(0) + 1));
                ui_push_message(T(S_NONE_LEFT_ALL_SHOPS_ARE_BACK) /* No shop wanted? All are back */);
            }
            lunch_phase(0);
        } else { S.shop = S.pick[0]; lunch_phase(1); }   /* This one: now its dishes */
        spin();
    } else if (S.tool == LT_LUNCH) {
        if (i == 0) lunch_phase(0);                       /* Another shop */
        spin();
    } else if (S.tool == LT_PEOPLE) {
        if (i == 0) { S.norep = !S.norep; cfg_set_int("pick_norep", S.norep); }
        else {
            if (!ui_arm_tap(S.btn[1], T(S_TAP_AGAIN) /* Tap again to restart */)) return;   /* the first tap only asks */
            uint8_t *d = drawn_bits();
            if (d) memset(d, 0, pc_lp_bitbytes((uint32_t)nchild(group_node())));
            ledrim_flash(ui_theme()->a2, 400);
        }
        people_buttons();
    }
}
static void btn_cb(lv_event_t *e) { btn_press((int)(intptr_t)lv_event_get_user_data(e)); }

/* pick a person: the group is an axis: a tap, or the knob while it is borrowed, goes to the next (previous) group that has someone to draw */
static bool group_step(int axis, int d)
{
    (void)axis;
    int n = nchild(0), g = S.group, dir = d < 0 ? -1 : 1;
    if (S.spinning || n < 2) return false;
    for (int left = d < 0 ? -d : d; left > 0; left--)
        for (int k = 1; k <= n; k++) {
            int c = ((g + dir * k) % n + n) % n;
            if (pc_lp_count(S.gp, child_node(0, c), NULL)) { g = c; break; }
        }
    S.group = g;
    cfg_set_int("pick_group", S.group);
    head_set(group_node());
    build_reels();
    if (S.spun) people_buttons();
    if (ui_screen_on()) ui_haptic_tick();
    printf("APP lists group=%d\n", S.group);
    return true;
}
static void tap_spin(void) { spin(); }
static bool tool_knob(int d) { if (!ui_axis_knob(d)) spin(); return true; }

static void tool_create(lv_obj_t *p, void *arg)
{
    const lt_arg_t *a = arg;
    S.tool = a->tool;
    snprintf(S.path, sizeof S.path, "%s", a->path);
    S.root = p;
    S.rv = NULL;
    S.card = S.cardtxt = S.headbtn = NULL;
    S.btn[0] = S.btn[1] = NULL;
    S.card_tmr = NULL;
    ui_timer_guard(p, &S.card_tmr);
    S.spinning = S.spun = false;
    S.phase = S.spins = S.shop = 0;
    S.pick[0] = S.pick[1] = S.pick[2] = -1;
    ledrim_create(p);
    S.pk = open_any(S.tool, S.path);
    if (!S.pk && !builtin()) {                            /* the TF list went bad: the example instead */
        snprintf(S.path, sizeof S.path, "%s", BUILTIN_NAME[S.tool]);
        S.pk = open_any(S.tool, S.path);
    }
    S.gp = gt_pack_gp(S.pk);
    uint32_t mx = 0, gl = 0, tl = 0;
    for (int k = 0; S.gp && k < nchild(0); k++) {
        int c = nchild(child_node(0, k));
        if ((uint32_t)c > mx) mx = (uint32_t)c;
        for (int j = 0; j < c; j++) { pc_gp_node_t nd; if (!pc_gp_node(S.gp, child_node(child_node(0, k), j), &nd) && nd.len > tl) tl = nd.len; }
    }
    if (S.gp && (uint32_t)nchild(0) > mx) mx = (uint32_t)nchild(0);
    for (int s = 0; S.gp && s < pc_gp_nsizes(S.gp); s++) if (pc_gp_max_glyph_bytes(S.gp, s) > gl) gl = pc_gp_max_glyph_bytes(S.gp, s);
    S.items_cap = mx + 1;
    S.ids_cap = 3 * tl + 1;
    S.gcap = gl;
    S.items = psram_malloc(S.items_cap * 2);
    S.ids = psram_malloc(S.ids_cap * 2);
    S.gbuf = psram_malloc(gl ? gl : 1);
    S.skip = psram_calloc(1, pc_lp_bitbytes((uint32_t)nchild(0) + 1));
    S.fg = lv_color_to_u16(UI_FG);
    S.norep = cfg_int("pick_norep", 1) != 0;
    S.group = 0;
    if (S.gp && S.tool == LT_PEOPLE) {
        int g = cfg_int("pick_group", 0);
        S.group = g >= 0 && g < nchild(0) && pc_lp_count(S.gp, child_node(0, g), NULL) ? g : 0;
        for (int k = 0; k < nchild(0) && !pc_lp_count(S.gp, group_node(), NULL); k++) S.group = k;
    }
    if (S.tool == LT_IDEAS) { S.rows = 1; S.cell_w = ui_scale_pct(284, 112); S.cell_h = ui_scale_pct(44, 130); S.gap = ui_scale_pct(4, 200); }   /* the square panel's reel is larger (SQ2) */
    else { S.rows = 3; S.cell_w = ui_scale_pct(264, 122); S.cell_h = ui_scale_pct(50, 130); S.gap = 0; }
    int rw = S.cell_w, rh = S.tool == LT_IDEAS ? 3 * S.cell_h + 2 * S.gap : S.rows * S.cell_h;
    lv_obj_t *frame = box(p, rw + 2 * FRAME_PAD, rh + 2 * FRAME_PAD, UI_PANEL, LV_OPA_COVER, 14);
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, REEL_Y);
    S.reelbox = box(p, rw, rh, UI_PANEL, LV_OPA_TRANSP, 0);
    lv_obj_align(S.reelbox, LV_ALIGN_CENTER, 0, REEL_Y);
    if (S.tool != LT_IDEAS) {                             /* the middle row is the pick: dim the others, frame it */
        for (int k = 0; k < 2; k++) {
            lv_obj_t *dm = box(p, rw, S.cell_h, UI_BG, LV_OPA_50, 0);
            lv_obj_align(dm, LV_ALIGN_CENTER, 0, REEL_Y + (k ? 1 : -1) * S.cell_h);
        }
        lv_obj_t *fr = box(p, rw + 8, S.cell_h + 6, UI_BG, LV_OPA_TRANSP, 10);
        lv_obj_set_style_border_color(fr, UI_A1, 0);
        lv_obj_set_style_border_width(fr, 3, 0);
        lv_obj_align(fr, LV_ALIGN_CENTER, 0, REEL_Y);
    }
    bool groups = S.gp && S.tool == LT_PEOPLE && nchild(0) > 1;
    if (groups) S.headbtn = ui_axis_button(p, UI_SLOT_HEAD, 0, group_step, NULL);     /* the group: a tap goes on, the knob browses */
    else {                                                /* the pack's title in the HEAD slot, not a control: a transparent holder of the slot's size */
        S.headbtn = box(p, ui_slot_w(UI_SLOT_HEAD, 40), 40, UI_BG, LV_OPA_TRANSP, 0);
        lv_obj_align(S.headbtn, LV_ALIGN_CENTER, 0, ui_slot_y(UI_SLOT_HEAD));
    }
    S.head = glyph_text_create(S.headbtn);
    S.hint = ui_text_dim(p, UI_SLOT_FOOT, NULL);
    if (builtin()) lv_label_set_text(ui_text_dim(p, UI_SLOT_TITLE, NULL), T(S_EXAMPLE) /* Built-in sample */);   /* no list of this kind on the card: say so, small, at the top */
    if (S.tool != LT_IDEAS) ui_choice_row(p, UI_SLOT_FOOT, 2, btn_cb, S.btn);
    else {
        S.card = box(p, S.cell_w + 16, rh + 2 * FRAME_PAD, UI_PANEL, LV_OPA_COVER, 18);
        lv_obj_set_style_border_color(S.card, UI_A1, 0);
        lv_obj_set_style_border_width(S.card, 3, 0);
        lv_obj_align(S.card, LV_ALIGN_CENTER, 0, REEL_Y);
        S.cardtxt = glyph_text_create(S.card);
        lv_obj_set_width(S.cardtxt, S.cell_w - 12);
        lv_obj_center(S.cardtxt);
        lv_obj_add_flag(S.card, LV_OBJ_FLAG_HIDDEN);
    }
    ui_back_button(p);
    ui_page_tap(p, tap_spin);
    if (!S.gp || !S.items || !S.ids || !S.gbuf || !S.skip) { hint_text(T(S_CANNOT_OPEN_THE_LIST) /* The list cannot be opened */, true); return; }
    if (S.tool == LT_PEOPLE) head_set(group_node());
    else head_set(0);
    if (!build_reels()) { hint_text(T(S_NOTHING_TO_PICK_OR_OUT_OF_MEMORY) /* Nothing to draw, or out of memory */, true); return; }
    if (S.tool == LT_LUNCH) hint_text(T(S_TAP_OR_TURN_PICK_A_SHOP) /* Tap or turn the knob to draw a shop */, false);
    else if (S.tool == LT_PEOPLE) hint_text(nchild(0) > 1 ? T(S_TAP_DRAW_TAP_THE_GROUP_TO_CHANGE) /* Tap to draw a person · tap the group name to switch groups */ : T(S_TAP_OR_TURN_DRAW_SOMEONE) /* Tap or turn the knob to draw a person */, false);
    else hint_text(T(S_TAP_OR_TURN_AN_IDEA) /* Tap or turn the knob for an idea */, false);
}

static void tool_destroy(void)
{
    card_hide();
    reel_destroy(S.rv);
    S.rv = NULL;
    lv_obj_clean(S.root);                     /* widgets before the pack they point into */
    S.card = S.cardtxt = S.headbtn = S.head = S.hint = NULL;
    S.btn[0] = S.btn[1] = NULL;
    ledrim_destroy();
    gt_pack_close(S.pk);
    S.pk = NULL;
    S.gp = NULL;
    psram_free(S.items); psram_free(S.ids); psram_free(S.gbuf); psram_free(S.skip);
    S.items = S.ids = NULL;
    S.gbuf = S.skip = NULL;
    S.spinning = false;
}
static const ui_screen_t scr_ltool = { .name = "tools_list", .create = tool_create, .destroy = tool_destroy, .on_knob = tool_knob, .content = true };

/* console (tools_cmd in scr_tools.c passes these on while a list tool is on top): spin | stop | btn 0|1 | group | stat */
bool lists_cmd(const char *a)
{
    int x;
    if (ui_top() == &scr_lchoose && sscanf(a, "choose %d", &x) == 1) {      /* the chooser: open list x (0 = first) */
        if (x < 0 || x >= C.n) return false;
        s_arg[C.tool].tool = C.tool;
        snprintf(s_arg[C.tool].path, sizeof s_arg[C.tool].path, "%s", s_found[x]);
        ui_push(&scr_ltool, &s_arg[C.tool]);
        return true;
    }
    if (ui_top() != &scr_ltool) return false;
    if (!strcmp(a, "roll") || !strcmp(a, "spin")) spin();
    else if (!strcmp(a, "stop")) { if (S.spinning) reel_stop_early(S.rv); }
    else if (sscanf(a, "btn %d", &x) == 1 && x >= 0 && x < 2) btn_press(x);
    else if (!strcmp(a, "group")) group_step(0, 1);
    else if (!strcmp(a, "stat")) { stat_print("stat"); ui_fps_stat(); }
    else return false;
    return true;
}
