// SPDX-License-Identifier: LGPL-2.1-or-later
/* The instrument's pages (UI unify T10, T11; spec 4.1): the play page (bottom of the stack) with its tap-revealed ring, the chips list, the kits list,
 * one settings list (T10); the pads, the metronome, the looper, the looper's more list and its file ring (T11). Pages only compose shared pieces
 * (ui_text, inst_bars, inst_pads, ui_popring, ui_list*, ui_choice_row, ui_big_button, ui_progress_bar, ui_dots, ui_axis_borrow, ui_arm_tap, Back);
 * the state they show and change goes through inst_ui.c (settings, the chip debounce, tap tempo, the pads' notes) and the engine API. Every
 * instrument ring has no dim layer (the host's rule for the LED ring, O5). LVGL task only. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "font.h"
#include "inst_ble.h"
#include "inst_beat.h"
#include "inst_chip.h"
#include "inst_engine.h"
#include "inst_fm.h"
#include "inst_kit.h"
#include "inst_loopfile.h"
#include "inst_pads.h"
#include "inst_ui.h"
#include "cfg.h"
#include "inst_bars.h"
#include "pc/pc_ledfx.h"
#include "pc/pc_ledring.h"
#include "pc/pc_looper.h"
#include "pc/pc_midi.h"
#include "pc/pc_volume.h"
#include "screens.h"
#include "ui_axis.h"
#include "ui_kit.h"
#include "ui_popring.h"
#include "ui_theme.h"
#include "pc/pc_dial.h"

#define MAXKITS 24
#define MAXPICK (MAXKITS + 1)

static const char *exit_text(void) { return T(S_TAP_AGAIN_REBOOT_INTO_THE_PLAYER) /* Tap again: reboot to the player */; }
static void text_if(lv_obj_t *l, const char *t) { if (l && strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }   /* only what changed: a redraw costs the audio task PSRAM bandwidth */

/* ---- the play page: BLE status (HEAD), chip (UPPER), last note (CENTER), voice bars (LOWER); a tap opens the ring ---- */
static lv_obj_t *s_play, *s_status, *s_chip, *s_note;
static int s_conn = -1, s_bars_chip = -1, s_nbars;

enum { C_PADS, C_METRO, C_LOOP, C_CHIP, C_SET, C_EXIT, C_N };
static const char *play_label(int i)
{
    switch (i) {
    case C_PADS: return T(S_PADS) /* Drum pad */;
    case C_METRO: return T(S_METRONOME) /* Metronome */;
    case C_LOOP: return T(S_LOOPER) /* Loop */;
    case C_CHIP: {                                    /* the ring's centre disc covers the page's chip line: the cell names the chip it steps */
        static char b[48];
        const pc_synth_t *c = inst_chip(inst_ui_chip_shown());
        snprintf(b, sizeof b, "%s\n%s", T(S_CHIP) /* Chip */, c ? c->name : "-");
        return b;
    }
    case C_SET: return T(S_SETTINGS) /* Settings */;
    default: return T(S_BACK_TO_PLAYER) /* Back to player */;
    }
}
static void play_pick(int i)
{
    if (i == C_PADS) ui_push(&scr_inst_pads, NULL);       /* ui_push closes the ring */
    else if (i == C_METRO) ui_push(&scr_inst_metro, NULL);
    else if (i == C_LOOP) ui_push(&scr_inst_loop, NULL);
    else if (i == C_SET) ui_push(&scr_inst_set, NULL);
    else if (i == C_EXIT) inst_ui_exit();                 /* the armed cell's second tap */
}
static void play_frame(bool note);
static const ui_popring_t s_ring;
static bool chip_step(int axis, int d)            /* the cell and the page's line follow at once (as the playing ring's order axis: a relabel per step) */
{
    (void)axis;
    inst_ui_chip_step(d);
    play_frame(false);
    if (ui_popring_up(&s_ring)) ui_popring_refresh();   /* in place: no ring rebuilt per detent (the step itself restarts the 4 s) */
    return true;
}
static const char *play_armed(int i) { (void)i; return exit_text(); }
/* Q1: the chip is an axis (a tap = the next chip, then the knob for 4 s; applied 350 ms after the last step); Q4: leaving is an armed cell. The Back cell closes. */
static const ui_popring_t s_ring = { .n = C_N, .label = play_label, .pick = play_pick, .axis_mask = 1u << C_CHIP, .step = chip_step,
                                     .arm_mask = 1u << C_EXIT, .armed = play_armed, .sys_mask = 1u << C_EXIT, .no_dim = true };   /* no dim layer over the LED ring (§4.1); console "popring dim" overrides */
static void play_open(void) { if (s_play) ui_popring_open(s_play, &s_ring, 0); }

static void chip_text(char *b, size_t n)
{
    int i = inst_ui_chip_shown();
    const pc_synth_t *c = inst_chip(i);
    if (!c) snprintf(b, n, "-");
    else if (!strcmp(c->id, "fm") && i == inst_engine_chip()) snprintf(b, n, "FM  %s", inst_fm_patch_name(inst_fm_program(0)));   /* FM adds channel 1's patch */
    else snprintf(b, n, "%s", c->name);
}

static void play_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_play = p;
    s_status = ui_text(p, UI_SLOT_HEAD, NULL);
    s_chip = ui_text(p, UI_SLOT_UPPER, font_large());
    s_note = ui_text(p, UI_SLOT_CENTER, font_clock());
    char b[8];
    lv_label_set_text(s_note, inst_engine_last_note() >= 0 ? pc_midi_note_name(inst_engine_last_note(), b) : "-");
    inst_bars_create(p, UI_SLOT_LOWER);
    s_conn = s_bars_chip = -1;
    ui_page_tap(p, play_open);
    scr_inst_frame(false);
}
static void play_destroy(void)
{
    if (ui_popring_up(&s_ring)) ui_popring_close();
    s_play = s_status = s_chip = s_note = NULL;
}
static bool play_knob(int d) { return ui_axis_knob(d); }   /* the chip cell while it is borrowed; else the host's volume */
const ui_screen_t scr_inst_play = { .name = "inst_play", .create = play_create, .destroy = play_destroy, .on_knob = play_knob };

static void play_frame(bool note)
{
    if (!s_play) return;
    char b[64];
    if (note) text_if(s_note, pc_midi_note_name(inst_engine_last_note(), b));   /* also while covered: the label is there when the page is back */
    chip_text(b, sizeof b);
    text_if(s_chip, b);
    int c = inst_ble_connected();
    if (c != s_conn) {
        s_conn = c;
        char sb[64];                                   /* the symbol outside T_RAW: its key is the literal alone (a macro in front would never match the data table) */
        snprintf(sb, sizeof sb, "%s  %s", LV_SYMBOL_BLUETOOTH, c ? T_RAW("已連線", "Connected") : T_RAW("等待連線：JukeDial MIDI", "Waiting: JukeDial MIDI"));
        lv_label_set_text(s_status, sb);
        lv_obj_set_style_text_color(s_status, c ? UI_A1 : UI_DIM, 0);
    }
    const pc_synth_t *ch = inst_chip(inst_engine_chip());
    if (s_bars_chip != inst_engine_chip() && inst_ui_chip_shown() == inst_engine_chip()) {   /* a new chip: its voices (not while a step waits) */
        s_bars_chip = inst_engine_chip();
        s_nbars = ch ? ch->voice_count() : 0;
        inst_bars_build(s_nbars);
    }
    if (ui_top() != &scr_inst_play || !ch) return;        /* the bars move only where they are seen */
    for (int i = 0; i < s_nbars; i++) {
        uint8_t lv = 0;
        int act = 0;
        ch->voice_state(i, &lv, &act);
        inst_bars_set(i, lv);
    }
}

/* ---- the settings list (spec 4.1 P_SET): one scrolling list; rows with three or more values are axes (tap = step, then the knob 4 s) ---- */
static lv_obj_t *s_set_list, *s_set_rows[INST_ROW_N];
static const char *on_off(int v) { return v ? T(S_ON_2) /* On */ : T(S_OFF_2) /* Off */; }

static const char *layer_text(int l)
{
    static const char *const zh[] = { "自動", "晶片", "取樣", "兩者" }, *const en[] = { "Auto", "Chip", "Kit", "Both" };
    return l >= 0 && l < 4 ? T_RAW(zh[l], en[l]) : "-";
}

static void kit_state_text(char *b, size_t n)
{
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    int e;
    inst_kit_state_t st = inst_kit_state(&e, NULL, NULL);
    if (st == INST_KIT_LOADING) snprintf(b, n, "%s", T(S_LOADING_2) /* Loading... */);
    else if (st == INST_KIT_ERROR) snprintf(b, n, "%s", inst_ui_kit_err(e));
    else snprintf(b, n, "%s", cfg->kit[0] ? cfg->kit : T(S_NONE) /* None */);
}

static void set_text(int r, char *b, size_t n)
{
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    const pc_synth_t *c = inst_chip(inst_engine_chip());
    char v[64];
    switch (r) {
    case INST_ROW_CHIP: snprintf(b, n, "%s  %s", T(S_CHIP) /* Chip */, c ? c->name : "-"); break;
    case INST_ROW_MAP: snprintf(b, n, "%s  %s", T(S_MAPPING) /* Mapping */, cfg->map == PC_MAP_GM ? "GM" : cfg->map == PC_MAP_DIRECT ? "Direct" : "Auto"); break;
    case INST_ROW_CH:
        if (cfg->channel < 0) snprintf(b, n, "%s  %s", T(S_CHANNEL) /* Channel */, T(S_ALL) /* All */);
        else snprintf(b, n, "%s  %d", T(S_CHANNEL) /* Channel */, cfg->channel + 1);
        break;
    case INST_ROW_VOL: snprintf(b, n, "%s  %d", T(S_VOLUME) /* Volume */, cfg->volume); break;
    case INST_ROW_TIME: snprintf(b, n, "%s  %s", T(S_TIMING) /* Timing */, cfg->stable ? T(S_STABLE_8_MS) /* Steady +8 ms */ : T(S_NOW) /* Immediate */); break;
    case INST_ROW_KIT: kit_state_text(v, sizeof v); snprintf(b, n, "%s  %s", T(S_KIT) /* Kit */, v); break;
    case INST_ROW_LAYER: snprintf(b, n, "%s  %s", T(S_LAYER) /* Sound layer */, layer_text(cfg->layer)); break;
    case INST_ROW_CLOCK: snprintf(b, n, "%s  %s", T(S_CLOCK) /* Clock */, cfg->clock_ext ? T(S_EXTERNAL) /* External */ : T(S_INTERNAL) /* Internal */); break;
    case INST_ROW_CLICK: snprintf(b, n, "%s  %s", T(S_CLICK) /* Click sound */, on_off(cfg->click)); break;
    case INST_ROW_BHAP: snprintf(b, n, "%s  %s", T(S_BEAT_HAPTICS) /* Click haptic */, on_off(cfg->beat_haptic)); break;
    case INST_ROW_FM: snprintf(b, n, "%s  %s", T(S_FM_PATCH) /* FM voice */, inst_fm_patch_name(cfg->fm)); break;
    case INST_ROW_FX: {
        static const char *const zh[] = { "音高", "彩虹", "漣漪", "彗星", "頻譜", "關" };
        static const char *const en[] = { "Pitch", "Rainbow", "Ripple", "Comet", "Spectrum", "Off" };
        snprintf(b, n, "%s  %s", T(S_RING) /* LED ring */, T_RAW(zh[cfg->fx], en[cfg->fx]));
        break;
    }
    default: snprintf(b, n, "%s", T(S_BACK_TO_PLAYER) /* Back to player */); break;
    }
}
static void set_texts(void)                       /* the value texts only (the armed exit row keeps its own) */
{
    char b[96];
    for (int r = 0; r < INST_ROW_EXIT; r++) {
        if (!s_set_rows[r]) continue;
        set_text(r, b, sizeof b);
        text_if(lv_obj_get_child(s_set_rows[r], 0), b);
    }
}
static void set_refresh(void)
{
    if (!s_set_list) return;
    set_texts();
    int cur = ui_axis_current_of(s_set_list);
    for (int r = 0; r < INST_ROW_N; r++) lv_obj_set_style_bg_color(s_set_rows[r], r == INST_ROW_EXIT ? UI_SYS : r == cur ? UI_SEL : UI_PANEL, 0);
}

static void set_end(void) { set_refresh(); }      /* the borrow ended: the row's own colour again */
static bool set_step(int r, int d)
{
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    switch (r) {
    case INST_ROW_MAP: cfg->map = (pc_map_t)pc_wrapi_step(cfg->map, d, PC_MAP_COUNT); inst_engine_set_map(cfg->map); break;
    case INST_ROW_CH: cfg->channel = pc_wrapi_step(cfg->channel + 1, d, 17) - 1; inst_engine_set_channel(cfg->channel); break;   /* all, 1..16 */
    case INST_ROW_VOL: inst_ui_set_volume(ui_axis_step(cfg->volume, 0, 100, PC_VOL_PER_DETENT, d, ui_axis_from_tap())); set_refresh(); return true;   /* bounded */
    case INST_ROW_LAYER: cfg->layer = (pc_layer_t)pc_wrapi_step(cfg->layer, d, PC_LAYER_COUNT); inst_engine_set_layer(cfg->layer); break;
    case INST_ROW_FM: cfg->fm = pc_wrapi_step(cfg->fm, d, inst_fm_patch_count()); inst_fm_set_program_all(cfg->fm); break;   /* every channel: for playing without a controller */
    case INST_ROW_FX: inst_ui_fx(pc_wrapi_step(cfg->fx, d, PC_FX_COUNT)); break;
    default: return false;
    }
    inst_ui_save();
    set_refresh();
    return true;
}
static bool set_is_axis(int r) { return r == INST_ROW_MAP || r == INST_ROW_CH || r == INST_ROW_VOL || r == INST_ROW_LAYER || r == INST_ROW_FM || r == INST_ROW_FX; }

static bool set_toggle(int r)                    /* the two-value rows (also the metronome ring's clock and click cells): false = not one */
{
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    switch (r) {
    case INST_ROW_TIME: cfg->stable = !cfg->stable; inst_engine_set_stable(cfg->stable); break;
    case INST_ROW_CLOCK: inst_ui_set_clock_ext(!cfg->clock_ext); break;
    case INST_ROW_CLICK: cfg->click = !cfg->click; inst_engine_set_click(cfg->click); break;
    case INST_ROW_BHAP: cfg->beat_haptic = !cfg->beat_haptic; inst_beat_set_haptic(cfg->beat_haptic && cfg_haptic_mv() > 0); break;
    default: return false;
    }
    inst_ui_save();
    set_refresh();
    return true;
}

static void set_row_cb(lv_event_t *e)
{
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    if (r != INST_ROW_EXIT) ui_arm_cancel();                       /* a tap anywhere else cancels the armed exit */
    if (set_is_axis(r)) { ui_axis_borrow(s_set_list, r, set_step, set_end, true); return; }
    if (ui_axis_current_of(s_set_list) >= 0) ui_axis_end();        /* another row: the knob scrolls again */
    switch (r) {
    case INST_ROW_CHIP: ui_push(&scr_inst_chips, NULL); return;
    case INST_ROW_KIT: ui_push(&scr_inst_kits, NULL); return;
    case INST_ROW_EXIT: if (ui_arm_tap(s_set_rows[INST_ROW_EXIT], exit_text())) inst_ui_exit(); return;
    default: set_toggle(r); return;
    }
}

static void set_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list(p), &s_set_list);
    for (int r = 0; r < INST_ROW_N; r++) s_set_rows[r] = ui_list_row(s_set_list, "", UI_PANEL, set_row_cb, (void *)(intptr_t)r);
    char b[96];
    set_text(INST_ROW_EXIT, b, sizeof b);
    lv_label_set_text(lv_obj_get_child(s_set_rows[INST_ROW_EXIT], 0), b);
    ui_back_button(p);
    set_refresh();
}
static void set_destroy(void) { memset(s_set_rows, 0, sizeof s_set_rows); }
static void set_resume(void) { set_refresh(); }    /* back from the chips or kits list: the new value */
static bool set_knob(int d)
{
    ui_arm_cancel();                                               /* scrolling cancels the armed exit */
    if (ui_axis_knob(d)) return true;                              /* an axis row has the knob for 4 s */
    return ui_list_knob(s_set_list, d);
}
const ui_screen_t scr_inst_set = { .name = "inst_set", .create = set_create, .destroy = set_destroy, .on_knob = set_knob, .on_resume = set_resume };

/* ---- the chips and kits lists (one of them at a time: both are pushed from settings, the chips list also by the console) ---- */
static lv_obj_t *s_pick_list, *s_pick_rows[MAXPICK], *s_kit_note;
static int s_npick;
static bool pick_knob(int d) { return ui_list_knob(s_pick_list, d); }
static void pick_destroy(void) { memset(s_pick_rows, 0, sizeof s_pick_rows); s_npick = 0; s_kit_note = NULL; }

static void chip_row(lv_event_t *e)
{
    inst_ui_pick_chip((int)(intptr_t)lv_event_get_user_data(e));
    ui_pop();
}
static void chips_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list_titled(p, T(S_CHIP) /* Chip */), &s_pick_list);
    s_npick = inst_chip_count() < MAXPICK ? inst_chip_count() : MAXPICK;
    for (int i = 0; i < s_npick; i++) s_pick_rows[i] = ui_list_row_mark(s_pick_list, inst_chip(i)->name, i == inst_engine_chip(), chip_row, (void *)(intptr_t)i);
    ui_back_button(p);
    int c = inst_engine_chip();
    if (c >= 0 && c < s_npick) ui_list_show(s_pick_list, s_pick_rows[c]);
}
const ui_screen_t scr_inst_chips = { .name = "inst_chips", .create = chips_create, .destroy = pick_destroy, .on_knob = pick_knob };

static char s_kits[MAXKITS][INST_KIT_NAME];
static int s_nkits;
static bool s_kit_wait;                           /* a kit was chosen and is loading: the list pops when it is ready */
static int kit_current(void)                      /* its row: 0 = none, k + 1 = s_kits[k] */
{
    const char *k = inst_ui_cfg()->kit;
    for (int i = 0; k[0] && i < s_nkits; i++) if (!strcmp(s_kits[i], k)) return i + 1;
    return 0;
}
static void kits_after(void)                      /* after a choice: loading -> say so and wait; an error -> say it and stay; else back to settings */
{
    int e;
    inst_kit_state_t st = inst_kit_state(&e, NULL, NULL);
    s_kit_wait = st == INST_KIT_LOADING;
    if (st == INST_KIT_LOADING) { text_if(s_kit_note, T(S_LOADING_2) /* Loading... */); return; }
    if (st == INST_KIT_ERROR) {
        text_if(s_kit_note, inst_ui_kit_err(e));
        for (int i = 0; i < s_npick; i++) lv_obj_set_style_bg_color(s_pick_rows[i], i == kit_current() ? UI_SEL : UI_PANEL2, 0);
        return;
    }
    ui_pop();
}
static void kit_row(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    snprintf(cfg->kit, sizeof cfg->kit, "%s", i > 0 && i <= s_nkits ? s_kits[i - 1] : "");
    inst_ui_save();
    inst_kit_load(cfg->kit);                      /* async: TF -> PSRAM */
    printf("INST ui kit='%s'\n", cfg->kit);
    kits_after();
}
static void kits_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_kit_wait = false;
    s_nkits = inst_kit_list(s_kits, MAXKITS);
    ui_obj_guard(ui_list_titled(p, T(S_KIT) /* Kit */), &s_pick_list);
    int cur = kit_current();
    s_npick = s_nkits + 1;
    s_pick_rows[0] = ui_list_row_mark(s_pick_list, T(S_NONE) /* None */, cur == 0, kit_row, (void *)(intptr_t)0);
    for (int i = 0; i < s_nkits; i++) s_pick_rows[i + 1] = ui_list_row_mark(s_pick_list, s_kits[i], cur == i + 1, kit_row, (void *)(intptr_t)(i + 1));
    ui_list_note(s_pick_list, s_nkits ? "" : T(S_NO_KITS_ON_THE_CARD_INSTRUMENT_KITS) /* No kits on the card (instrument/kits) */);
    s_kit_note = lv_obj_get_child(s_pick_list, -1);
    ui_back_button(p);
    ui_list_show(s_pick_list, s_pick_rows[cur]);
}
const ui_screen_t scr_inst_kits = { .name = "inst_kits", .create = kits_create, .destroy = pick_destroy, .on_knob = pick_knob };

/* ---- what the pages below share ---- */
static void ring_pop(void) { ui_popring_close(); ui_pop(); }   /* a ring's Back cell on a page that is not the bottom: the page goes */
static bool axis_knob(int d) { return ui_axis_knob(d); }        /* a borrowed ring cell; else the host's volume */

/* ---- the pads page (spec 4.1 P_PADS): 8 sectors (inst_pads), the hub names the kit and its state; a tap on the hub opens the pads ring:
 * Kit (the kits list), Sound layer (an axis, 4 values); its Back cell pops the page ---- */
static lv_obj_t *s_pads, *s_hub;
static uint32_t s_pk_gen;
static int s_pk_st = -1;
enum { PR_KIT, PR_LAYER, PR_N };
static const char *pads_label(int i)
{
    static char kit[64], lay[48];                 /* the ring asks for every cell before it copies any: one buffer each */
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    if (i == PR_KIT) { snprintf(kit, sizeof kit, "%s\n%s", T(S_KIT) /* Kit */, cfg->kit[0] ? cfg->kit : T(S_NONE) /* None */); return kit; }
    snprintf(lay, sizeof lay, "%s\n%s", T(S_LAYER) /* Sound layer */, layer_text(cfg->layer));
    return lay;
}
static void pads_pick(int i) { if (i == PR_KIT) ui_push(&scr_inst_kits, NULL); }   /* ui_push closes the ring */
static bool pads_step(int axis, int d) { (void)axis; set_step(INST_ROW_LAYER, d); ui_popring_refresh(); return true; }   /* the settings row's own step */
static const ui_popring_t s_pads_ring = { .n = PR_N, .label = pads_label, .pick = pads_pick, .back = ring_pop,
                                          .axis_mask = 1u << PR_LAYER, .step = pads_step, .no_dim = true };
static void pads_open(void) { if (s_pads) ui_popring_open(s_pads, &s_pads_ring, 0); }
static void pads_kit(bool force)                  /* the pad labels and the hub, when the kit or its state changed */
{
    const inst_kit_info_t *k = inst_ui_kit();
    int e;
    size_t by;
    int st = (int)inst_kit_state(&e, &by, NULL);
    if (!force && k->gen == s_pk_gen && st == s_pk_st) return;
    s_pk_gen = k->gen;
    s_pk_st = st;
    for (int p = 0; p < PC_PADS; p++) {
        char b[8];
        inst_pads_label(p, k->note[p] < 0 ? "" : k->label[p][0] ? k->label[p] : pc_midi_note_name(k->note[p], b));
    }
    char b[96], v[48];
    if (st == INST_KIT_LOADING) snprintf(v, sizeof v, "%s", T(S_LOADING_2) /* Loading... */);
    else if (st == INST_KIT_ERROR) snprintf(v, sizeof v, "%s", inst_ui_kit_err(e));
    else if (st == INST_KIT_READY) snprintf(v, sizeof v, "%u KB", (unsigned)(by / 1024));
    else v[0] = 0;
    snprintf(b, sizeof b, "%s%s%s", k->name[0] ? k->name : T(S_NO_KIT) /* No kits */, v[0] ? "\n" : "", v);
    ui_pill_set_text(s_hub, b);
    if (ui_popring_up(&s_pads_ring)) ui_popring_refresh();
}
static void pads_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_pads = p;
    s_hub = inst_pads_create(p, inst_ui_pad_down, inst_ui_pad_up, pads_open);
    ui_page_tap(p, pads_open);                    /* the pads object covers the page: only the console ("popring -2") reaches the root, as the hub's tap */
    pads_kit(true);
}
static void pads_destroy(void)
{
    if (ui_popring_up(&s_pads_ring)) ui_popring_close();
    s_pads = s_hub = NULL;
}
static void pads_resume(void) { pads_kit(true); }   /* back from the kits list */
const ui_screen_t scr_inst_pads = { .name = "inst_pads", .create = pads_create, .destroy = pads_destroy, .on_knob = axis_knob, .on_resume = pads_resume };

/* ---- the metronome page (spec 4.1 P_METRO): the clock/loop line (UPPER), the BPM (CENTER), the state (LOWER), a dot per beat (FOOT).
 * A tap opens the ring: the centre starts / stops (Q2); Tempo (bounded axis 40..240), Time signature (axis 2..7), Tap (runs at the touch-down, each one
 * keeps the ring open), Clock int/ext, Click sound; Back pops. With an external clock or a loop the tempo cells are inert and the centre says why. ---- */
static lv_obj_t *s_metro, *s_m_line, *s_m_bpm, *s_m_state, *s_m_dots;
static int s_m_bpb = -1, s_m_lit = -1;
enum { MC_BPM, MC_BPB, MC_TAP, MC_CLOCK, MC_CLICK, MC_N };
static void bpm_text(const inst_metro_t *m, char *b, size_t n)
{
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    if (cfg->clock_ext) { if (m->locked) snprintf(b, n, "%d", (int)lroundf(m->ext_bpm)); else snprintf(b, n, "--"); }
    else snprintf(b, n, "%d", m->loop ? (int)lroundf(m->bpm) : cfg->bpm);
}
static int metro_bpb(const inst_metro_t *m) { return m->loop ? m->bpb : inst_ui_cfg()->bpb; }
static const char *metro_label(int i)
{
    static char c[MC_N][40];
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    inst_metro_t m;
    inst_engine_metro(&m);
    char v[16];
    switch (i) {
    case MC_BPM: bpm_text(&m, v, sizeof v); snprintf(c[i], sizeof c[i], "%s\n%s", T(S_TEMPO) /* Tempo */, v); break;
    case MC_BPB: snprintf(c[i], sizeof c[i], "%s\n%d/4", T(S_BEATS) /* Time sig. */, metro_bpb(&m)); break;
    case MC_TAP: if (inst_ui_taps() > 0 && inst_ui_taps() < 4) snprintf(c[i], sizeof c[i], "Tap\n%d/4", inst_ui_taps()); else snprintf(c[i], sizeof c[i], "Tap"); break;
    case MC_CLOCK: snprintf(c[i], sizeof c[i], "%s\n%s", T(S_CLOCK) /* Clock */, cfg->clock_ext ? T(S_EXT) /* External */ : T(S_INT) /* Internal */); break;
    default: snprintf(c[i], sizeof c[i], "%s\n%s", T(S_CLICK) /* Click sound */, on_off(cfg->click)); break;
    }
    return c[i];
}
static const char *metro_centre(void)
{
    static char b[96];
    inst_metro_t m;
    inst_engine_metro(&m);
    const char *why = inst_ui_tempo_why();        /* a loop (both cells inert) before an external clock (the tempo only): each reason is true for its cells */
    snprintf(b, sizeof b, "%s%s%s", m.running ? T(S_STOP) /* Stop */ : T(S_START) /* Start */, why ? "\n" : "", why ? why : "");
    return b;
}
static const ui_popring_t s_metro_ring;
static void metro_frame(void);
static void metro_pick(int i)
{
    if (i == -1) inst_ui_metro_toggle();
    else if (i == MC_TAP) inst_ui_tap();
    else if (i == MC_CLOCK) set_toggle(INST_ROW_CLOCK);
    else if (i == MC_CLICK) set_toggle(INST_ROW_CLICK);
    metro_frame();
}
static bool metro_step(int axis, int d)
{
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    if (axis == MC_BPM ? inst_ui_tempo_why() != NULL : inst_ui_metre_why() != NULL) { metro_frame(); return false; }   /* the clock / the loop sets it: nothing moves, the centre says why */
    if (axis == MC_BPM) inst_ui_set_bpm(ui_axis_step(cfg->bpm, INST_BPM_LO, INST_BPM_HI, 1, d, ui_axis_from_tap()));
    else inst_ui_set_bpb(pc_wrapi_step(cfg->bpb - 2, d, 6) + 2);
    metro_frame();
    return true;
}
static const ui_popring_t s_metro_ring = { .n = MC_N, .label = metro_label, .pick = metro_pick, .centre = metro_centre, .back = ring_pop,
                                           .axis_mask = 1u << MC_BPM | 1u << MC_BPB, .step = metro_step, .press_mask = 1u << MC_TAP, .no_dim = true };
static void metro_open(void) { if (s_metro) ui_popring_open(s_metro, &s_metro_ring, 0); }
static void metro_frame(void)                     /* the texts, the dots (when the bar changed or it stopped), the open ring */
{
    if (!s_metro) return;
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    inst_metro_t m;
    inst_engine_metro(&m);
    char b[48];
    bpm_text(&m, b, sizeof b);
    text_if(s_m_bpm, b);
    if (cfg->clock_ext) { if (m.locked) snprintf(b, sizeof b, "%s  %.1f", T(S_EXT_CLOCK) /* External clock */, m.ext_bpm); else snprintf(b, sizeof b, "%s", T(S_NO_CLOCK) /* No clock */); }
    else if (m.loop) snprintf(b, sizeof b, "%s", T(S_TEMPO_THE_LOOP_S) /* Tempo follows the loop */);
    else if (inst_ble_subscribed() && m.running) snprintf(b, sizeof b, "%s", T(S_SENDING_CLOCK) /* Sending clock */);
    else b[0] = 0;
    text_if(s_m_line, b);
    text_if(s_m_state, m.running ? "■  BPM" : "▶  BPM");
    lv_obj_set_style_text_color(s_m_state, m.running ? UI_A1 : UI_DIM, 0);
    int bpb = metro_bpb(&m);
    if (bpb != s_m_bpb || (!m.running && s_m_lit >= 0)) { s_m_bpb = bpb; s_m_lit = -1; ui_dots_set(s_m_dots, bpb, -1, UI_A1); }
    if (ui_popring_up(&s_metro_ring)) ui_popring_refresh();
}
static void metro_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_metro = p;
    s_m_line = ui_text_dim(p, UI_SLOT_UPPER, NULL);
    s_m_bpm = ui_text(p, UI_SLOT_CENTER, font_clock());
    s_m_state = ui_text(p, UI_SLOT_LOWER, NULL);
    s_m_dots = ui_dots(p, UI_SLOT_FOOT, 7);
    s_m_bpb = s_m_lit = -1;
    ui_page_tap(p, metro_open);
    metro_frame();
}
static void metro_destroy(void)
{
    if (ui_popring_up(&s_metro_ring)) ui_popring_close();
    s_metro = s_m_line = s_m_bpm = s_m_state = s_m_dots = NULL;
}
const ui_screen_t scr_inst_metro = { .name = "inst_metro", .create = metro_create, .destroy = metro_destroy, .on_knob = axis_knob };

/* ---- the looper page (spec 4.1 P_LOOP; layout: plan's call, the track row went to FOOT because UPPER would touch the 96 px button):
 * a message (TITLE), the state (HEAD), the progress (UPPER), record / end / overdub (CENTER, the page's one action), the 4 tracks (FOOT,
 * a tap = select it, as before). A tap elsewhere opens the ring: Play/Stop, Mute, Clear this track (two taps), Undo, More (the more list); Back pops. ---- */
static lv_obj_t *s_loop, *s_l_msg, *s_l_status, *s_l_prog, *s_l_big, *s_l_trk[PC_LP_TRACKS];
static char s_l_msgbuf[48];
static int64_t s_l_msg_at;
static int s_l_prog_v = -1, s_l_big_look = -1, s_l_trk_look[PC_LP_TRACKS];   /* what is drawn (reset at create) */
static bool s_l_was_full;
enum { LC_PLAY, LC_MUTE, LC_CLEAR, LC_UNDO, LC_MORE, LC_N };
static void loop_msg(const char *t)               /* 2.5 s in TITLE; set from the more list too, shown when the page is back */
{
    snprintf(s_l_msgbuf, sizeof s_l_msgbuf, "%s", t);
    s_l_msg_at = esp_timer_get_time();
    text_if(s_l_msg, s_l_msgbuf);
}
static const char *lf_err(int r)
{
    switch (r) {
    case INST_LF_EMPTY: return T(S_NOTHING_TO_SAVE) /* Nothing to save */;
    case INST_LF_BUSY: return T(S_STOP_THE_OVERDUB_FIRST) /* Stop overdubbing first */;
    case INST_LF_FORMAT: return T(S_NOT_A_LOOP_FILE) /* Wrong file format */;
    case INST_LF_MEMORY: return T(S_OUT_OF_MEMORY) /* Out of memory */;
    default: return T(S_CARD_ERROR) /* Read/write failed */;
    }
}
static bool loop_playing(const inst_loop_t *s) { return s->state == PC_LP_PLAY || s->state == PC_LP_TAKE || s->state == PC_LP_COUNTIN; }
static const char *loop_label(int i)
{
    static char c[LC_N][48];
    inst_loop_t s;
    inst_engine_loop_state(&s);
    switch (i) {
    case LC_PLAY: return loop_playing(&s) ? T(S_STOP) /* Stop */ : T(S_PLAY) /* Play */;
    case LC_MUTE: snprintf(c[i], sizeof c[i], "%s\n%s %d", s.mute >> s.sel & 1 ? T(S_UNMUTE) /* Unmute */ : T(S_MUTE) /* Mute */, T(S_TRACK) /* Track */, s.sel + 1); return c[i];
    case LC_CLEAR: snprintf(c[i], sizeof c[i], "%s\n%s %d", T(S_CLEAR_2) /* Clear */, T(S_TRACK) /* Track */, s.sel + 1); return c[i];
    case LC_UNDO: return T(S_UNDO) /* Undo */;
    default: return T(S_MORE) /* More */;
    }
}
static const char *loop_armed(int i) { (void)i; return T(S_TAP_AGAIN_CLEAR_THE_TRACK) /* Tap again: clear this track */; }
static void loop_frame(int64_t now);
static void loop_pick(int i)
{
    inst_loop_t s;
    inst_engine_loop_state(&s);
    switch (i) {
    case LC_PLAY: inst_engine_loop(INST_LOOP_PLAYSTOP, 0); break;
    case LC_MUTE: inst_engine_loop(INST_LOOP_MUTE, s.sel); break;
    case LC_CLEAR: inst_engine_loop(INST_LOOP_CLEAR, s.sel); break;   /* the armed cell's second tap */
    case LC_UNDO: inst_engine_loop(INST_LOOP_UNDO, 0); break;
    case LC_MORE: ui_push(&scr_inst_loopmore, NULL); return;          /* ui_push closes the ring */
    default: return;
    }
    loop_frame(esp_timer_get_time());
}
static const ui_popring_t s_loop_ring = { .n = LC_N, .label = loop_label, .pick = loop_pick, .back = ring_pop,
                                          .arm_mask = 1u << LC_CLEAR, .armed = loop_armed, .no_dim = true };
static void loop_open(void) { if (s_loop) ui_popring_open(s_loop, &s_loop_ring, 0); }
static void loop_big_cb(lv_event_t *e) { (void)e; inst_engine_loop(INST_LOOP_REC, 0); loop_frame(esp_timer_get_time()); }
static void loop_trk_cb(lv_event_t *e) { inst_engine_loop(INST_LOOP_SEL, (int)(intptr_t)lv_event_get_user_data(e)); loop_frame(esp_timer_get_time()); }
static void loop_frame(int64_t now)               /* only what changed (text_if, the bar in 2 % steps): every setter makes LVGL redraw */
{
    if (!s_loop) return;
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    inst_loop_t s;
    inst_engine_loop_state(&s);
    char b[64];
    uint32_t bar = s.bar ? s.bar : 1, bars = s.len ? s.len / bar : 0, at = s.pos / bar + 1;
    switch (s.state) {
    case PC_LP_COUNTIN: snprintf(b, sizeof b, "%s  %d", T(S_COUNT_IN) /* Ready */, (int)s.countin); break;
    case PC_LP_TAKE:
        if (bars) snprintf(b, sizeof b, "%s  %u / %u", T(S_RECORDING) /* Record */, (unsigned)at, (unsigned)bars);
        else snprintf(b, sizeof b, "%s  %u", T(S_RECORDING) /* Record */, (unsigned)at);
        break;
    case PC_LP_PLAY: snprintf(b, sizeof b, "%s  %u / %u", s.rec ? T(S_OVERDUB) /* Overdub */ : T(S_PLAYING_2) /* Play */, (unsigned)at, (unsigned)bars); break;
    case PC_LP_STOP: snprintf(b, sizeof b, "%s  ·  %u %s", T(S_STOPPED) /* Stop */, (unsigned)bars, T(S_BARS) /* bars */); break;
    default:
        if (cfg->loop_bars) snprintf(b, sizeof b, "%s  ·  %d %s", T(S_EMPTY) /* Empty */, cfg->loop_bars, T(S_BARS) /* bars */);
        else snprintf(b, sizeof b, "%s  ·  %s", T(S_EMPTY) /* Empty */, T(S_FREE_LENGTH) /* Free length */);
        break;
    }
    text_if(s_l_status, b);
    bool rec = s.state == PC_LP_TAKE || s.rec;
    const char *big = s.state == PC_LP_EMPTY ? T(S_REC) /* Record */ : s.state == PC_LP_COUNTIN ? T(S_CANCEL) /* Cancel */
                    : s.state == PC_LP_TAKE ? T(S_END_2) /* Finish */ : s.rec ? T(S_DUB_OFF) /* Stop overdub */ : T(S_DUB) /* Overdub */;
    int look = rec ? UI_LOOK_REC : s.state == PC_LP_COUNTIN ? UI_LOOK_SEL : UI_LOOK_NORMAL;
    if (look != s_l_big_look || strcmp(lv_label_get_text(lv_obj_get_child(s_l_big, 0)), big)) { s_l_big_look = look; ui_choice_set(s_l_big, big, look); }
    for (int t = 0; t < PC_LP_TRACKS; t++) {               /* selected, recording into it, muted (the track looks of the kit) */
        look = t == s.sel ? (rec ? UI_LOOK_REC : UI_LOOK_SEL) : s.mute >> t & 1 ? UI_LOOK_DIM : UI_LOOK_NORMAL;
        if (look == s_l_trk_look[t]) continue;
        s_l_trk_look[t] = look;
        ui_choice_set(s_l_trk[t], NULL, look);
    }
    uint32_t total = s.state == PC_LP_TAKE ? (cfg->loop_bars ? cfg->loop_bars * bar : 16 * bar) : s.len;
    int v = total && (s.state == PC_LP_PLAY || s.state == PC_LP_TAKE) ? (int)((uint64_t)s.pos * 50 / total) * 20 : 0;   /* 2 % steps */
    if (v != s_l_prog_v) { s_l_prog_v = v; lv_bar_set_value(s_l_prog, v, LV_ANIM_OFF); }
    int m = inst_engine_loop_msg();
    if (m == INST_LOOP_MSG_NOCLOCK) loop_msg(T(S_NO_CLOCK) /* No clock */);
    else if (m == INST_LOOP_MSG_TOOLONG) loop_msg(T(S_TOO_LONG) /* Too long */);
    if (s.full && !s_l_was_full) loop_msg(T(S_MEMORY_FULL) /* Memory full */);
    s_l_was_full = s.full;
    if (s_l_msg_at && now - s_l_msg_at > 2500000) { s_l_msgbuf[0] = 0; text_if(s_l_msg, ""); s_l_msg_at = 0; }
    if (ui_popring_up(&s_loop_ring)) ui_popring_refresh();
}
static void loop_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_loop = p;
    s_l_msg = ui_text(p, UI_SLOT_TITLE, NULL);
    lv_obj_set_style_text_color(s_l_msg, UI_A2, 0);
    lv_label_set_text(s_l_msg, s_l_msgbuf);               /* a message from the more list (loaded, cleared) */
    s_l_status = ui_text(p, UI_SLOT_HEAD, NULL);
    s_l_prog = ui_progress_bar(p, UI_SLOT_UPPER);
    s_l_big = ui_big_button(p, UI_SLOT_CENTER, loop_big_cb, NULL, NULL);
    ui_choice_row(p, UI_SLOT_FOOT, PC_LP_TRACKS, loop_trk_cb, s_l_trk);
    for (int t = 0; t < PC_LP_TRACKS; t++) {
        char b[4];
        snprintf(b, sizeof b, "%d", t + 1);
        ui_choice_set(s_l_trk[t], b, UI_LOOK_NORMAL);
    }
    s_l_prog_v = s_l_big_look = -1;
    for (int t = 0; t < PC_LP_TRACKS; t++) s_l_trk_look[t] = -1;
    s_l_was_full = false;
    ui_page_tap(p, loop_open);
    loop_frame(esp_timer_get_time());
}
static void loop_destroy(void)
{
    if (ui_popring_up(&s_loop_ring)) ui_popring_close();
    s_loop = s_l_msg = s_l_status = s_l_prog = s_l_big = NULL;
    memset(s_l_trk, 0, sizeof s_l_trk);
}
const ui_screen_t scr_inst_loop = { .name = "inst_loop", .create = loop_create, .destroy = loop_destroy };

/* ---- the looper's more list (spec 4.1): Save new file, Export MOD (the loop in memory, batch B ruling A-I2: under the name of the file last saved or loaded,
 * else "loop"), a row per loop file (a tap opens the file ring: Load, Delete armed, Back closes), Quantize (axis, 3), Bars (axis: 1/2/4/8/free),
 * Clear all (two taps); a result is the list's last line. `inst loop more N` = a tap on row N.
 * The files are read at create (inst_loop_list, as the old panel did when it opened); a save or a delete builds the list again. ---- */
#define LM_ROWS (INST_LOOP_FILES + 5)
static lv_obj_t *s_lm, *s_lm_list, *s_lm_rows[LM_ROWS], *s_lm_note;
static char s_lm_files[INST_LOOP_FILES][INST_LOOP_NAME], s_lm_msg[48], s_lm_last[INST_LOOP_NAME];   /* last: the file last saved or loaded (this session) */
static int s_lm_n, s_lm_nrows, s_lm_file = -1;
enum { FC_LOAD, FC_DEL, FC_N };
enum { LM_SAVE, LM_EXPORT, LM_FILE0 };            /* the rows before the files */
static int lm_quant(void) { return s_lm_n + LM_FILE0; }
static int lm_bars(void) { return s_lm_n + LM_FILE0 + 1; }
static int lm_reset(void) { return s_lm_n + LM_FILE0 + 2; }
static const int k_bars[] = { 1, 2, 4, 8, 0 };
static void lm_text(int r)
{
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    char b[64];
    static const char *const qz[] = { "關", "1/8", "1/16" }, *const qe[] = { "Off", "1/8", "1/16" };
    if (r == lm_quant()) snprintf(b, sizeof b, "%s  %s", T(S_QUANTISE) /* Quantize */, T_RAW(qz[cfg->loop_q % 3], qe[cfg->loop_q % 3]));
    else if (r == lm_bars() && cfg->loop_bars) snprintf(b, sizeof b, "%s  %d", T(S_BARS_2) /* Bars */, cfg->loop_bars);
    else if (r == lm_bars()) snprintf(b, sizeof b, "%s  %s", T(S_BARS_2) /* Bars */, T(S_FREE) /* Free */);
    else return;
    text_if(lv_obj_get_child(s_lm_rows[r], 0), b);
}
static void lm_refresh(void)                       /* the axis rows' values and the borrowed row's colour */
{
    if (!s_lm_list) return;
    int cur = ui_axis_current_of(s_lm_list);
    for (int r = lm_quant(); r <= lm_bars(); r++) { lm_text(r); lv_obj_set_style_bg_color(s_lm_rows[r], r == cur ? UI_SEL : UI_PANEL, 0); }
}
static void lm_end(void) { lm_refresh(); }
static bool lm_step(int r, int d)
{
    pc_instr_cfg_t *cfg = inst_ui_cfg();
    if (r == lm_quant()) { cfg->loop_q = pc_wrapi_step(cfg->loop_q, d, 3); inst_engine_loop(INST_LOOP_QUANT, cfg->loop_q); }
    else if (r == lm_bars()) {
        int i = 0;
        while (i < 5 && k_bars[i] != cfg->loop_bars) i++;
        cfg->loop_bars = k_bars[pc_wrapi_step(i % 5, d, 5)];
        inst_engine_loop(INST_LOOP_BARS, cfg->loop_bars);
    } else return false;
    inst_ui_save();
    lm_refresh();
    return true;
}
static void lm_note(const char *t) { text_if(s_lm_note, t); if (s_lm_note) ui_list_show(s_lm_list, s_lm_note); }
static void lm_again(void) { ui_popring_close(); ui_replace(&scr_inst_loopmore, NULL); }   /* the files changed: the list again */

static const char *file_label(int i) { return i == FC_LOAD ? T(S_LOAD) /* Load */ : T(S_DELETE) /* Delete */; }
static char s_fr_msg[48];                          /* the file ring's centre after an action (its result); empty: the file's name */
static const char *file_centre(void) { return s_fr_msg[0] ? s_fr_msg : s_lm_file >= 0 ? s_lm_files[s_lm_file] : "-"; }
static const char *file_armed(int i) { (void)i; return T(S_TAP_AGAIN_TO_DELETE) /* Tap again to delete */; }
static void file_pick(int i)
{
    if (s_lm_file < 0 || s_lm_file >= s_lm_n) { ui_popring_close(); return; }
    const char *name = s_lm_files[s_lm_file];
    int r;
    switch (i) {
    case FC_LOAD:
        r = inst_loop_load(name);
        if (r == INST_LF_OK) {                                     /* back to the looper, which says so */
            snprintf(s_lm_last, sizeof s_lm_last, "%s", name);
            loop_msg(T(S_LOADED) /* Loaded */);
            ui_popring_close();
            ui_pop();
            return;
        }
        snprintf(s_fr_msg, sizeof s_fr_msg, "%s", lf_err(r));
        break;
    case FC_DEL: inst_loop_remove(name); lm_again(); return;     /* the armed cell's second tap */
    default: ui_popring_close(); return;                          /* the centre: close */
    }
    ui_popring_refresh();
}
static const ui_popring_t s_file_ring = { .n = FC_N, .label = file_label, .pick = file_pick, .centre = file_centre,
                                          .arm_mask = 1u << FC_DEL, .armed = file_armed, .sys_mask = 1u << FC_DEL, .no_dim = true };

static void lm_row_cb(lv_event_t *e)
{
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    if (r != lm_reset()) ui_arm_cancel();                          /* a tap anywhere else cancels the armed clear-all row */
    if (r == lm_quant() || r == lm_bars()) { ui_axis_borrow(s_lm_list, r, lm_step, lm_end, true); return; }
    if (ui_axis_current_of(s_lm_list) >= 0) ui_axis_end();
    if (r == LM_SAVE) {
        char name[INST_LOOP_NAME] = "";
        int rc = inst_loop_save(name);
        if (rc != INST_LF_OK) { lm_note(lf_err(rc)); return; }
        snprintf(s_lm_last, sizeof s_lm_last, "%s", name);
        snprintf(s_lm_msg, sizeof s_lm_msg, "%s  %s", T(S_SAVED) /* Saved */, name);
        lm_again();
    } else if (r == LM_EXPORT) {                                    /* the loop in memory; the engine's refusal is the note */
        const char *name = s_lm_last[0] ? s_lm_last : "loop";
        int ch = 0, rc = inst_loop_export(name, &ch);
        char b[48];
        if (rc == INST_LF_OK) snprintf(b, sizeof b, "%s.mod  %d ch", name, ch);
        else snprintf(b, sizeof b, "%s", lf_err(rc));
        lm_note(b);
    } else if (r < LM_FILE0 + s_lm_n) {
        s_lm_file = r - LM_FILE0;
        s_fr_msg[0] = 0;
        ui_popring_open(s_lm, &s_file_ring, 0);
    } else if (r == lm_reset() && ui_arm_tap(s_lm_rows[r], T(S_TAP_AGAIN_CLEAR_ALL) /* Tap again: clear all */)) {
        inst_engine_loop(INST_LOOP_RESET, 0);
        loop_msg(T(S_ALL_CLEARED) /* All cleared */);
        ui_pop();                                                  /* back to the looper */
    }
}
static void lm_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_lm = p;
    s_lm_file = -1;
    s_lm_n = inst_loop_list(s_lm_files, INST_LOOP_FILES);
    ui_obj_guard(ui_list(p), &s_lm_list);
    s_lm_rows[LM_SAVE] = ui_list_row(s_lm_list, T(S_SAVE_NEW) /* Save new file */, UI_PANEL, lm_row_cb, (void *)(intptr_t)LM_SAVE);
    s_lm_rows[LM_EXPORT] = ui_list_row(s_lm_list, T(S_EXPORT_MOD) /* Export MOD */, UI_PANEL, lm_row_cb, (void *)(intptr_t)LM_EXPORT);
    for (int i = 0; i < s_lm_n; i++) s_lm_rows[LM_FILE0 + i] = ui_list_row(s_lm_list, s_lm_files[i], UI_PANEL2, lm_row_cb, (void *)(intptr_t)(LM_FILE0 + i));
    for (int r = lm_quant(); r <= lm_bars(); r++) s_lm_rows[r] = ui_list_row(s_lm_list, "", UI_PANEL, lm_row_cb, (void *)(intptr_t)r);
    s_lm_rows[lm_reset()] = ui_list_row(s_lm_list, T(S_CLEAR_ALL) /* Clear all */, UI_SYS, lm_row_cb, (void *)(intptr_t)lm_reset());
    s_lm_nrows = lm_reset() + 1;
    ui_list_note(s_lm_list, s_lm_n ? s_lm_msg : T(S_NO_LOOP_FILES) /* No loop files */);
    s_lm_note = lv_obj_get_child(s_lm_list, -1);
    ui_back_button(p);
    lm_refresh();
    if (s_lm_msg[0]) {                                             /* after a save: the new file's row */
        for (int i = 0; i < s_lm_n; i++) if (strstr(s_lm_msg, s_lm_files[i])) ui_list_show(s_lm_list, s_lm_rows[LM_FILE0 + i]);
        s_lm_msg[0] = 0;                                           /* the note keeps saying it until the list goes */
    }
}
static void lm_destroy(void)
{
    if (ui_popring_up(&s_file_ring)) ui_popring_close();
    memset(s_lm_rows, 0, sizeof s_lm_rows);
    s_lm = s_lm_note = NULL;
    s_lm_nrows = 0;
}
static bool lm_knob(int d)
{
    if (ui_axis_knob(d)) return true;                              /* a borrowed axis row */
    if (ui_popring_up(&s_file_ring)) return false;                 /* the file ring is open (no axis cells): the knob is the volume */
    ui_arm_cancel();                                               /* scrolling cancels the armed clear-all row */
    return ui_list_knob(s_lm_list, d);
}
const ui_screen_t scr_inst_loopmore = { .name = "inst_loopmore", .create = lm_create, .destroy = lm_destroy, .on_knob = lm_knob };

/* ---- inst_ui.c's hooks ---- */
void scr_inst_frame(bool note)
{
    static unsigned n;
    play_frame(note);
    ++n;
    const ui_screen_t *t = ui_top();
    if (t == &scr_inst_set && n % 10 == 0) set_texts();              /* 3 Hz: a kit loading -> done, a console change */
    if (t == &scr_inst_kits && s_kit_wait) kits_after();
    if (t == &scr_inst_pads) pads_kit(false);                         /* a kit loaded or changed: labels and hub */
    if (t == &scr_inst_metro && n % 5 == 0) metro_frame();           /* 6 Hz: running state, the followed tempo, the tap count */
    if (t == &scr_inst_loop && n % 3 == 0) loop_frame(esp_timer_get_time());   /* 10 Hz */
}

void scr_inst_beat(int beat, int acc)
{
    if (ui_top() != &scr_inst_metro || !s_m_dots || s_m_bpb < 1) return;
    s_m_lit = beat % s_m_bpb;
    ui_dots_set(s_m_dots, s_m_bpb, s_m_lit, acc ? UI_A1 : UI_A2);
}

void scr_inst_row(int r)
{
    const ui_screen_t *t = ui_top();
    if (t == &scr_inst_set && r >= 0 && r < INST_ROW_N) lv_obj_send_event(s_set_rows[r], LV_EVENT_CLICKED, NULL);
    else if ((t == &scr_inst_chips || t == &scr_inst_kits) && r >= 0 && r < s_npick) lv_obj_send_event(s_pick_rows[r], LV_EVENT_CLICKED, NULL);
    else if (t == &scr_inst_loopmore && r >= 0 && r < s_lm_nrows) lv_obj_send_event(s_lm_rows[r], LV_EVENT_CLICKED, NULL);
}

void scr_inst_loop_more(int r)
{
    if (ui_top() != &scr_inst_loopmore) {
        if (ui_top() != &scr_inst_loop) inst_ui_page(5);
        ui_push(&scr_inst_loopmore, NULL);
    }
    scr_inst_row(r);
}

int scr_inst_cur(void)
{
    const ui_screen_t *t = ui_top();
    if (t == &scr_inst_chips) return inst_engine_chip();
    if (t == &scr_inst_kits) return kit_current();
    if (t == &scr_inst_set && s_set_list) return ui_axis_current_of(s_set_list);
    if (t == &scr_inst_loopmore && s_lm_list) return ui_axis_current_of(s_lm_list);
    return -1;
}
