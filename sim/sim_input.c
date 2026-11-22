// SPDX-License-Identifier: LGPL-2.1-or-later
/* Input: the knob (wheel / simknob), the touch pointer (mouse / simtap), and the window's keys.
 * Headless mode touches no SDL function; the window part (event filter, circle overlay) is only attached for a window. */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>
#include "lvgl.h"
#include "bsp/bsp_knob.h"
#include "bsp/bsp_geom.h"
#include "sim.h"
#include "cfg.h"
#include "pc/pc_rotate.h"

bsp_knob_hook_t sim_knob_hook(void);   /* shim_bsp.c: what the UI registered with bsp_knob_set_hook */

static int g_enc;                      /* detents the UI did not consume, handed to LVGL's encoder at the next read */
static struct { bool active, pressed; int x, y; } g_inj;   /* a headless simtap in progress */

static atomic_int g_keys;   /* window arrow keys: +1 top key, -1 bottom key (queued by the SDL filter, run on the main thread) */
static atomic_int g_wheel;             /* detents from the SDL event filter, drained by sim_input_poll() */
static atomic_bool g_key_shot, g_key_theme;

static void enc_read(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    d->state = LV_INDEV_STATE_RELEASED;
    d->enc_diff = (int16_t)g_enc;
    g_enc = 0;
}

static void ptr_read(lv_indev_t *i, lv_indev_data_t *d)
{
    if (g_inj.active) {
        d->point.x = g_inj.x;
        d->point.y = g_inj.y;
        d->state = g_inj.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    } else d->state = LV_INDEV_STATE_RELEASED;
}

void sim_knob(int detents)
{
    bsp_knob_hook_t h = sim_knob_hook();
    if (h && h(detents)) return;       /* the UI consumed it (volume, a page's own knob) */
    g_enc += detents;                  /* else focus navigation, as the board's driver does */
}

/* The FakePod's two side keys (make sim-fp): top = knob +, bottom = knob -, one detent per click; a key held longer than KEY_REPEAT_FIRST_MS repeats every
 * KEY_REPEAT_MS (the firmware's key driver does the same). ms = how long the key stays down in virtual time (0 = a click). */
#define KEY_REPEAT_FIRST_MS 400
#define KEY_REPEAT_MS       120
bool sim_key(const char *which, int ms)
{
    int d = !strcmp(which, "top") ? 1 : !strcmp(which, "bottom") ? -1 : 0;
    if (!d) return false;
#if !BSP_HAS_KNOB
    if (pc_rotate_map(PC_ROT_SQUARE, sim_rotation()).key_swap) d = -d;   /* rotate 1 (180 degrees): the physical upper key is the lower on screen */
#endif
    sim_knob(d);
    if (ms <= 0) { sim_advance_ms(2 * LV_DEF_REFR_PERIOD); return true; }
    int t = 0;
    while (t < ms) {
        int step = t < KEY_REPEAT_FIRST_MS ? KEY_REPEAT_FIRST_MS - t : KEY_REPEAT_MS;
        if (step > ms - t) step = ms - t;
        sim_advance_ms((uint32_t)step);
        t += step;
        if (t >= KEY_REPEAT_FIRST_MS && t < ms && (t - KEY_REPEAT_FIRST_MS) % KEY_REPEAT_MS == 0) sim_knob(d);
    }
    sim_advance_ms(2 * LV_DEF_REFR_PERIOD);
    return true;
}

static Uint32 win_id(void)
{
    lv_display_t *d = lv_display_get_default();
    SDL_Window *w = d ? lv_sdl_window_get_window(d) : NULL;
    return w ? SDL_GetWindowID(w) : 0;
}

/* motion, left button down, left button up at window pixel (wx, wy), pushed as a real desktop would deliver them */
static bool push_click(int wx, int wy)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_MOUSEMOTION;
    e.motion.windowID = win_id(); e.motion.x = wx; e.motion.y = wy;
    bool ok = SDL_PushEvent(&e) >= 0;
    SDL_zero(e);
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.windowID = win_id(); e.button.button = SDL_BUTTON_LEFT; e.button.state = SDL_PRESSED; e.button.clicks = 1; e.button.x = wx; e.button.y = wy;
    ok = ok && SDL_PushEvent(&e) >= 0;
    e.type = SDL_MOUSEBUTTONUP;
    e.button.state = SDL_RELEASED;
    return ok && SDL_PushEvent(&e) >= 0;
}

static int g_rot = -1, g_rot_override = -1;
void sim_rotation_override(int rotate) { g_rot_override = pc_rotate_norm(rotate); g_rot = g_rot_override; }
void sim_rotation_latch(void) { g_rot = g_rot_override >= 0 ? g_rot_override : cfg_rotation(); }
int sim_rotation(void) { return g_rot < 0 ? 0 : g_rot; }

/* a point on the screen as the user sees it -> the UI's own: the picture is the UI turned 180 degrees when rotate is 1, so undo that (pc_rotate.h); 0 changes nothing */
static void to_ui(int *x, int *y)
{
    if (!sim_headless() || !sim_rotation()) return;
    pc_rotate_screen(sim_rotation(), (int)lv_display_get_horizontal_resolution(lv_display_get_default()), *x, *y, x, y);
}

void sim_tap(int x, int y)
{
    to_ui(&x, &y);
    if (!sim_headless()) {
        float z = lv_sdl_window_get_zoom(lv_display_get_default());
        push_click((int)((float)x * z + z / 2), (int)((float)y * z + z / 2));   /* the middle of the zoomed pixel */
        sim_advance_ms(2 * LV_DEF_REFR_PERIOD);
        return;
    }
    g_inj.active = true;
    g_inj.x = x;
    g_inj.y = y;
    g_inj.pressed = true;
    sim_advance_ms(LV_DEF_REFR_PERIOD);
    g_inj.pressed = false;
    sim_advance_ms(LV_DEF_REFR_PERIOD);
    g_inj.active = false;
}

/* a press held for `ms` of virtual time, then released (a long press: LVGL's LONG_PRESSED comes after 400 ms) */
extern void ui_sleep(void);
static int s_off_after = -1;                                  /* simpress x y ms off: the screen goes off `off` ms into the press (tests: a hold must not go home in the dark) */
void sim_press_off(int x, int y, int ms, int off) { s_off_after = off; sim_press(x, y, ms); s_off_after = -1; }
void sim_press(int x, int y, int ms)
{
    to_ui(&x, &y);
    if (!sim_headless()) {
        float z = lv_sdl_window_get_zoom(lv_display_get_default());
        int wx = (int)((float)x * z + z / 2), wy = (int)((float)y * z + z / 2);
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = win_id(); e.motion.x = wx; e.motion.y = wy;
        SDL_PushEvent(&e);
        SDL_zero(e);
        e.type = SDL_MOUSEBUTTONDOWN;
        e.button.windowID = win_id(); e.button.button = SDL_BUTTON_LEFT; e.button.state = SDL_PRESSED; e.button.clicks = 1; e.button.x = wx; e.button.y = wy;
        SDL_PushEvent(&e);
        sim_advance_ms(ms);
        e.type = SDL_MOUSEBUTTONUP;
        e.button.state = SDL_RELEASED;
        SDL_PushEvent(&e);
        sim_advance_ms(2 * LV_DEF_REFR_PERIOD);
        return;
    }
    g_inj.active = true;
    g_inj.x = x;
    g_inj.y = y;
    g_inj.pressed = true;
    if (s_off_after >= 0 && s_off_after < ms) { sim_advance_ms(s_off_after); ui_sleep(); sim_advance_ms(ms - s_off_after); }
    else sim_advance_ms(ms);
    g_inj.pressed = false;
    sim_advance_ms(LV_DEF_REFR_PERIOD);
    g_inj.active = false;
}

/* a finger drag: down at (x0,y0), a straight line to (x1,y1) over `ms` in frame steps, up at the end */
void sim_drag(int x0, int y0, int x1, int y1, int ms)
{
    to_ui(&x0, &y0); to_ui(&x1, &y1);
    int steps = ms / sim_frame_ms();
    if (steps < 1) steps = 1;
    if (!sim_headless()) {
        float z = lv_sdl_window_get_zoom(lv_display_get_default());
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = win_id(); e.motion.x = (int)((float)x0 * z + z / 2); e.motion.y = (int)((float)y0 * z + z / 2);
        SDL_PushEvent(&e);
        SDL_zero(e);
        e.type = SDL_MOUSEBUTTONDOWN;
        e.button.windowID = win_id(); e.button.button = SDL_BUTTON_LEFT; e.button.state = SDL_PRESSED; e.button.clicks = 1; e.button.x = (int)((float)x0 * z + z / 2); e.button.y = (int)((float)y0 * z + z / 2);
        SDL_PushEvent(&e);
        sim_advance_ms(sim_frame_ms());
        for (int s = 1; s <= steps; s++) {
            SDL_zero(e);
            e.type = SDL_MOUSEMOTION;
            e.motion.windowID = win_id(); e.motion.state = SDL_BUTTON_LMASK;
            e.motion.x = (int)((float)(x0 + (x1 - x0) * s / steps) * z + z / 2); e.motion.y = (int)((float)(y0 + (y1 - y0) * s / steps) * z + z / 2);
            SDL_PushEvent(&e);
            sim_advance_ms(sim_frame_ms());
        }
        SDL_zero(e);
        e.type = SDL_MOUSEBUTTONUP;
        e.button.windowID = win_id(); e.button.button = SDL_BUTTON_LEFT; e.button.state = SDL_RELEASED; e.button.clicks = 1;
        e.button.x = (int)((float)x1 * z + z / 2); e.button.y = (int)((float)y1 * z + z / 2);
        SDL_PushEvent(&e);
        sim_advance_ms(2 * sim_frame_ms());
        return;
    }
    g_inj.active = true;
    g_inj.x = x0;
    g_inj.y = y0;
    g_inj.pressed = true;
    sim_advance_ms(sim_frame_ms());
    for (int s = 1; s <= steps; s++) {
        g_inj.x = x0 + (x1 - x0) * s / steps;
        g_inj.y = y0 + (y1 - y0) * s / steps;
        sim_advance_ms(sim_frame_ms());
    }
    g_inj.pressed = false;
    sim_advance_ms(sim_frame_ms());
    g_inj.active = false;
}

/* Like bsp_knob_start / bsp_touch_start: the default group, an encoder indev, and the pointer the UI listens to. */
lv_indev_t *sim_input_start(lv_display_t *disp)
{
    lv_group_t *grp = lv_group_create();
    lv_group_set_default(grp);
    lv_indev_t *enc = lv_indev_create();
    lv_indev_set_type(enc, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(enc, enc_read);
    lv_indev_set_display(enc, disp);
    lv_indev_set_group(enc, grp);

    /* Window mode keeps LVGL's own SDL mouse indev untouched: lv_sdl_mouse_handler finds it by its read callback, so replacing the callback
     * (as this once did, to inject simtap) made every real mouse click invisible. Headless has no SDL: a plain pointer indev fed by simtap. */
    lv_indev_t *ptr = sim_headless() ? lv_indev_create() : lv_sdl_mouse_create();
    if (sim_headless()) {
        lv_indev_set_type(ptr, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(ptr, ptr_read);
    }
    lv_indev_set_display(ptr, disp);
    return ptr;
}

static int SDLCALL win_filter(void *ud, SDL_Event *e)
{
    (void)ud;
    if (e->type == SDL_QUIT || (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_CLOSE)) { sim_quit(); return 0; }   /* dropped: LVGL's own handler would tear down behind our back */
    if (e->type == SDL_MOUSEWHEEL && e->wheel.y) atomic_fetch_add(&g_wheel, e->wheel.y < 0 ? 1 : -1);   /* one notch = one detent, down = clockwise */
#if !BSP_HAS_KNOB   /* the FakePod's two keys on the keyboard: Up = top key (knob +), Down = bottom key (knob -); the OS key repeat is the hold */
    if (e->type == SDL_KEYDOWN && (e->key.keysym.sym == SDLK_UP || e->key.keysym.sym == SDLK_DOWN)) atomic_fetch_add(&g_wheel, e->key.keysym.sym == SDLK_UP ? 1 : -1);
#endif
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {
        if (e->key.keysym.sym == SDLK_ESCAPE) sim_quit();
        else if (e->key.keysym.sym == SDLK_s) atomic_store(&g_key_shot, true);
        else if (e->key.keysym.sym == SDLK_t) atomic_store(&g_key_theme, true);
    }
    return 1;
}

/* A dark, half-transparent frame outside the 360 px circle: shows which corners the round panel cuts off.
 * On the system layer, so lv_snapshot_take (active screen only) never includes it. */
static void overlay(lv_display_t *disp)
{
    lv_obj_t *o = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, lv_display_get_horizontal_resolution(disp), lv_display_get_vertical_resolution(disp));
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_outline_width(o, 120, 0);
    lv_obj_set_style_outline_color(o, lv_color_black(), 0);
    lv_obj_set_style_outline_opa(o, LV_OPA_50, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

void sim_input_window(lv_display_t *disp)
{
    SDL_SetEventFilter(win_filter, NULL);
    if (BSP_LCD_ROUND) overlay(disp);   /* the square panel has no corners to mark (its rounded glass corners are the owner's to judge on the device) */
}

/* main-thread half of the window input: wheel detents and the S / T keys */
void sim_input_poll(void)
{
    int n = atomic_exchange(&g_wheel, 0);
    if (n) sim_knob(n);
    if (atomic_exchange(&g_key_theme, false)) sim_exec("theme next");
    if (atomic_exchange(&g_key_shot, false)) {
        static int seq;
        char cmd[96];
        mkdir(".local", 0777);
        mkdir(".local/sim", 0777);
        mkdir(".local/sim/shots", 0777);
        snprintf(cmd, sizeof cmd, "shot .local/sim/shots/manual_%d.png", ++seq);
        sim_exec(cmd);
    }
}

/* TEST ONLY (window mode): "simsdl click <x> <y> | wheel <n> | key <s|t|esc> | close | quit". Builds the SDL events a real desktop would deliver and pushes them
 * with SDL_PushEvent, so they travel the real path: event filter, SDL_PollEvent in LVGL's event timer, the mouse handler, sim_input_poll.
 * Coordinates are WINDOW pixels (zoom included). wheel n > 0 = away from the user (SDL's y > 0), n < 0 = toward the user (clockwise). */
void sim_sdl_inject(const char *arg)
{
    Uint32 id = win_id();
    if (!id) { printf("SIM simsdl no window\n"); return; }
    int x, y, n;
    char k[16];
    SDL_Event e;
    SDL_zero(e);
    int ok = 1;
    if (sscanf(arg, "click %d %d", &x, &y) == 2) ok = push_click(x, y);
    else if (sscanf(arg, "wheel %d", &n) == 1) {
        e.type = SDL_MOUSEWHEEL;
        e.wheel.windowID = id; e.wheel.y = n; e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        ok = SDL_PushEvent(&e) >= 0;
    } else if (sscanf(arg, "key %15s", k) == 1 && (!strcmp(k, "s") || !strcmp(k, "t") || !strcmp(k, "esc"))) {
        e.type = SDL_KEYDOWN;
        e.key.windowID = id; e.key.state = SDL_PRESSED; e.key.repeat = 0;
        e.key.keysym.sym = !strcmp(k, "s") ? SDLK_s : !strcmp(k, "t") ? SDLK_t : SDLK_ESCAPE;
        ok = SDL_PushEvent(&e) >= 0;
    } else if (!strcmp(arg, "close")) {   /* the window's X button: SDL_WINDOWEVENT_CLOSE */
        e.type = SDL_WINDOWEVENT;
        e.window.windowID = id; e.window.event = SDL_WINDOWEVENT_CLOSE;
        ok = SDL_PushEvent(&e) >= 0;
    } else if (!strcmp(arg, "quit")) {    /* SDL_QUIT: the session / Alt-F4 route */
        e.type = SDL_QUIT;
        ok = SDL_PushEvent(&e) >= 0;
    } else { printf("SIM simsdl usage: click <x> <y> | wheel <n> | key <s|t|esc> | close | quit\n"); return; }
    printf(ok ? "SIM simsdl %s\n" : "SIM simsdl push failed %s\n", arg);
}
