// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "ui.h"
#include "pc/pc_vizdraw.h"
extern const ui_screen_t scr_menu;
extern const ui_screen_t scr_langpick; /* the first-boot language picker (i18n B): three cells, each in its own language, one tap stores cfg "lang"; name "langpick" (scr_langpick.c) */
bool langpick_first_boot(void);        /* the card is mounted and the cfg has no "lang" key: app_main / sim_boot push the picker over the menu */
extern const ui_screen_t scr_mp3;
extern const ui_screen_t scr_now;      /* arg: NULL */
void now_open(void);                    /* THE way to the playing page: not on top yet -> exactly one scr_now (pages above an older one popped), and the spectrum page over it when now_view=viz */
extern const ui_screen_t scr_viz;      /* arg: NULL; spectrum page, over the cover page */
void scr_viz_open(void);                /* not on top: the cover page first (if it is not on top), then this page */
void scr_viz_set_kind(pc_vd_kind_t k);  /* store the setting and switch now (page closed: only stored) */
void scr_viz_set_map(pc_vd_map_t m);    /* store the colour mapping (bars, ring) and apply it now if the page is up */
void scr_viz_set_peaks(bool on);              /* store the falling peak marks on or off and apply them now if the page is up */
void scr_viz_set_order(pc_vd_order_t o);       /* store the ring kind's band order and apply it now if the page is up */
void scr_viz_set_ring_gap(int px);            /* store the ring wedges' gap (0, 2, 4) and rebuild the ring map now if the page is up */
void scr_viz_set_fall_full(bool full);       /* store the waterfall size (box or full) and rebuild the picture now if the page is up */
void scr_viz_set_zoom(bool on);               /* store the square board's picture size (zoomed / original) and apply it now if the page is up (the round board ignores it) */
void scr_viz_pick_voice(int i);               /* console: the voice cursor of the voice rows / piano roll (stored in the page, 0-based) */
void scr_viz_set_var(int v);            /* the current kind's variation */
void scr_viz_ring(int pick);            /* console: -2 opens the style ring (page on top); open: 0..n-1 a cell (the four axes: kind, variant, colour, map), -1 the centre, -3 the back cell */
void scr_viz_tap(void);                  /* console: what a tap on the page does (the playing ring, or the style ring when nothing plays) */
void scr_now_tap(void);                  /* console: what a tap on the cover page does (the playing ring) */
void now_leave_if_stopped(void);        /* the cover or spectrum page on top has seen playback and nothing plays: both leave (async; ruling R40) */
void scr_viz_stat(void);                /* console "viz stat" line */
extern const ui_screen_t scr_meta;     /* arg: (void *)(intptr_t)track; -1: the phone's track in speaker mode (bt_meta.h) */
extern const ui_screen_t scr_tracks;   /* arg: (void *)(intptr_t)folder: that folder's level of the song tree (-1: the music root) */
/* the song tree page's shared body (scr_tracks.c): scr_mp3 is the same page with `top` (Rescan last, a song tap keeps the page) */
void scr_tracks_create(lv_obj_t *p, int folder, bool top);
bool scr_tracks_knob(bool top, int d);
void scr_tracks_resume(bool top);
extern const ui_screen_t scr_scan;
extern const ui_screen_t scr_chip;
extern const ui_screen_t scr_noise;
extern const ui_screen_t scr_settings;   /* top page: six categories (display, sound, power, card & connection, advanced, system) */
extern const ui_screen_t scr_setgroup;   /* one category's rows; arg: (void *)(intptr_t)(group + 1) (scr_settings.c) */
extern const ui_screen_t scr_covers;     /* Cover cache (hires T8e): the cover pack's background pass, Pause/Resume, Build now, mode; arg NULL (scr_covers.c) */
void scr_covers_row_label(char *b, size_t n);
void covers_cmd(const char *args);         /* console "covers [open | tap pause|build|mode]" */
extern const ui_screen_t scr_btaudio;    /* Bluetooth audio (BTAUDIO): codec / rate / bits / channel / LDAC preferences, the headset's best and the actual; arg NULL (scr_btaudio.c) */
void btaudio_cmd(const char *args);        /* console "btaudio [open | rows | key codec|rate|bits|chan|ldac]" */
extern const ui_screen_t scr_wiredout;   /* Wired output (BTAUDIO): the wired ceiling / hard limit / 24-bit slot / DAC options with the live line; name "wired" (scr_wiredout.c) */
extern const ui_screen_t scr_timeset;    /* Set time: year / month / day / hour / minute axis rows + Apply, the live clock on top; from Settings > System and the clock menu; name "timeset" (scr_timeset.c) */
void timeset_cmd(const char *args);        /* console "timeset [open | key year|month|day|hour|min|apply | draft]" */
void wired_cmd(const char *args);          /* console "wired [open | rows | key dacmax|dachard|slot|hpf|fs|amute]" */
extern const ui_screen_t scr_cardtest;   /* Card diagnostics (cardtest): arg NULL; the tests run in the page's timer and end when it goes */
extern const ui_screen_t scr_ratetest;  /* Sample rate test (hires T11): arg NULL; starts the test on create, leaves it on destroy / suspend */
extern const ui_screen_t scr_tfcard;      /* Memory card (cardfmt): the card's state in plain words, Format card... when it may be formatted, 說明; arg NULL */
extern const ui_screen_t scr_tfcardfmt;   /* the format flow (warning, code pad, armed tap, progress, result); arg NULL; only scr_tfcard opens it */
void scr_tfcard_row_label(char *b, size_t n);   /* "Card unusable (<reason>)": the settings row's text */
void scr_tfcard_cmd(const char *args);    /* console "cardfmt [open|row|continue|arm]" */
void clock_alarm_tap(int row);              /* console "alarm tap N": a click on row N of the alarm page (on top) */
bool scr_bt_tap_ok(int idx);               /* console "bt tap N" takes -1 (the centre), the quality cell, Status and the last cell (說明); the others would reboot or forget */
void scr_bt_tap(int idx);                   /* console "bt tap N": a tap on cell N of the bluetooth ring (-1 the centre) */
void scr_settings_tap(int row);           /* console "settings tap N": a click on the N-th row of the open page (top page: a category) */
void scr_cardtest_cmd(const char *a);   /* console "cardtest tap raw|file": a click on that row of the page on top */
void scr_settings_tap_key(const char *key);   /* console "settings key <name>": a click on the row of that name (sleep theme style cover bright haptic hapms volmax rg dacmax ratetest usb rescan); unknown: prints "APP settings key unknown <name>" */
void scr_settings_group(const char *name);   /* console "settings group <1..6|display|sound|power|card|advanced|system>": opens that category page from the top page */
void scr_settings_rows(void);           /* console "settings rows": the row count and every row label (page on top; else rows=0) */
extern const ui_screen_t scr_fx;       /* Effects: the filter list; name "fx" */
extern const ui_screen_t scr_fxapp;    /* a filter's page generated from its parameter table; arg (void *)(intptr_t)app; name "fxapp" */
void scr_fx_console(const char *args);  /* console "fxui rows" | "fxui tap <n>" (the list or the filter page on top; n = the n-th clickable row, -1 the last) */
extern const ui_screen_t scr_favs;
extern const ui_screen_t scr_bt;       /* Bluetooth modes */
extern const ui_screen_t scr_radio;
extern const ui_screen_t scr_charge;     /* the FakePod charge-only screen and the low-battery boot page (scr_charge.c; charge_mode.h) */
extern const ui_screen_t scr_spk_ask;    /* speaker mode is an exclusive source: the one confirm before a local source plays (scr_spkask.c; opened only by spk_excl_gate) */
extern const ui_screen_t scr_cast_ask;   /* the radio page's confirm before the reboot into the broadcast radio (scr_cast.c) */
extern const ui_screen_t scr_colors;   /* coloured noise */
extern const ui_screen_t scr_clock;    /* clock, moon, sun, solar term */
extern const ui_screen_t scr_album;    /* Photos: the photo slideshow */
extern const ui_screen_t scr_ring;     /* the alarm is ringing */
extern const ui_screen_t scr_fortune;  /* Fortune: omikuji, moon blocks, dice, wheel, gacha, tarot */
void fortune_cmd(const char *args);     /* console "fortune ..." */
extern const ui_screen_t scr_tools;    /* Tools: dice, slot, moon (clock info face), fortune; spec 2026-11-11-tools-design.md */
extern const ui_screen_t scr_bubbles;  /* Bubble wrap: the shared bubble field and Back; name "tools_bubbles" (scr_bubbles.c) */
extern const ui_screen_t scr_counter;  /* Counters: 2 / 4 / 8 counters on ui_sectors, tap +1, the borrowed knob adjusts; name "tools_counter" (scr_counter.c, ideas G2) */
extern const ui_screen_t scr_breath;   /* Breathing coach (ideas G7): a row of the White noise list; the guide circle on ui_arena + game_ring, pc_breath timing, the playing scene may follow the breath; name "noise_breath" (scr_breath.c) */
extern const ui_screen_t scr_torch;    /* Flashlight (ui_torch.c): a pure white page at full backlight, nothing else; name "tools_torch" */
extern const ui_screen_t scr_tuner;    /* Tuner (scr_tuner.c): Listen / Play a note / 說明; name "tools_tuner" */
extern const ui_screen_t scr_tuner_listen;   /* the microphone and a needle gauge in cents; name "tools_tuner_listen" */
extern const ui_screen_t scr_tuner_tone;     /* the reference tone: big button, note axis, menu (voice, guitar string, A4); name "tools_tuner_tone" */
extern const ui_screen_t scr_tuner_voice;    /* the voice list of the tone page; name "tools_tuner_voice" */
void tuner_cmd(const char *args);            /* console "tuner [listen|tone|voice|shown|tonestate|play|stop|note <midi>|wave <n>|a4 <hz>|guitar <n>]" (scr_tuner.c) */
extern const ui_screen_t scr_ambient;  /* Ambient scenes: lava lamp / fish tank / campfire / night light on ui_arena + game_ring, tap pokes, the borrowed knob steps the brightness; name "tools_ambient" (scr_ambient.c, ideas G5) */
extern const ui_screen_t scr_drink;    /* Drink reminder: the day's cups on ui_sectors with a progress ring, the clock ring's Drink water; name "clock_drink" (scr_drink.c, ideas G7) */
extern const ui_screen_t scr_habits;   /* Habits: 2 / 4 / 8 habits on ui_sectors, a tap = done today, streaks and a 7-day strip; name "clock_habits" (scr_habits.c, ideas G7) */
extern const ui_screen_t scr_stopwatch, scr_countdown;   /* the clock ring's time tools (HUD task, scr_timers.c): a face (tface.c) and a tap-revealed ring; the state is timers_svc.c; names "clock_stopwatch", "clock_countdown" */
void timers_cmd(const char *args);     /* console "timers [sw start|lap|reset|laps | cd set <s>|start|reset|jump <s> | ring | face <name|next> | dump]" (scr_timers.c) */
void drink_cmd(const char *args);       /* console "drink [tap <i> | ring | cup | undo | snooze | tick | reset | set ...]" (scr_drink.c, drink_svc.h) */
void habit_cmd(const char *args);       /* console "habit [tap <i> | sel <i> | ring | undo | name <d> | set <n> | reset | seed <slot> <strip>]" (scr_habits.c) */
void counter_cmd(const char *args);     /* console "counter [tap <i> | sel <i> | ring | reset | set <n> <name> <style>]" (scr_counter.c) */
extern const ui_screen_t scr_sfxbank;  /* Sound bank: the sound bank's entries by group, a tap plays one; name "tools_sfxbank" (scr_sfxbank.c, sfx task 2) */
void sfxbank_cmd(const char *args);     /* console "sfxbank rows | tap <name|row>" (scr_sfxbank.c) */
void tools_cmd(const char *args);       /* console "tools ..." (scr_tools.c) */
void lists_open(int tool);              /* Tools list toys (scr_lists.c): 0 Lunch, 1 Pick a person, 2 Idea generator */
bool lists_cmd(const char *args);       /* a list tool on top: spin | stop | btn N | group | stat; false = not handled */
void vocab_open(void);                  /* Tools vocabulary (scr_vocab.c, spec 2026-11-11-vocab-design.md) */
void vocab_cmd(const char *args);       /* console "vocab ..." */
extern const ui_screen_t scr_explain;  /* arg: const ui_help_t *; the page of every 說明 entry (ui_help.h) */
extern const ui_screen_t scr_pair;     /* a phone asks to pair: six digits, Pair / Reject */
extern const ui_screen_t scr_chip_games;   /* arg: system name (kept alive by the caller) */
void ui_restore_last(void);   /* boot: back to the last mode screen, no autoplay */
lv_obj_t *ui_back_button(lv_obj_t *parent);   /* UI_BACK_W x UI_BACK_H at UI_BACK_Y, pops the screen; held = the main menu (every Back button) */
lv_obj_t *ui_back_button_hub(lv_obj_t *parent);   /* the named exception: Back in the centre hub (fortune wheel, ten-pull), UI_HUB_BACK_Y */
lv_obj_t *ui_back_button_do(lv_obj_t *parent, void (*cb)(void));   /* the same place and look, own action instead of pop (the ring's no-memory Back) */
const char *ui_word_knob(void);   /* the name of the rotary input in a sentence: "旋鈕" (knob) / "Knob"; "按鍵" (keys) / "keys" on a build without one (UI_HAS_KNOB) */
lv_obj_t *ui_progress_arc(lv_obj_t *parent);  /* outer rim: track position, hidden when the length is unknown */
lv_obj_t *ui_sleep_arc(lv_obj_t *parent);     /* just inside: sleep timer left, hidden when off */
/* --- Task 11: shared list --- */
/* A ui_list_row whose content is the pack's title (node 0 at 24 px) drawn once into a PSRAM strip (freed with the row); the pack is closed
 * again before this returns. "builtin:lunch|people|ideas|vocab" name the embedded examples. When the pack or the strip cannot be had the
 * row shows the file's base name as text. */
lv_obj_t *pack_title_row(lv_obj_t *list, const char *pack_path, lv_color_t bg, lv_event_cb_t cb, void *user);
