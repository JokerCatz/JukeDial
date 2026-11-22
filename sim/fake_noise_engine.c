// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes noise_engine (white-noise scenes mixed from library.muk). */
/* Behaviour: state only (set up at the first call, whatever the order) - four fixed scenes of three layers (or the set "simnoise" asks for: more scenes, more layers in scene 0, scene 0 custom), the layer on/off set (real pc_layerset: at most 3 on), layer volumes, which scene is "playing"; six sounds in two categories for the add-sound pages;
 * save as a custom scene (inserted after the custom ones, as the real one; "SIM noise saved <i>"), add a sound (a new layer named after it; -EEXIST for a layer of that name), delete a custom scene ("SIM noise delete <i>", removed, later indices move).
 * "simnoise own": from then on a started scene takes the player as the real engine does (its now_ops: title, favourite id, stop, list = the scene page) and the fake audio is "playing" (a mixer source); default off: the scene is only state. */
/* Empty: all audio, library.muk, nothing is written to the card. noise_scene() is NULL out of range, as the real one. */
#include "noise_engine.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "audio_svc.h"
#include "font.h"
#include "pc/pc_layerset.h"
#include "player_ctl.h"
#include "sim.h"
#include "ui.h"

#define MAXSCENES 40
#define NLAYERS 3
#define NSOUNDS 6

static const char *const k_scene[4] = { "rain", "forest", "waves", "campfire" };
static const char *const k_scene_zh[4] = { "夜雨", "森林", "海浪", "營火" };
static const char *const k_layer[4][NLAYERS] = {
    { "Rain", "Thunder", "Wind" }, { "Birds", "Leaves", "Stream" }, { "Surf", "Gulls", "Breeze" }, { "Fire", "Crickets", "Night" },
};
static const char *const k_sound[NSOUNDS] = { "Rain", "Thunder", "Wind", "Birds", "Surf", "Fire" };
static const char *const k_cat[2] = { "Weather", "Places" };

static pc_scene_t s_scenes[MAXSCENES];
static pc_layerset_t s_set[MAXSCENES];
static float s_vol[PC_SCENE_MAX_LAYERS];
static int s_cur = -1, s_n = 4, s_layers0 = NLAYERS;
static bool s_custom0, s_ready, s_own;

static void ensure(void)
{
    if (s_ready) return;
    for (int i = 0; i < s_n; i++) {
        pc_scene_t *s = &s_scenes[i];
        const int nl = i == 0 ? s_layers0 : NLAYERS;
        if (i < 4) snprintf(s->id, sizeof s->id, "%s", k_scene[i]);
        else snprintf(s->id, sizeof s->id, "scene%02d", i);
        if (i == 10) snprintf(s->name, sizeof s->name, "%s", "Rain on a tin roof far away in the hills");   /* a name longer than a list row */
        else if (i < 4) snprintf(s->name, sizeof s->name, "%s", k_scene_zh[i]);
        else snprintf(s->name, sizeof s->name, "情境 %02d", i);
        s->nlayers = nl;
        s->custom = i == 0 && s_custom0;
        for (int j = 0; j < nl; j++) s->layer[j].volume = 0.8f;
        pc_layerset_init(&s_set[i], nl, NOISE_MAX_ON);
    }
    for (int j = 0; j < PC_SCENE_MAX_LAYERS; j++) s_vol[j] = 0.8f;
    s_ready = true;
}

/* "simnoise <scenes> <layers of scene 0> [custom]": the scene set from now on; "simnoise ?" prints the playing scene (scenes 4..MAXSCENES, layers 1..PC_SCENE_MAX_LAYERS); nothing plays afterwards */
bool fake_noise_set(const char *arg)
{
    if (!strcmp(arg, "?")) { printf("SIM noise current=%d\n", s_cur); return true; }   /* "simnoise ?": which scene plays */
    if (!strcmp(arg, "own")) { s_own = true; return true; }                               /* "simnoise own": a start takes the player */
    int n = 0, l = 0;
    char c[16] = "";
    if (sscanf(arg, "%d %d %15s", &n, &l, c) < 2 || n < 4 || n > MAXSCENES || l < 1 || l > PC_SCENE_MAX_LAYERS) return false;
    s_n = n;
    s_layers0 = l;
    s_custom0 = c[0] == 'c';
    s_cur = -1;
    s_ready = false;
    ensure();
    return true;
}

int noise_load(void) { ensure(); return s_n; }
int noise_scene_count(void) { ensure(); return s_n; }
const pc_scene_t *noise_scene(int i) { ensure(); return i >= 0 && i < s_n ? &s_scenes[i] : NULL; }
const char *noise_scene_name(int i) { ensure(); return i >= 0 && i < s_n ? pc_pick_name(s_scenes[i].name, s_scenes[i].en) : ""; }
static const now_ops_t s_ops;
static void forget(void) { s_cur = -1; }
static int s_breath = 1000;
static void redo_start(int a, int b) { (void)b; noise_engine_start(a); }
int noise_engine_start(int scene)
{
    if (!noise_scene(scene)) return -1;
    if (spk_excl_gate(redo_start, scene, 0)) return -1;     /* as noise_engine.c: speaker mode asks first */
    s_breath = 1000;
    s_cur = scene;
    if (!s_own) return 0;
    player_ctl_take(&s_ops, forget, NULL);
    audio_svc_play_mixer(NULL, NULL, 44100, NULL, NULL);
    player_ctl_changed();
    return 0;
}
int noise_engine_toggle_layer(int scene, int layer) { return noise_scene(scene) ? pc_layerset_toggle(&s_set[scene], layer) : -1; }
noise_layer_state_t noise_engine_layer_state(int scene, int layer)
{
    if (!noise_scene(scene) || layer < 0 || layer >= s_scenes[scene].nlayers) return NOISE_LAYER_OFF;
    return s_set[scene].on[layer] ? NOISE_LAYER_ON : NOISE_LAYER_OFF;
}
const char *noise_layer_name(int scene, int layer)
{
    static char b[24];
    if (!noise_scene(scene) || layer < 0 || layer >= s_scenes[scene].nlayers) return "";
    if (s_scenes[scene].layer[layer].sha[0]) return s_scenes[scene].layer[layer].sha;   /* an added sound: its name (the fake keeps it in the sha field) */
    if (scene < 4 && layer < NLAYERS) return k_layer[scene][layer];
    snprintf(b, sizeof b, "Extra %d", layer + 1);
    return b;
}
void noise_engine_set_layer_volume(int layer, float v)
{
    if (layer < 0 || layer >= PC_SCENE_MAX_LAYERS) return;
    s_vol[layer] = v < 0 ? 0 : v > 1 ? 1 : v;
}
float noise_engine_layer_volume(int layer) { return layer >= 0 && layer < PC_SCENE_MAX_LAYERS ? s_vol[layer] : 0; }
void noise_engine_set_breath(int permille) { s_breath = permille < 500 ? 500 : permille > 1000 ? 1000 : permille; }
int noise_engine_breath(void) { return s_breath; }
void noise_engine_stop(bool fade_10s)
{
    (void)fade_10s;
    s_breath = 1000;
    const bool owned = s_own && s_cur >= 0 && player_ctl_now() == &s_ops;
    s_cur = -1;
    if (owned) { audio_svc_stop(false); player_ctl_release(); }
}
int noise_current(void) { return s_cur; }
static int custom_count(void) { int n = 0; while (n < s_n && s_scenes[n].custom) n++; return n; }
int noise_save_custom(int scene)                    /* as the real one: after the custom scenes, later indices move, the saved one plays if this one did */
{
    const pc_scene_t *sc = noise_scene(scene);
    if (!sc || s_n >= MAXSCENES || !sc->nlayers) return -1;
    const int k = custom_count();
    pc_scene_t n = *sc;
    snprintf(n.id, sizeof n.id, "custom:%d", k + 1);
    snprintf(n.name, sizeof n.name, "%s %d", T(S_CUSTOM) /* 自訂 */, k + 1);
    n.custom = true;
    for (int j = 0; j < n.nlayers; j++) snprintf(n.layer[j].sha, sizeof n.layer[j].sha, "%s", noise_layer_name(scene, j));
    const bool was_playing = s_cur == scene;
    memmove(&s_scenes[k + 1], &s_scenes[k], (size_t)(s_n - k) * sizeof *s_scenes);
    memmove(&s_set[k + 1], &s_set[k], (size_t)(s_n - k) * sizeof *s_set);
    s_scenes[k] = n;
    pc_layerset_init(&s_set[k], n.nlayers, NOISE_MAX_ON);
    s_n++;
    if (s_cur >= k) s_cur++;
    printf("SIM noise saved %d\n", k);
    if (was_playing) noise_engine_start(k);
    return k;
}
int noise_engine_add_sound(int scene, int sound)
{
    if (!noise_scene(scene) || sound < 0 || sound >= NSOUNDS) return -EINVAL;
    pc_scene_t *sc = &s_scenes[scene];
    for (int j = 0; j < sc->nlayers; j++) if (!strcmp(noise_layer_name(scene, j), k_sound[sound])) return -EEXIST;
    if (sc->nlayers >= PC_SCENE_MAX_LAYERS) return -ENOSPC;
    const int k = pc_layerset_add(&s_set[scene]);
    if (k < 0 || k != sc->nlayers) return -ENOSPC;
    memset(&sc->layer[k], 0, sizeof sc->layer[k]);
    sc->layer[k].volume = 0.7f;
    snprintf(sc->layer[k].sha, sizeof sc->layer[k].sha, "%s", k_sound[sound]);
    sc->nlayers++;
    printf("SIM noise add sound=%s layer=%d\n", k_sound[sound], k);
    return s_set[scene].on[k] ? 0 : 1;
}
bool noise_scene_modified(int scene) { (void)scene; return false; }
int noise_delete_custom(int scene)
{
    if (!noise_scene(scene) || !s_scenes[scene].custom) return -1;
    printf("SIM noise delete %d\n", scene);
    if (s_cur == scene) noise_engine_stop(false);
    memmove(&s_scenes[scene], &s_scenes[scene + 1], (size_t)(s_n - scene - 1) * sizeof *s_scenes);
    memmove(&s_set[scene], &s_set[scene + 1], (size_t)(s_n - scene - 1) * sizeof *s_set);
    s_n--;
    if (s_cur > scene) s_cur--;
    return 0;
}
int noise_cat_count(void) { return 2; }
const char *noise_cat_name(int cat) { return cat >= 0 && cat < 2 ? k_cat[cat] : ""; }
int noise_cat_sounds(int cat, int *out, int max)
{
    int n = 0;
    for (int i = cat == 1 ? 3 : 0; i < (cat == 1 ? NSOUNDS : 3) && cat >= 0 && cat < 2 && n < max; i++) out[n++] = i;
    return n;
}
const char *noise_sound_name(int sound) { return sound >= 0 && sound < NSOUNDS ? k_sound[sound] : ""; }

/* the real engine's player hooks (noise_engine.c), used while "simnoise own" is on */
static void title(char *out, size_t cap) { snprintf(out, cap, "%s", s_cur >= 0 ? s_scenes[s_cur].name : ""); }
static bool fav_id(char *out, size_t cap) { if (s_cur < 0) return false; snprintf(out, cap, "scene:%s", s_scenes[s_cur].id); return true; }
static bool playing(void) { return s_cur >= 0; }
static void stop_now(void) { noise_engine_stop(false); }
static void open_layers(void) { extern const ui_screen_t scr_scene; if (s_cur >= 0) ui_open_once(&scr_scene, (void *)(intptr_t)s_cur); }
static const char *layers_label(void) { return T(S_SOUNDS) /* 聲音 */; }
static const now_ops_t s_ops = { .src = NOW_SRC_NOISE, .title = title, .fav_id = fav_id, .stop = stop_now, .playing = playing,
                                 .open_list = open_layers, .list_label = layers_label, .no_art = true };
