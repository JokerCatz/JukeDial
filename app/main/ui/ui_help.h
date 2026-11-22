// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
/* The one help piece (fx apps T6, spec 2026-11-13-ui-shared-area.md): a help entry is a list-end row (ui_list_help_row) or the last cell of a
 * popring (ui_popring_t.help); both open the explain page (scr_explain) with the entry's text. Explanations live here and in ui_help_texts.c, never on a page.
 * All calls from the LVGL task. */
typedef struct ui_help {
    const char *id;                                      /* console "help open <id>" */
    const char *title_zh, *title_en, *body_zh, *body_en; /* paragraphs split by "\n\n"; NULL when body_fn */
    int (*body_fn)(char *b, size_t n, bool zh, int arg); int arg;   /* writes the body (paragraphs split by "\n\n") into b[n]; returns its length */
    bool qr;                                             /* settings QR + address first */
    const char *web_zh, *web_en;                         /* "On a computer you can: ..." line under the QR; NULL none */
    uint16_t refresh_ms;                                 /* non-zero: the explain page runs body_fn again every refresh_ms while it is open (ui_help_refresh); a note's text is set only when it changed */
    const void *link;                                    /* hires T8e: non-NULL = a row before the text that opens this page (const ui_screen_t *): Status -> Cover cache */
    const char *link_zh, *link_en;
    bool (*link_shown)(void);                            /* fix 0 (no redundant chrome): NULL = always; else the row is there only while this says so (asked again at every refresh) */
} ui_help_t;

const char *ui_help_label(void);                         /* T("說明", "Help"): the one label of every help entry */
const char *ui_status_label(void);                       /* T("現況", "Status"): the label of the Status cell (ui_popring_t.status, the Bluetooth ring); it opens HELP_STATUS */
lv_obj_t *ui_list_help_row(lv_obj_t *list, const ui_help_t *h);   /* the LAST row of a list (normal look); a tap = ui_help_open; its text is the label */
lv_obj_t *ui_list_entry_row(lv_obj_t *list, const char *text, const ui_help_t *h);   /* the same with its own text (the edit-on-the-computer rows) */
void ui_help_open(const ui_help_t *h);                   /* ui_push(&scr_explain, (void *)h) */
lv_obj_t *ui_qr_box(lv_obj_t *parent, const char *label, const char *url);  /* a short label, the QR (UI_QR_D, square rule) and the address line, one column; used by ui_help_fill only */
void ui_help_fill(lv_obj_t *list, const ui_help_t *h);   /* the explain page's content: one QR box per setup_url address (primary, then mirrors) when h->qr, the web line, one note per paragraph; remembers h for "help show" */
void ui_help_refresh(void);                              /* the explain page's timer: the body again, changed notes set in place, a changed paragraph count rebuilds the notes (no-op for a page without refresh_ms) */
const ui_help_t *ui_help_find(const char *id);
void ui_help_console(const char *args);                  /* "open <id>" | "tap" (the help/entry row of the top page) | "show" (prints APP explain id= title= paras= qr= web=) | "stats" (prints APP explain sets= rebuilds=: what the refresh timer changed since the page was filled) */

/* ui_help_texts.c: every help text. Later tasks add theirs to that file only. */
extern const ui_help_t HELP_SETTINGS;
extern const ui_help_t HELP_RATETEST;                     /* Sample rate test (hires T11) */
extern const ui_help_t HELP_CARD;                         /* Memory card (cardfmt): what formatting does, the steps and the confirmation code */
extern const ui_help_t HELP_CARDTEST;                     /* Card diagnostics (cardtest): the direct read, the file test, the format table */
extern const ui_help_t HELP_COUNTER;                      /* Counters (ideas G2): tap counts, the turn adjusts, the ring */
extern const ui_help_t HELP_DRINK, HELP_HABITS;           /* Drink reminder, Habits (ideas G7, clock family) */
extern const ui_help_t HELP_BREATH;                       /* Breathing coach (ideas G7): the patterns, the session, the noise follow */
extern const ui_help_t HELP_AMBIENT;                      /* Ambient scenes (ideas G5): the four scenes, the tap, the brightness, the music */
extern const ui_help_t HELP_TUNER;                        /* Tuner (tools ring, 2026-11-17): listen and play-a-note pages, the A4 reference */
extern const ui_help_t HELP_SFXBANK;                      /* Sound bank (sfx task 2): what the bank is, how it plays, where the sounds come from */
extern const ui_help_t HELP_COVERS;                       /* Cover cache (hires T8e): the background pass, auto / manual, why it pauses */
extern const ui_help_t HELP_NINEGRID;                     /* the nine-grid of the FakePod's now-playing family (the playing menu's help cell there) */
extern const ui_help_t HELP_VIZ;                           /* the visualiser: kinds, size, voice rows / piano roll, mute and solo, the lyrics file */
extern const ui_help_t HELP_STATUS;                       /* Status: the pipeline as it is now (pc_nowinfo), refreshed every second */
int ui_status_body(char *b, size_t n, bool zh, int arg);  /* its body_fn; the console `status` prints the same text */
extern const ui_help_t HELP_GAMES;                        /* the Games hub's help: how a game page works, then every game's rules (games.h, games.def) */
extern const ui_help_t HELP_FX;                           /* the filter list's help */
extern const ui_help_t HELP_SLOT, HELP_BT, HELP_ALARM, HELP_POMO;   /* the slot pay list, the Bluetooth ring (last cell), the alarm list (last row), the pomodoro ring */
extern const ui_help_t HELP_TIMESET;   /* the Set time page (scr_timeset.c) */
extern const ui_help_t HELP_STOPWATCH, HELP_COUNTDOWN;   /* the stopwatch and countdown rings (HUD task) */
extern const ui_help_t HELP_CAST;                         /* Broadcast radio (the boot mode): Wi-Fi in, Auracast out, the cells, the gestures */
extern const ui_help_t HELP_EXT;                          /* External mode (T2): the keys, the layers, _player/config/keys.txt, the reload; also the Settings page's help */
extern const ui_help_t HELP_WIFI, HELP_RADIO, HELP_LISTS;   /* the edit-on-the-computer entries: QR + address + what the computer can do */
const ui_help_t *ui_help_fx_app(int app);                 /* the help of one filter page: title = the app, body = ui_help_fx_app_body (one page-static block, refilled per call) */
int ui_help_fx_app_body(char *b, size_t n, bool zh, int app);   /* the description, then one line per row of the page (Mix and every parameter; an equalizer band once): "<name>: <lo> – <hi>, <default> <def>" */
extern const ui_help_t *const UI_HELPS[];
int ui_help_count(void);
