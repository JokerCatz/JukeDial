// SPDX-License-Identifier: LGPL-2.1-or-later
/* Card diagnostics (cardtest): the card's facts, two test rows (each keeps its last result on the page), the table of formats the
 * measured speed can feed. One scrolling list: title, the card (capacity, free, file system, name, bus clock), 「Direct read (raw)」 + its result
 * notes, 「File test」 (two taps: the first says what it writes) + its notes, the table, 說明 last. The tests (card_test.c) run in this page's
 * timer, a slice at a time, and end when the page goes (Back, home, a page over it, the screen going dark): nothing half-done stays, the temp file
 * is removed. Every note exists from the start (its text changes in place), so the list does not jump while a test runs. Composed of shared pieces only. */
#include <stdio.h>
#include <string.h>
#include "card_test.h"
#include "font.h"
#include "pc/pc_cardtest.h"
#include "pc/pc_fmtguard.h"
#include "screens.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_theme.h"

enum { N_RAW_S, N_RAW_A, N_RAW_R, N_RAW_B, N_RAW_C, N_FILE_S, N_FILE_W, N_FILE_R, N_FILE_V, N_MAX };
#define RUNGS 9                                           /* the formats + the card's marker (pc_cardtest.h: pc_ct_ladder) */
static lv_obj_t *s_list, *s_raw_row, *s_file_row, *s_n[N_MAX], *s_info[5], *s_rung[RUNGS], *s_disc[8];

static void set_text(lv_obj_t *label, const char *text)         /* only a changed text is set (no redraw every tick) */
{
    if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void mbps(char *b, size_t n, uint32_t x100) { snprintf(b, n, "%u.%02u", (unsigned)(x100 / 100), (unsigned)(x100 % 100)); }

static const char *fail_text(int why)
{
    switch (why) {
    case CTF_NOCARD: return T(S_NO_CARD_2) /* No memory card */;
    case CTF_SMALL: return T(S_CT_FAIL_SMALL) /* Card too small (under 64 MB) to test */;
    case CTF_NOSPACE: return T(S_CT_FAIL_NOSPACE) /* Less than 8 MB free, the file test cannot run */;
    case CTF_NOMEM: return T(S_CT_FAIL_NOMEM) /* Out of memory, cannot test */;
    default: return T(S_CT_FAIL_IO) /* Read or write failed, test stopped */;
    }
}

/* the status note of one test: running (percent), failed (why), done / not run (empty: its result notes tell) */
static void status(int kind, lv_obj_t *note)
{
    const ct_result_t *r = card_test_result(kind);
    char b[96] = "";
    if (card_test_kind() == kind) snprintf(b, sizeof b, T(S_CT_RUNNING) /* Testing... %u%% */, (unsigned)r->pct);
    else if (r->state == CTS_FAIL) snprintf(b, sizeof b, "%s", fail_text(r->fail));
    else if (r->state == CTS_DONE && r->save_failed) snprintf(b, sizeof b, "%s", T(S_CT_SAVE_FAIL) /* Done, but the result could not be saved on the card */);
    else if (r->state == CTS_DONE && r->saved) snprintf(b, sizeof b, T(S_CT_LAST) /* Last run: %s */, !strcmp(r->when, "-") ? "-" : r->when);
    else if (r->state == CTS_NONE) snprintf(b, sizeof b, "%s", T(S_CT_NOT_RUN) /* Not run yet */);
    else snprintf(b, sizeof b, "%s", T(S_DONE) /* Done */);
    set_text(note, b);
}

static void refresh(void)
{
    if (!s_list) return;
    char b[128], x[16], y[16];
    const ct_result_t *r = card_test_result(CT_RAW), *f = card_test_result(CT_FILE);
    status(CT_RAW, s_n[N_RAW_S]);
    status(CT_FILE, s_n[N_FILE_S]);
    const bool rd = r->state == CTS_DONE, fd = f->state == CTS_DONE;
    if (!rd) for (int k = N_RAW_A; k <= N_RAW_C; k++) set_text(s_n[k], "-");
    if (!fd) for (int k = N_FILE_W; k <= N_FILE_V; k++) set_text(s_n[k], "-");
    if (rd) {
        mbps(x, sizeof x, r->avg16_x100); mbps(y, sizeof y, r->slow16_x100);
        snprintf(b, sizeof b, T(S_CT_RAW_A) /* 16 KB reads: %s MB/s (slowest region %s) */, x, y);
        set_text(s_n[N_RAW_A], b);
        if (r->reg16_x100[0] && r->reg16_x100[1] && r->reg16_x100[2]) {       /* the three regions the slowest one was picked from (an older saved line has none) */
            char z[16];
            mbps(x, sizeof x, r->reg16_x100[0]); mbps(y, sizeof y, r->reg16_x100[1]); mbps(z, sizeof z, r->reg16_x100[2]);
            snprintf(b, sizeof b, T(S_CT_RAW_REG) /* Start / mid / end: %s / %s / %s MB/s */, x, y, z);
        } else snprintf(b, sizeof b, "%s", T(S_CT_RAW_REG_NONE) /* Regions: not saved, run the raw test again */);
        set_text(s_n[N_RAW_R], b);
        snprintf(x, sizeof x, "%lu.%lu", (unsigned long)(r->worst_us / 1000), (unsigned long)(r->worst_us % 1000 / 100));
        snprintf(b, sizeof b, T(S_CT_RAW_B) /* Slowest single read: %s ms */, x);
        set_text(s_n[N_RAW_B], b);
        mbps(x, sizeof x, r->max64_x100);
        snprintf(b, sizeof b, T(S_CT_RAW_C) /* Card maximum (64 KB): %s MB/s */, x);
        set_text(s_n[N_RAW_C], b);
    }
    if (fd) {
        mbps(x, sizeof x, f->w_x100);
        snprintf(b, sizeof b, T(S_CT_FILE_W) /* File write: %s MB/s */, x);
        set_text(s_n[N_FILE_W], b);
        mbps(x, sizeof x, f->r_x100);
        snprintf(b, sizeof b, T(S_CT_FILE_R) /* File read-back: %s MB/s */, x);
        set_text(s_n[N_FILE_R], b);
        set_text(s_n[N_FILE_V], f->verify_ok ? T(S_CT_VERIFY_OK) /* Data check: OK */ : T(S_CT_VERIFY_BAD) /* Data check: MISMATCH */);
    }
    uint32_t sraw, sfile;
    card_test_speeds(&sraw, &sfile);
    const uint32_t v = pc_ct_follow(sraw, sfile, NULL);
    pc_ct_rung_t lad[RUNGS];
    const int n = pc_ct_ladder(sraw, sfile, lad, RUNGS);
    for (int k = 0; k < n && k < RUNGS; k++) {
        if (lad[k].fmt < 0) {                                            /* a score of this card, at its place in the ladder: raw and file read-back are two markers */
            const bool file = lad[k].fmt == PC_CT_RUNG_FILE;
            const ct_result_t *res = card_test_result(file ? CT_FILE : CT_RAW);
            if (lad[k].need_x100) {
                mbps(x, sizeof x, lad[k].need_x100);
                snprintf(b, sizeof b, file ? T(S_CT_MARK_FILE) /* This card file read-back %s MB/s */ : T(S_CT_MARK_RAW) /* This card raw %s MB/s */, x);
                if (res->saved) { size_t l = strlen(b); snprintf(b + l, sizeof b - l, "%s", T(S_CT_MARK_LAST) /* (last run) */); }
            } else snprintf(b, sizeof b, "%s", file ? T(S_CT_MARK_FILE_NONE) /* This card file read-back: not tested yet */ : T(S_CT_MARK_RAW_NONE) /* This card raw: not tested yet */);
            char t[128];                                                   /* the leading arrow marks "this card" */
            snprintf(t, sizeof t, "%s", b);
            ui_list_row_set_text(s_rung[k], ui_icon_text(b, sizeof b, UI_ICON_L_ARROW, t));
            ui_list_row_look(s_rung[k], UI_LOOK_SEL);
            continue;
        }
        const pc_ct_fmt_t *fm = pc_ct_format(lad[k].fmt);
        mbps(x, sizeof x, lad[k].need_x100);
        snprintf(b, sizeof b, T(S_CT_LADDER) /* %s  needs %s MB/s */, fm->label, x);
        if (v) {                                                          /* the verdict is the leading icon (the same helper the settings rows use): check / warning (marginal) / cross */
            char t[128];
            snprintf(t, sizeof t, "%s", b);
            ui_icon_text(b, sizeof b, lad[k].grade == PC_CT_YES ? UI_ICON_L_CHECK : lad[k].grade == PC_CT_MARGINAL ? UI_ICON_L_WARN : UI_ICON_L_CLOSE, t);
        }
        ui_list_row_set_text(s_rung[k], b);
        ui_list_row_look(s_rung[k], v && lad[k].grade == PC_CT_NO ? UI_LOOK_DIM : UI_LOOK_NORMAL);
    }
}

static void raw_cb(lv_event_t *e)
{
    (void)e;
    ui_arm_cancel();
    if (!card_test_running() && card_test_start(CT_RAW)) { /* running: the timer steps it */ }
    refresh();
}

static void file_cb(lv_event_t *e)
{
    (void)e;
    if (card_test_running()) return;
    if (!ui_arm_tap(s_file_row, T(S_CT_FILE_ARM) /* Writes a 4 MB temp file on the card, deleted afterwards. Tap again to start */)) return;
    card_test_start(CT_FILE);
    refresh();
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (card_test_running()) { ui_activity(); card_test_step(); }    /* the screen stays on while the card is tested */
    refresh();
}

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    memset(s_n, 0, sizeof s_n);
    memset(s_info, 0, sizeof s_info);
    memset(s_rung, 0, sizeof s_rung);
    memset(s_disc, 0, sizeof s_disc);
    card_test_load();                                                  /* this card's results of an earlier visit or boot (cardtest.txt) */
    ui_obj_guard(ui_list_titled(p, T(S_CT_TITLE) /* Card diagnostics */), &s_list);
    ct_info_t i;
    char b[96], sz[16];
    int ni = 0;
    if (!card_hw_info(&i)) s_info[ni++] = ui_list_note(s_list, T(S_NO_CARD_2) /* No memory card */);
    else {
        pc_fmt_size_label(i.capacity, sz, sizeof sz);
        snprintf(b, sizeof b, T(S_CT_CAPACITY) /* Capacity: %s */, sz);
        s_info[ni++] = ui_list_note(s_list, b);
        if (i.fs) {
            pc_fmt_size_label(i.free_bytes, sz, sizeof sz);
            snprintf(b, sizeof b, T(S_CT_FREE) /* Free: %s */, sz);
            s_info[ni++] = ui_list_note(s_list, b);
            snprintf(b, sizeof b, T(S_CT_FS) /* File system: %s */, i.fs);
            s_info[ni++] = ui_list_note(s_list, b);
        }
        if (i.name[0]) { snprintf(b, sizeof b, T(S_CT_NAME) /* Card: %s */, i.name); s_info[ni++] = ui_list_note(s_list, b); }
        if (i.freq_khz) { snprintf(b, sizeof b, T(S_CT_CLOCK) /* Bus clock: %u MHz */, (unsigned)(i.freq_khz / 1000)); s_info[ni++] = ui_list_note(s_list, b); }
    }
    s_raw_row = ui_list_row(s_list, T(S_CT_RAW_ROW) /* Direct read (raw) */, UI_SYS, raw_cb, NULL);
    for (int k = N_RAW_S; k <= N_RAW_C; k++) s_n[k] = ui_list_note(s_list, "-");
    s_file_row = ui_list_row_wrap(s_list, T(S_CT_FILE_ROW) /* File test (write + read back) */, UI_SYS, file_cb, NULL);
    for (int k = N_FILE_S; k <= N_FILE_V; k++) s_n[k] = ui_list_note(s_list, "-");
    ui_list_header(s_list, T(S_CT_FORMATS) /* Formats this card can feed */);
    for (int k = 0; k < RUNGS; k++) s_rung[k] = ui_list_row_wrap(s_list, "-", UI_PANEL2, NULL, NULL);   /* the ladder: no tap, its looks are set in refresh */
    {                                                                    /* how the numbers are derived: the constants are the pure function's (pc_cardtest.h) */
        char sf[16];
        snprintf(sf, sizeof sf, PC_CT_SAFETY_X100 % 10 ? "%u.%02u" : "%u.%u", (unsigned)(PC_CT_SAFETY_X100 / 100), (unsigned)(PC_CT_SAFETY_X100 % 10 ? PC_CT_SAFETY_X100 % 100 : PC_CT_SAFETY_X100 % 100 / 10));
        snprintf(b, sizeof b, T(S_CT_NOTE_NEED) /* Minimum = the format's data per second x %s (margin) */, sf);
        s_disc[0] = ui_list_note(s_list, b);
        snprintf(b, sizeof b, T(S_CT_NOTE_FLAC) /* FLAC is counted at %u%% of uncompressed; real files are usually smaller */, (unsigned)PC_CT_FLAC_RATIO_PCT);
        s_disc[1] = ui_list_note(s_list, b);
        snprintf(b, sizeof b, T(S_CT_NOTE_MARG) /* Marginal = at least %u%% of the minimum: may play, may stutter */, (unsigned)PC_CT_MARGINAL_PCT);
        s_disc[2] = ui_list_note(s_list, b);
        s_disc[6] = ui_list_note(s_list, T(S_CT_NOTE_FOLLOW) /* The marks follow the file read-back marker; without a file test, the raw one */);
        s_disc[7] = ui_list_note(s_list, T(S_CT_NOTE_SLOWEST) /* The raw marker uses the slowest region; where your files actually sit decides, the file test shows that */);
        s_disc[3] = ui_list_note(s_list, T(S_CT_NOTE_RAW) /* Raw read measures the card itself; ... The file test is closer */);
        s_disc[4] = ui_list_note(s_list, T(S_CT_NOTE_CAL) /* The limits were calibrated on one card only (2026-11-20): estimates, not a guarantee */);
        s_disc[5] = ui_list_note(s_list, T(S_CT_NOTE_SPOTS) /* Only three regions are read ... */);
    }
    ui_list_help_row(s_list, &HELP_CARDTEST);
    ui_back_button(p);
    ui_timer_bind(p, tick, 20, NULL);
    refresh();
}

static void destroy(void)
{
    card_test_cancel();
    s_list = s_raw_row = s_file_row = NULL;
    memset(s_info, 0, sizeof s_info);
    memset(s_rung, 0, sizeof s_rung);
    memset(s_disc, 0, sizeof s_disc);
    memset(s_n, 0, sizeof s_n);
}
static void suspend(void) { card_test_cancel(); }                 /* a page over it (help), or the screen went dark */
static void resume(void) { refresh(); }
static bool on_knob(int d) { ui_arm_cancel(); return ui_list_knob(s_list, d); }

const ui_screen_t scr_cardtest = { .name = "cardtest", .create = create, .destroy = destroy, .on_knob = on_knob, .on_suspend = suspend, .on_resume = resume };

/* console "cardtest tap raw|file": a click on that row (the file row: the second tap is another "tap file") */
void scr_cardtest_cmd(const char *a)
{
    if (!strcmp(a, "page")) {                                          /* every text of the page, in order */
        if (ui_top() != &scr_cardtest) { printf("APP cardtest no page\n"); return; }
        for (int k = 0; k < 5; k++) if (s_info[k]) printf("APP cardtest page %s\n", lv_label_get_text(s_info[k]));
        printf("APP cardtest page %s\n", lv_label_get_text(lv_obj_get_child(s_raw_row, 0)));
        for (int k = 0; k < N_MAX; k++) if (s_n[k]) printf("APP cardtest page %s\n", lv_label_get_text(s_n[k]));
        for (int k = 0; k < RUNGS; k++) if (s_rung[k]) printf("APP cardtest page rung%s %s\n", ui_list_row_look_get(s_rung[k]) == UI_LOOK_SEL ? "*" : "", lv_label_get_text(lv_obj_get_child(s_rung[k], 0)));
        for (int k = 0; k < 8; k++) if (s_disc[k]) printf("APP cardtest page note %s\n", lv_label_get_text(s_disc[k]));
        return;
    }
    lv_obj_t *row = !strcmp(a, "raw") ? s_raw_row : !strcmp(a, "file") ? s_file_row : NULL;
    if (ui_top() == &scr_cardtest && row) lv_obj_send_event(row, LV_EVENT_CLICKED, NULL); else printf("APP cardtest no row\n");
}
