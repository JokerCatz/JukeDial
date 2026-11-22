// SPDX-License-Identifier: LGPL-2.1-or-later
/* Games hub (games hub G0): a ring of the games listed in games.def with 說明 last, opened from the tools ring's last cell. Composes the shared ring;
 * the registry is the X-macro file, so adding a game touches only that file's one new line (and the game's own files). Internal RAM: built with
 * -msmall-data-limit=0 (its few statics are .bss in PSRAM). */
#include "esp_attr.h"
#include "games.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "icons_gen.h"
#include "game_fx.h"
#include "game_result.h"
#include "game_ring.h"
#include "screens.h"
#include "ui_help.h"
#include "ui_ring.h"

#define GAME(id, zh, en, scr) extern const ui_screen_t scr; extern const ui_game_help_t game_help_##id;
#include "games.def"
#undef GAME
static const struct { const char *id, *zh, *en; const ui_screen_t *scr; const ui_game_help_t *help; } GAMES[] = {
#define GAME(id, zh, en, scr) { #id, zh, en, &scr, &game_help_##id },
#include "games.def"
#undef GAME
};
#define N_GAMES ((int)(sizeof GAMES / sizeof GAMES[0]))

int games_count(void) { return N_GAMES; }
const char *games_id(int i) { return i >= 0 && i < N_GAMES ? GAMES[i].id : ""; }
const char *games_name(int i) { return i >= 0 && i < N_GAMES ? T_RAW(GAMES[i].zh, GAMES[i].en) : ""; }
const char *games_name_zh(int i) { return i >= 0 && i < N_GAMES ? GAMES[i].zh : ""; }
const char *games_name_en(int i) { return i >= 0 && i < N_GAMES ? GAMES[i].en : ""; }
const ui_screen_t *games_screen(int i) { return i >= 0 && i < N_GAMES ? GAMES[i].scr : NULL; }
const ui_game_help_t *games_help(int i) { return i >= 0 && i < N_GAMES ? GAMES[i].help : NULL; }
static uint32_t s_boot;                               /* zero-initialised: .bss in PSRAM (this file is built with -msmall-data-limit=0) */
static bool s_boot_set;
uint32_t games_boot(void)
{
    if (!s_boot_set) {
        s_boot_set = true;
#ifdef SIM
        s_boot = 0;                                       /* pinned: the simulator's tests and pictures are deterministic */
#else
        s_boot = esp_random();
#endif
    }
    return s_boot;
}
void games_boot_set(uint32_t v) { s_boot = v; s_boot_set = true; }

int games_find(const char *id) { for (int i = 0; i < N_GAMES; i++) if (!strcmp(GAMES[i].id, id)) return i; return -1; }

static const char *game_icon(const char *id)
{
    static const struct { const char *id, *icon; } GAME_ICONS[] = {
        { "beat", UI_ICON_G_BEAT }, { "breakout", UI_ICON_G_BREAKOUT }, { "c4", UI_ICON_G_C4 }, { "deflect", UI_ICON_G_DEFLECT }, { "dominoes", UI_ICON_G_DOMINOES },
        { "guess", UI_ICON_G_GUESS }, { "knobtoy", UI_ICON_G_KNOBTOY }, { "life", UI_ICON_G_LIFE }, { "puzzle15", UI_ICON_G_PUZZLE15 }, { "radio", UI_ICON_G_RADIO },
        { "safe", UI_ICON_G_SAFE }, { "simon", UI_ICON_G_SIMON }, { "spiro", UI_ICON_G_SPIRO }, { "tower", UI_ICON_G_TOWER }, { "ttt", UI_ICON_G_TTT },
    };
    for (unsigned i = 0; i < sizeof GAME_ICONS / sizeof GAME_ICONS[0]; i++) if (!strcmp(GAME_ICONS[i].id, id)) return GAME_ICONS[i].icon;
    return UI_ICON_GAMES;
}
static void hub_pick(int i)
{
    printf("APP games pick=%d\n", i);
    if (i >= 0 && i < N_GAMES) ui_push(GAMES[i].scr, NULL);
    else ui_help_open(&HELP_GAMES);
}
static void hub_back(void) { ui_pop(); }

static void hub_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[N_GAMES + 1];
    for (int i = 0; i < N_GAMES; i++) labels[i] = games_name(i);
    labels[N_GAMES] = ui_help_label();                 /* 說明 is the last cell */
    static EXT_RAM_BSS_ATTR const char *icons[N_GAMES];                 /* ui_ring_set_icons keeps the pointer: static; filled by id so a new game (a games.def line) needs only a line in GAME_ICONS (else the games icon) */
    for (int i = 0; i < N_GAMES; i++) icons[i] = game_icon(GAMES[i].id);
    ui_ring_set_icons(ui_ring_create(p, labels, N_GAMES + 1, hub_pick, hub_back), icons, N_GAMES);
}
const ui_screen_t scr_games = { .name = "tools_games", .create = hub_create };

void games_cmd(const char *a)
{
    char id[24];
    int v;
    if (!a[0]) { tools_cmd("games"); return; }
    if (!strncmp(a, "fx", 2) && (!a[2] || a[2] == ' ')) { game_fx_cmd(a[2] ? a + 3 : ""); return; }            /* the open game's effects (game_fx.h) */
    if (!strncmp(a, "result", 6) && (!a[6] || a[6] == ' ')) { game_result_cmd(a[6] ? a + 7 : ""); return; }   /* its result card (game_result.h) */
    if (!strcmp(a, "ring")) { game_ring_open(); game_ring_print(); return; }                                    /* its ring (game_ring.h) */
    if (!strcmp(a, "sel")) { game_ring_print(); return; }
    if (sscanf(a, "rule %d", &v) == 1) { game_ring_set(GR_RULE, v); game_ring_print(); return; }
    if (sscanf(a, "style %d", &v) == 1) { game_ring_set(GR_STYLE, v); game_ring_print(); return; }
    if (sscanf(a, "mode %d", &v) == 1) { game_ring_set(GR_MODE, v); game_ring_print(); return; }
    if (!strncmp(a, "boot", 4) && (!a[4] || a[4] == ' ')) {                                                  /* the boot value of the seeds (G1 fix round 1) */
        unsigned long b;
        if (sscanf(a, "boot %lu", &b) == 1) games_boot_set((uint32_t)b);
        printf("APP games boot=%lu\n", (unsigned long)games_boot());
        return;
    }
    if (sscanf(a, "streak %d", &v) == 1) { game_result_streak_set(game_ring_id(), v); return; }
    if (!strcmp(a, "gold auto")) { game_result_gold_force(-1); return; }
    if (sscanf(a, "gold %d", &v) == 1) { game_result_gold_force(v); return; }
    if (!strcmp(a, "list")) {
        char ids[160] = "";
        for (int i = 0; i < N_GAMES; i++) { strncat(ids, i ? "," : "", sizeof ids - strlen(ids) - 1); strncat(ids, GAMES[i].id, sizeof ids - strlen(ids) - 1); }
        printf("APP games n=%d ids=%s\n", N_GAMES, ids);
    }
    else if (!strcmp(a, "help")) { tools_cmd("games"); hub_pick(N_GAMES); }
    else if (sscanf(a, "open %23s", id) == 1) {
        int i = games_find(id);
        if (i < 0) { printf("APP games unknown %s\n", id); return; }
        tools_cmd("games");
        hub_pick(i);
    }
    else if (game_ring_cmd(a)) return;                    /* the open game's own words (game_ring_t.cmd) */
    else printf("APP games unknown %s\n", a);
    printf("APP games top=%s\n", ui_top() ? ui_top()->name : "-");
}
