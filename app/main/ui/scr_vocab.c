// SPDX-License-Identifier: LGPL-2.1-or-later
/* Vocabulary quiz (spec an internal note), in Tools, runs while the music
 * plays, no reboot. A deck is a glyph pack of kind 4 (/sdcard/vocab/NAME.zip, or the built-in sample from flash:
 * tools/gen/make_vocab.py -> app/main/assets/vocab/sample.zip); words and meanings are glyph-ID sequences, never text.
 * Progress (Leitner boxes per card, pc_vocab, host-tested) is one small file per deck next to it
 * (/sdcard/vocab/<deck>.prg; the sample's is builtin.prg), written as .new then renamed, 5 s after the last answer
 * and when the quiz closes.
 * Screens: home (deck title, stats, Start / direction / Deck), quiz (the prompt across the middle, four answers in
 * the four quadrants of the round screen), deck chooser. Internal RAM ~0: built with -msmall-data-limit=0 (statics
 * in PSRAM .bss), deck index, progress and widgets in PSRAM; an open TF deck holds one unbuffered FILE. */
#include <dirent.h>
#include "pc/pc_paths.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_timer.h"
#include "pc/pc_glyphpack.h"
#include "pc/pc_lowmem.h"
#include "pc/pc_upload.h"
#include "pc/pc_vocab.h"
#include "cfg.h"
#include "clock_svc.h"
#include "font.h"
#include "glyph_text.h"
#include "ledrim.h"
#include "lowmem.h"
#include "psram_alloc.h"
#include "screens.h"
#include "ui_theme.h"
#include "ui_axis.h"
#include "ui_kit.h"
#include "ui_ring.h"

#define VOCAB_DIR    PC_VOCAB_DIR
#define BUILTIN      "builtin:vocab"
#define SAVE_MS      5000         /* after the last answer ... */
#define SAVE_MAX_MS  30000        /* ... but no later than this after the first unsaved one */
#define NEXT_MS      900          /* a right answer: the next question after this */
#define NEW_DEFAULT  10           /* new cards a day (cfg vocab_new) */
#define C_RIGHT      ui_pal(UI_PAL_quiz)[0]
#define C_WRONG      ui_pal(UI_PAL_quiz)[1]
extern const uint8_t vsample_start[] asm("_binary_sample_zip_start");
extern const uint8_t vsample_end[] asm("_binary_sample_zip_end");

/* ================= the open deck and its progress ================= */
static struct {
    char path[PC_LIST_PATH_MAX], prg[PC_LIST_PATH_MAX + 8];
    gt_pack_t *pk;
    const pc_gp_t *gp;
    uint32_t n, usable, hash;
    uint8_t *skip;
    pc_vocab_prog_t prog;
    int load_rc;            /* the last progress load: 0, -ENOENT (none yet), <0 reset */
    bool dirty, fell_back;  /* fell_back: the configured deck did not open and the built-in sample stands in for it */
    uint32_t dirty_since;
    lv_timer_t *save_tmr;
    int saves, save_ms;
} V;

static bool builtin(void) { return !strcmp(V.path, BUILTIN); }

static uint16_t today(bool *ok)
{
    bool v = clock_time_valid();
    if (ok) *ok = v;
    if (!v) return V.prog.last_day;
    return pc_vocab_day(clock_now_utc() + (int64_t)clock_tz_min() * 60);
}

static void prg_path(void)
{
    if (builtin()) { snprintf(V.prg, sizeof V.prg, VOCAB_DIR "/builtin.prg"); return; }
    snprintf(V.prg, sizeof V.prg, "%s", V.path);
    char *dot = strrchr(V.prg, '.');
    if (dot && !strcmp(dot, ".zip")) strcpy(dot, ".prg");
    else strncat(V.prg, ".prg", sizeof V.prg - strlen(V.prg) - 1);
}

static int prog_load(void)
{
    if (!g_sd_ok) return -ENOENT;
    FILE *f = fopen(V.prg, "rb");
    if (!f) return -ENOENT;
    setvbuf(f, NULL, _IONBF, 0);
    size_t want = pc_vocab_file_bytes(V.n) + 4;      /* one more: a longer file is damaged too */
    uint8_t *b = psram_malloc(want);
    size_t got = b ? fread(b, 1, want, f) : 0;
    fclose(f);
    int rc = b ? pc_vocab_load(&V.prog, b, got, V.hash, V.n) : -ENOMEM;
    psram_free(b);
    if (rc && rc != -ENOMEM) {                        /* damaged or another deck: keep it as .bad, start again */
        char bad[sizeof V.prg + 4];
        snprintf(bad, sizeof bad, "%s.bad", V.prg);
        remove(bad);
        rename(V.prg, bad);
        V.dirty = true;
    }
    return rc;
}

static void prog_save(void)
{
    if (!V.gp || !V.dirty || !g_sd_ok) return;
    int64_t t0 = esp_timer_get_time();
    size_t n = pc_vocab_file_bytes(V.n);
    uint8_t *b = psram_malloc(n);
    if (!b) { printf("APP vocab save nomem\n"); return; }
    pc_vocab_save(&V.prog, b, n);
    mkdir(VOCAB_DIR, 0777);
    char tmp[sizeof V.prg + 4];
    snprintf(tmp, sizeof tmp, "%s.new", V.prg);
    FILE *f = fopen(tmp, "wb");
    bool ok = false;
    if (f) {
        setvbuf(f, NULL, _IONBF, 0);
        ok = fwrite(b, 1, n, f) == n;
        ok = fclose(f) == 0 && ok;
    }
    psram_free(b);
    if (ok) { remove(V.prg); ok = rename(tmp, V.prg) == 0; }
    else remove(tmp);
    V.save_ms = (int)((esp_timer_get_time() - t0) / 1000);
    if (ok) { V.dirty = false; V.saves++; }
    printf("APP vocab save %s bytes=%u ok=%d ms=%d\n", V.prg, (unsigned)n, ok, V.save_ms);
}

static void save_cb(lv_timer_t *t) { (void)t; V.save_tmr = NULL; prog_save(); }
static void save_soon(void)
{
    if (!V.dirty) V.dirty_since = lv_tick_get();
    V.dirty = true;
    if (V.save_tmr) { if (lv_tick_elaps(V.dirty_since) < SAVE_MAX_MS) lv_timer_reset(V.save_tmr); }
    else { V.save_tmr = lv_timer_create(save_cb, SAVE_MS, NULL); lv_timer_set_repeat_count(V.save_tmr, 1); }
}

static void deck_close(void)
{
    if (V.save_tmr) { lv_timer_delete(V.save_tmr); V.save_tmr = NULL; }
    prog_save();
    gt_pack_close(V.pk);
    V.pk = NULL;
    V.gp = NULL;
    psram_free(V.skip);
    psram_free(V.prog.cards);
    V.skip = NULL;
    V.prog.cards = NULL;
    V.prog.ncards = 0;
    V.n = V.usable = 0;
    V.fell_back = false;
}

static bool deck_open(const char *path)
{
    deck_close();
    int64_t t0 = esp_timer_get_time();
    snprintf(V.path, sizeof V.path, "%s", path);
    V.pk = builtin() ? gt_pack_open_mem(BUILTIN, vsample_start, (uint32_t)((uintptr_t)vsample_end - (uintptr_t)vsample_start)) : gt_pack_open(V.path);
    V.gp = gt_pack_gp(V.pk);
    V.n = pc_vocab_count(V.gp);
    if (!V.gp || !V.n || V.n > PC_VOCAB_MAX_CARDS) {
        printf("APP vocab open %s failed (cards=%u)\n", path, (unsigned)V.n);
        gt_pack_close(V.pk);
        V.pk = NULL; V.gp = NULL; V.n = 0;
        return false;
    }
    V.skip = psram_calloc(1, pc_vocab_bitbytes(V.n));
    V.prog.cards = psram_calloc(V.n, sizeof(pc_vocab_card_t));
    if (!V.skip || !V.prog.cards) { printf("APP vocab open nomem\n"); deck_close(); return false; }
    V.usable = pc_vocab_skipbits(V.gp, V.skip, pc_vocab_bitbytes(V.n));
    V.hash = pc_vocab_hash(V.gp);
    pc_vocab_reset(&V.prog, V.hash, V.n);
    V.dirty = false;
    prg_path();
    V.load_rc = prog_load();
    printf("APP vocab open %s cards=%u usable=%u hash=%08x index=%u B progress=%s rc=%d ms=%d\n", V.path, (unsigned)V.n,
           (unsigned)V.usable, (unsigned)V.hash, (unsigned)pc_gp_block_bytes(V.gp), V.prg, V.load_rc,
           (int)((esp_timer_get_time() - t0) / 1000));
    return true;
}

/* the deck to use: cfg vocab_deck if it still opens, else the built-in sample */
static void deck_open_current(void)
{
    const char *p = cfg_str("vocab_deck", BUILTIN);
    if (V.gp && (!strcmp(V.path, p) || V.fell_back)) return;   /* an open deck stays: closing it frees the pack under the home page's title */
    if (!deck_open(p) && strcmp(p, BUILTIN) && deck_open(BUILTIN)) V.fell_back = true;
}

/* ================= quiz ================= */
static struct { lv_obj_t *root, *ring, *title, *tag; bool alive; } H;   /* tag: the label in TITLE (example deck / clock note) */
static void home_stats(void);

enum { DIR_EN_ZH, DIR_ZH_EN, DIR_MIX, DIR_N };
static const char *dir_name(int d) { return d == DIR_ZH_EN ? T(S_ZH_EN) /* ZH→EN */ : d == DIR_MIX ? T(S_MIXED) /* Mixed */ : T(S_EN_ZH) /* EN→ZH */; }

#define PROMPT_W ui_scale_pct(296, 103)         /* the square panel's prompt band is larger (SQ2) */
#define PROMPT_PX ui_scale_pct(40, 130)
#define PROMPT_PX2 ui_scale_pct(24, 130)
static struct {
    lv_obj_t *root, *top, *band, *prompt, *extra, *box[4], *txt[4];
    lv_timer_t *next_tmr;
    pc_vocab_session_t ses;
    int32_t card;
    int why, field;           /* field: what the answers show (1 meaning: EN->ZH, 0 word) */
    uint32_t choice[4];
    int nchoice, right_pos, picked;
    bool answered, clock_ok;
    uint16_t day;
    int q, build_ms, build_max;
} Q;

static void q_top_text(void)
{
    const char *why = Q.why == PC_VN_NEW ? T(S_NEW_2) /* New */ : Q.why == PC_VN_DUE ? T(S_REVIEW) /* Review */
                    : Q.why == PC_VN_RELEARN ? T(S_AGAIN_2) /* Once more */ : T(S_PRACTICE) /* Practice */;
    if (Q.answered && Q.picked != Q.right_pos) lv_label_set_text(Q.top, T(S_TAP_TO_GO_ON) /* Tap to continue */);
    else lv_label_set_text_fmt(Q.top, T(S_S_RUN_U) /* %s · streak %u */, why, (unsigned)Q.ses.run);
}

static void entry_text(uint32_t card, int field, const uint16_t **t, uint16_t *n)
{
    pc_vocab_entry_t e;
    *t = NULL; *n = 0;
    if (pc_vocab_entry(V.gp, card, &e)) return;
    *t = field ? e.meaning : e.word;
    *n = field ? e.meaning_len : e.word_len;
}

/* the prompt at 40 px on one line when it fits, else 24 px on two */
static void set_prompt(const uint16_t *t, uint16_t n, bool small)
{
    int px = PROMPT_PX2, lines = 2;
    pc_gp_line_t L[1];
    int big = pc_gp_size_for_px(V.gp, PROMPT_PX);
    static uint16_t one[96];                     /* the text without the forced break between two senses (make_vocab.py
                                                    breaks there for the narrow answer boxes): for one 40 px line */
    uint16_t k = 0;
    for (uint16_t i = 0; i < n && k < sizeof one / sizeof one[0]; i++) if (t[i] != PC_GP_NL) one[k++] = t[i];
    bool whole = k == n || k + 1 == n;
    if (!small && whole && pc_gp_layout(V.gp, big, one, k, PROMPT_W, 1, L) == 1 && !L[0].ellipsis) { t = one; n = k; px = PROMPT_PX; lines = 1; }
    /* PROMPT_W, not the widget's width: right after create its layout is not computed yet */
    glyph_text_set(Q.prompt, V.pk, t, n, px, small ? 1 : lines);
}

static void band_layout(bool with_extra)
{
    if (with_extra) {
        lv_obj_remove_flag(Q.extra, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(Q.prompt, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_align(Q.extra, LV_ALIGN_BOTTOM_MID, 0, -4);
    } else {
        lv_obj_add_flag(Q.extra, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(Q.prompt);
    }
}

static void q_next(void)
{
    if (Q.next_tmr) { lv_timer_delete(Q.next_tmr); Q.next_tmr = NULL; }
    int64_t t0 = esp_timer_get_time();
    Q.day = today(&Q.clock_ok);
    Q.card = pc_vocab_next(&Q.ses, &V.prog, V.skip, Q.day, Q.clock_ok, ui_rng(), &Q.why);
    Q.answered = false;
    Q.picked = -1;
    if (Q.card < 0) {
        glyph_text_clear(Q.prompt);
        for (int i = 0; i < 4; i++) { glyph_text_clear(Q.txt[i]); lv_obj_add_flag(Q.box[i], LV_OBJ_FLAG_HIDDEN); }
        lv_label_set_text(Q.top, T(S_NOTHING_TO_ASK_IN_THIS_DECK) /* This deck has no words to quiz */);
        return;
    }
    int dir = cfg_int("vocab_dir", DIR_EN_ZH);
    Q.field = dir == DIR_ZH_EN ? 0 : dir == DIR_MIX ? (int)pc_rng_below(ui_rng(), 2) : 1;
    uint32_t d[3];
    int nd = pc_vocab_distractors(ui_rng(), V.gp, V.skip, (uint32_t)Q.card, Q.field, d, 3);
    Q.nchoice = nd + 1;
    Q.right_pos = (int)pc_rng_below(ui_rng(), (uint32_t)Q.nchoice);
    for (int i = 0, k = 0; i < Q.nchoice; i++) Q.choice[i] = i == Q.right_pos ? (uint32_t)Q.card : d[k++];
    const uint16_t *t;
    uint16_t n;
    entry_text((uint32_t)Q.card, !Q.field, &t, &n);
    set_prompt(t, n, false);
    band_layout(false);
    for (int i = 0; i < 4; i++) {
        ui_choice_set(Q.box[i], NULL, UI_LOOK_NORMAL);
        glyph_text_set_color(Q.txt[i], UI_FG);
        if (i >= Q.nchoice) { lv_obj_add_flag(Q.box[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(Q.box[i], LV_OBJ_FLAG_HIDDEN);
        entry_text(Q.choice[i], Q.field, &t, &n);
        glyph_text_set(Q.txt[i], V.pk, t, n, PROMPT_PX2, 2);
        lv_obj_center(Q.txt[i]);
    }
    q_top_text();
    Q.q++;
    Q.build_ms = (int)((esp_timer_get_time() - t0) / 1000);
    if (Q.build_ms > Q.build_max) Q.build_max = Q.build_ms;
    printf("APP vocab q=%d card=%d why=%d field=%d choices=%d right=%d build_ms=%d\n", Q.q, (int)Q.card, Q.why, Q.field,
           Q.nchoice, Q.right_pos, Q.build_ms);
}

static void next_cb(lv_timer_t *t) { (void)t; Q.next_tmr = NULL; q_next(); }

static void q_answer(int i)
{
    if (Q.card < 0 || i < 0 || i >= Q.nchoice) return;
    if (Q.answered) { q_next(); return; }
    Q.answered = true;
    Q.picked = i;
    bool right = i == Q.right_pos;
    pc_vocab_answer(&Q.ses, &V.prog, (uint32_t)Q.card, Q.why, right, Q.day, Q.clock_ok);
    save_soon();
    bool fx = lowmem_level() < PC_LOWMEM_STILL;                  /* lowmem >= 2: no rim animation */
    lv_obj_set_style_bg_color(Q.box[Q.right_pos], ui_c(C_RIGHT), 0);
    glyph_text_set_color(Q.txt[Q.right_pos], lv_color_white());
    if (right) {
        if (ui_screen_on()) ui_haptic_tick();
        if (fx) { if (Q.ses.run && Q.ses.run % 10 == 0) ledrim_chase(ui_tok(UI_PAL_gold), 1); else ledrim_flash(C_RIGHT, 450); }
        Q.next_tmr = lv_timer_create(next_cb, NEXT_MS, NULL);
        lv_timer_set_repeat_count(Q.next_tmr, 1);
    } else {
        lv_obj_set_style_bg_color(Q.box[i], ui_c(C_WRONG), 0);
        glyph_text_set_color(Q.txt[i], lv_color_white());
        if (ui_screen_on()) ui_haptic_strong();
        if (fx) ledrim_flash(C_WRONG, 700);
    }
    for (int k = 0; k < Q.nchoice; k++) if (k != i && k != Q.right_pos) glyph_text_set_color(Q.txt[k], UI_DIM);
    /* the answer pairs with the prompt now: show the extra line (example words) when the card has one */
    pc_vocab_entry_t e;
    if (!pc_vocab_entry(V.gp, (uint32_t)Q.card, &e) && e.extra) {
        const uint16_t *t;
        uint16_t n;
        entry_text((uint32_t)Q.card, !Q.field, &t, &n);
        set_prompt(t, n, true);
        glyph_text_set(Q.extra, V.pk, e.extra, e.extra_len, PROMPT_PX2, 1);
        band_layout(true);
    }
    q_top_text();
    printf("APP vocab answer q=%d card=%d pick=%d right=%d box=%u due=%u run=%u reviews=%u\n", Q.q, (int)Q.card, i, right,
           V.prog.cards[Q.card].box, V.prog.cards[Q.card].due, Q.ses.run, (unsigned)V.prog.reviews);
}

static void box_cb(lv_event_t *e) { q_answer((int)(intptr_t)lv_event_get_user_data(e)); }
static void bg_tap(void) { if (Q.answered) q_next(); }
static bool quiz_knob(int d) { (void)d; if (Q.answered) q_next(); return true; }

static void quiz_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    Q.root = p;
    Q.next_tmr = NULL;
    ui_timer_guard(p, &Q.next_tmr);
    Q.q = Q.build_ms = Q.build_max = 0;
    deck_open_current();
    ledrim_create(p);
    pc_vocab_session_init(&Q.ses, (uint16_t)cfg_int("vocab_new", NEW_DEFAULT));
    Q.top = ui_text_dim(p, UI_SLOT_TITLE, NULL);          /* one line */
    lv_label_set_long_mode(Q.top, LV_LABEL_LONG_MODE_DOTS);
    Q.band = lv_obj_create(p);
    lv_obj_remove_style_all(Q.band);
    lv_obj_set_size(Q.band, ui_scale_pct(316, 103), ui_scale_pct(72, 130));
    lv_obj_align(Q.band, LV_ALIGN_CENTER, 0, ui_scale_pct(-8, 130));
    lv_obj_set_style_bg_color(Q.band, UI_PANEL, 0);
    lv_obj_set_style_bg_opa(Q.band, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(Q.band, 20, 0);
    lv_obj_remove_flag(Q.band, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    Q.prompt = glyph_text_create(Q.band);
    lv_obj_set_width(Q.prompt, PROMPT_W);
    Q.extra = glyph_text_create(Q.band);
    lv_obj_set_width(Q.extra, PROMPT_W);
    glyph_text_set_color(Q.extra, UI_A1);
    ui_choice_grid(p, 2, 2, box_cb, Q.box);                 /* the four answers: a 2 x 2 grid of the kit, the words inside */
    for (int i = 0; i < 4; i++) {
        Q.txt[i] = glyph_text_create(Q.box[i]);
        lv_obj_set_width(Q.txt[i], lv_obj_get_style_width(Q.box[i], 0) - 18);   /* not get_width: the layout of a new cell is not computed yet */
    }
    ui_back_button(p);
    ui_page_tap(p, bg_tap);
    if (!V.gp) { lv_label_set_text(Q.top, T(S_CANNOT_OPEN_THE_DECK) /* The deck cannot be opened */); Q.card = -1; return; }
    q_next();
}

static void quiz_destroy(void)
{
    if (Q.next_tmr) { lv_timer_delete(Q.next_tmr); Q.next_tmr = NULL; }
    lv_obj_clean(Q.root);                      /* widgets before the pack they point into (the home screen keeps it) */
    ledrim_destroy();
    if (V.save_tmr) { lv_timer_delete(V.save_tmr); V.save_tmr = NULL; }
    prog_save();
    if (H.alive) home_stats();                 /* the home screen below shows the new numbers */
    printf("APP vocab quiz closed q=%d answered=%u right=%u best_run=%u build_max=%d\n", Q.q, Q.ses.answered, Q.ses.right,
           Q.ses.best_run, Q.build_max);
}
static const ui_screen_t scr_vquiz = { .name = "vocab_quiz", .create = quiz_create, .destroy = quiz_destroy, .on_knob = quiz_knob, .content = true };

/* ================= home ================= */
static const ui_screen_t scr_vhome, scr_vdecks;

/* the centre of the home ring: the numbers (the deck's title is a glyph picture, in UPPER above them); the note about the clock or the progress file goes in TITLE */
static void home_stats(void)
{
    if (!V.gp) { ui_ring_set_center_text(H.ring, T(S_CANNOT_OPEN_THE_DECK) /* The deck cannot be opened */); return; }
    bool ok;
    uint16_t d = today(&ok);
    pc_vocab_stats_t st;
    pc_vocab_stats(&V.prog, V.skip, d, &st);
    unsigned pct = st.today_n ? 100u * st.today_ok / st.today_n : 0;
    char b[160];
    snprintf(b, sizeof b, T(S_LEARNED_U_U_DUE_U_NEW_U_STREAK_UD_U) /* Learned %u/%u due %u · new %u streak %… */,
             (unsigned)st.learned, (unsigned)st.total, (unsigned)st.due, (unsigned)st.fresh, st.streak, pct);
    ui_ring_set_center_text(H.ring, b);
    const char *n = !ok ? T(S_CLOCK_NOT_SET_REVIEW_DAYS_DO_NOT_ADVANCE) /* Time not synced: the review day will not advance */
                  : V.load_rc == -ESTALE ? T(S_THE_DECK_CHANGED_PROGRESS_STARTS_AGAIN) /* The deck changed, progress restarted */
                  : V.load_rc && V.load_rc != -ENOENT ? T(S_PROGRESS_FILE_WAS_DAMAGED_STARTED_AGAIN) /* The progress file was corrupt, restarted */ : "";
    if (builtin() && n[0]) snprintf(b, sizeof b, "%s · %s", T(S_EXAMPLE) /* Built-in sample */, n);
    else snprintf(b, sizeof b, "%s", builtin() ? T(S_EXAMPLE) /* Built-in sample */ : n);
    if (H.tag) lv_label_set_text(H.tag, b);
}

static void start_cb(void) { if (V.gp) ui_push(&scr_vquiz, NULL); }
/* the direction (three values) is an axis (Task 7, rule R21): a tap on its cell steps it, then the knob does for 4 s; the cell in the selection colour meanwhile */
static bool dir_step(int axis, int n)
{
    (void)axis;
    int d = ((cfg_int("vocab_dir", DIR_EN_ZH) + n) % DIR_N + DIR_N) % DIR_N;
    cfg_set_int("vocab_dir", d);
    ui_ring_set_label(H.ring, 1, dir_name(d));
    ui_ring_set_mark(H.ring, 1);
    return true;
}
static void dir_end(void) { if (H.alive) ui_ring_set_mark(H.ring, -1); }
static void home_pick(int idx)
{
    if (idx == 0 || idx == -1) start_cb();                  /* the centre starts too */
    else if (idx == 1) ui_axis_borrow(H.root, 0, dir_step, dir_end, true);
    else if (idx == 2) ui_push(&scr_vdecks, NULL);
}
static void home_back(void) { ui_pop(); }

static void home_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    H.root = p;
    deck_open_current();
    const char *labels[3] = { T(S_START) /* Start */, dir_name(cfg_int("vocab_dir", DIR_EN_ZH)), T(S_DECKS) /* Deck */ };
    H.ring = ui_ring_create_menu(p, labels, 3, home_pick, home_back);
    H.tag = NULL;
    H.title = NULL;
    if (ui_ring_disc()) {                     /* a ring with a centre disc: the note (TITLE) and the deck's title (a glyph picture, UPPER) sit over it; the grid's header band is the ring's alone */
        H.tag = ui_text_dim(p, UI_SLOT_TITLE, NULL);
        lv_label_set_long_mode(H.tag, LV_LABEL_LONG_MODE_DOTS);
        H.title = glyph_text_create(ui_slot_holder(p, UI_SLOT_UPPER, 28));
        lv_obj_set_width(H.title, 150);
        if (V.gp) glyph_text_set_node(H.title, V.pk, 0, 24, 1);
        lv_obj_center(H.title);
    }
    H.alive = true;
    home_stats();
}

static void home_destroy(void)
{
    H.alive = false;
    lv_obj_clean(H.root);                     /* the title points into the pack */
    deck_close();
}

static const ui_screen_t scr_vhome = { .name = "vocab", .create = home_create, .destroy = home_destroy, .on_knob = ui_axis_knob, .on_suspend = ui_axis_stop };

/* ================= deck chooser ================= */
#define MAXDECKS 12
#define MORE_CAP 99                                /* stop examining files once this many did not fit */
static struct {
    int n, more;                                  /* decks listed; matching files past MAXDECKS, not listed */
    char path[MAXDECKS][PC_LIST_PATH_MAX];
    lv_obj_t *list;
    int sel;                                      /* the current deck's row, -1 none */
} D;

static int deck_cmp(const void *a, const void *b) { return strcmp(a, b); }

/* the .zip files in /sdcard/vocab that open as vocab decks, sorted, then the built-in sample */
static void find_decks(void)
{
    int64_t t0 = esp_timer_get_time();
    int seen = 0;
    D.n = D.more = 0;
    DIR *d = g_sd_ok ? opendir(VOCAB_DIR) : NULL;
    struct dirent *e;
    while (d && D.more < MORE_CAP && (e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 5 || l > 60 || strcasecmp(e->d_name + l - 4, ".zip") || e->d_name[0] == '.') continue;
        char path[PC_LIST_PATH_MAX];
        snprintf(path, sizeof path, VOCAB_DIR "/%s", e->d_name);
        seen++;
        bool ok = !strcmp(path, V.path) || gt_pack_in_use(path);       /* open already: fine */
        if (!ok) {
            gt_pack_t *g = gt_pack_open(path);
            ok = g && pc_vocab_count(gt_pack_gp(g));
            gt_pack_close(g);
        }
        if (!ok) continue;
        if (D.n < MAXDECKS - 1) snprintf(D.path[D.n++], PC_LIST_PATH_MAX, "%s", path);
        else D.more++;
    }
    if (d) closedir(d);
    qsort(D.path, (size_t)D.n, PC_LIST_PATH_MAX, deck_cmp);
    snprintf(D.path[D.n++], PC_LIST_PATH_MAX, BUILTIN);
    printf("APP vocab decks files=%d decks=%d ms=%d\n", seen, D.n, (int)((esp_timer_get_time() - t0) / 1000));
}

static void reopen_async(void *u) { (void)u; while (ui_top() == &scr_vdecks || ui_top() == &scr_vhome) ui_pop(); ui_push(&scr_vhome, NULL); }

static void choose(int i)
{
    if (i < 0 || i >= D.n) return;
    printf("APP vocab choose %s\n", D.path[i]);
    cfg_set_str("vocab_deck", D.path[i]);
    lv_async_call(reopen_async, NULL);            /* the home screen re-opens with it */
}
static void deck_btn_cb(lv_event_t *e) { choose((int)(intptr_t)lv_event_get_user_data(e)); }

static void decks_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    find_decks();
    ui_obj_guard(ui_list(p), &D.list);
    lv_obj_t *cur = NULL;
    D.sel = -1;
    for (int i = 0; i < D.n; i++) {
        bool is = !strcmp(D.path[i], V.path);
        lv_obj_t *r = pack_title_row(D.list, D.path[i], is ? UI_SEL : UI_PANEL, deck_btn_cb, (void *)(intptr_t)i);
        if (is) { cur = r; D.sel = i; }
    }
    if (D.more) {
        char m[64], n[8];
        snprintf(n, sizeof n, D.more >= MORE_CAP ? "%d+" : "%d", D.more);
        snprintf(m, sizeof m, T(S_S_MORE_NOT_SHOWN) /* ...%s more not listed */, n);
        ui_list_note(D.list, m);
    }
    lv_obj_update_layout(D.list);                  /* without a layout pass the scroll does nothing */
    ui_list_show(D.list, cur);
    ui_back_button(p);
}

static bool decks_knob(int d) { return ui_list_knob(D.list, d); }
static const ui_screen_t scr_vdecks = { .name = "vocab_decks", .create = decks_create, .on_knob = decks_knob };

/* ================= entry and console ================= */
void vocab_open(void) { ui_push(&scr_vhome, NULL); }

static void print_stat(void)
{
    bool ok;
    uint16_t d = today(&ok);
    pc_vocab_stats_t st;
    pc_vocab_stats(&V.prog, V.skip, d, &st);
    printf("APP vocab stat deck=%s cards=%u usable=%u day=%u clock=%d total=%u fresh=%u seen=%u learned=%u due=%u streak=%u today=%u/%u reviews=%u correct=%u saves=%d save_ms=%d dirty=%d load_rc=%d q=%d build_ms=%d build_max=%d lowmem=%d\n",
           V.path, (unsigned)V.n, (unsigned)V.usable, d, ok, (unsigned)st.total, (unsigned)st.fresh, (unsigned)st.seen,
           (unsigned)st.learned, (unsigned)st.due, st.streak, st.today_ok, st.today_n, (unsigned)V.prog.reviews,
           (unsigned)V.prog.correct, V.saves, V.save_ms, V.dirty, V.load_rc, Q.q, Q.build_ms, Q.build_max, lowmem_level());
}

/* console "vocab ...":
 *   (none)            open the home screen          start          the quiz (from home)
 *   ans N             tap answer N (0 TL 1 TR 2 BL 3 BR); again after an answer = next
 *   right | wrong     tap the right / a wrong answer                next      the next question
 *   dir N             0 EN->ZH, 1 ZH->EN, 2 mixed (cfg vocab_dir)  new N     new cards a day (cfg vocab_new)
 *   deck PATH|builtin use that deck (cfg vocab_deck)               decks     the chooser; choose N there
 *   stat | save | reset (this deck's progress, saved at once) */
void vocab_cmd(const char *a)
{
    int n;
    char p[PC_LIST_PATH_MAX];
    bool quiz = ui_top() == &scr_vquiz;
    if (!a[0]) { while (ui_top() && !strncmp(ui_top()->name, "vocab", 5)) ui_pop(); vocab_open(); }
    else if (!strcmp(a, "start")) { if (ui_top() != &scr_vhome) vocab_cmd(""); start_cb(); }
    else if (sscanf(a, "ans %d", &n) == 1) { if (quiz) q_answer(n); }
    else if (!strcmp(a, "right") || !strcmp(a, "wrong")) {
        if (quiz && !Q.answered && Q.card >= 0) {
            int k = Q.right_pos;
            if (a[0] == 'w') k = Q.nchoice > 1 ? (Q.right_pos + 1 + (int)pc_rng_below(ui_rng(), (uint32_t)Q.nchoice - 1)) % Q.nchoice : -1;
            q_answer(k);
        }
    }
    else if (!strcmp(a, "next")) { if (quiz) q_next(); }
    else if (sscanf(a, "dir %d", &n) == 1 && n >= 0 && n < DIR_N) { cfg_set_int("vocab_dir", n); if (ui_top() == &scr_vhome) ui_ring_set_label(H.ring, 1, dir_name(n)); }
    else if (sscanf(a, "new %d", &n) == 1 && n >= 0 && n <= 1000) { cfg_set_int("vocab_new", n); Q.ses.new_limit = (uint16_t)n; }
    else if (!strncmp(a, "deck ", 5) && sscanf(a + 5, "%95s", p) == 1) {
        cfg_set_str("vocab_deck", !strcmp(p, "builtin") ? BUILTIN : p);
        while (ui_top() && !strncmp(ui_top()->name, "vocab", 5)) ui_pop();
        vocab_open();
    }
    else if (!strcmp(a, "deckrow")) {              /* the chooser: rows, the row with the selection colour, its box */
        if (ui_top() != &scr_vdecks) { printf("APP vocab deckrow no chooser\n"); return; }
        lv_obj_update_layout(D.list);
        lv_area_t ar = { 0 };
        int sel = -1;
        for (int i = 0; i < D.n; i++) {
            lv_obj_t *r = lv_obj_get_child(D.list, i);
            if (r && lv_color_eq(lv_obj_get_style_bg_color(r, 0), UI_SEL)) { sel = i; lv_obj_get_coords(r, &ar); }
        }
        printf("APP vocab deckrow rows=%d sel=%d box=%d,%d,%d,%d\n", D.n, sel, (int)ar.x1, (int)ar.y1, (int)ar.x2, (int)ar.y2);
    }
    else if (!strcmp(a, "quizgeom")) {             /* the four answer boxes (screen coordinates), in the quiz */
        if (ui_top() != &scr_vquiz) { printf("APP vocab quizgeom no quiz\n"); return; }
        lv_obj_update_layout(Q.root);
        for (int i = 0; i < 4; i++) { lv_area_t ar; lv_obj_get_coords(Q.box[i], &ar); printf("APP vocab quizbox %d box=%d,%d,%d,%d\n", i, (int)ar.x1, (int)ar.y1, (int)ar.x2, (int)ar.y2); }
    }
    else if (!strcmp(a, "decks")) { if (ui_top() != &scr_vhome) vocab_cmd(""); ui_push(&scr_vdecks, NULL); }
    else if (sscanf(a, "choose %d", &n) == 1) { if (ui_top() == &scr_vdecks) choose(n); }
    else if (!strcmp(a, "save")) { V.dirty = V.dirty || V.gp; prog_save(); }
    else if (!strcmp(a, "reset")) { if (!V.gp) printf("APP vocab reset no deck\n"); else { pc_vocab_reset(&V.prog, V.hash, V.n); V.dirty = true; prog_save(); if (quiz) q_next(); } }
    else if (!strcmp(a, "stat")) { print_stat(); ui_fps_stat(); }
    else printf("APP vocab unknown %s\n", a);
    if (strcmp(a, "stat")) print_stat();
    printf("APP vocab top=%s\n", ui_top() ? ui_top()->name : "-");
}
