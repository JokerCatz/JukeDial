// SPDX-License-Identifier: LGPL-2.1-or-later
/* Desktop simulator entry: argument parsing, display setup, main loop, one shutdown() for every exit path. */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include "lvgl.h"
#include "sim.h"

void sim_boot(lv_display_t *disp);   /* sim_boot.c */

#include "bsp/bsp_geom.h"   /* the panel: 360 round (the board) or 460 square (make sim-fp) */
#define W BSP_LCD_H_RES
#define H BSP_LCD_V_RES

const char *g_sim_mode = "player";
static bool g_headless;
static uint32_t g_seed = 1;
static bool g_lv_up;
static uint8_t g_buf[W * 72 * 2] __attribute__((aligned(64)));   /* headless draw buffer (RGB565, 72 rows) */

bool sim_headless(void) { return g_headless; }
uint32_t sim_seed(void) { return g_seed; }

/* The only teardown. Reached from every exit: commands done, quit/Esc/window close, selftest, bad arguments. */
static void shutdown(void)
{
    if (g_lv_up) { sim_ui_close(); sim_font_shutdown(); lv_deinit(); g_lv_up = false; }   /* also closes the SDL window when there is one */
    sim_set_root(NULL);
}

static void flush_null(lv_display_t *d, const lv_area_t *a, uint8_t *px) { (void)a; (void)px; lv_display_flush_ready(d); }

static lv_display_t *g_disp;

static bool display_up(int zoom)
{
    if (g_headless) {   /* no SDL call at all in this mode */
        lv_display_t *d = lv_display_create(W, H);
        if (!d) return false;
        g_disp = d;
        lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(d, g_buf, NULL, sizeof g_buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(d, flush_null);
        return true;
    }
    lv_display_t *d = lv_sdl_window_create(W, H);
    if (!d) return false;
    g_disp = d;
    lv_sdl_window_set_zoom(d, (float)zoom);
    lv_tick_set_cb(NULL);                  /* the SDL driver installed SDL_GetTicks; time is ours (sim_advance_ms) */
    sim_input_window(d);
    return true;
}

static int64_t host_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void run_cmds(const char *cmds)
{
    char *dup = strdup(cmds), *save = NULL;
    for (char *c = strtok_r(dup, ";", &save); c && !sim_quitting(); c = strtok_r(NULL, ";", &save)) sim_exec(c);
    free(dup);
}

/* windowed: advance by the real elapsed time; stdin is polled without blocking */
static void window_loop(bool use_stdin)
{
    char line[4096];                                                 /* longer than sim_exec accepts (511): it reports "line too long" instead of a silent cut */
    size_t n = 0;
    int64_t last = host_ms();
    while (!sim_quitting()) {
        struct pollfd p = { .fd = 0, .events = POLLIN };
        if (use_stdin && poll(&p, 1, 0) > 0) {
            char ch;
            bool ran = false;                                        /* after a command the loop body below runs once, so its events (and any SDL event it pushed) are handled before the next queued line */
            ssize_t r = read(0, &ch, 1);
            if (r == 0) {                                            /* EOF: run an unterminated last line, keep the window */
                use_stdin = false;
                if (n) { line[n] = 0; n = 0; sim_exec(line); ran = true; }
            }
            else if (r == 1 && ch == '\n') { line[n] = 0; n = 0; sim_exec(line); ran = true; }
            else if (r == 1 && n < sizeof line - 1) line[n++] = ch;
            if (!ran) continue;
        }
        sim_input_poll();
        int64_t now = host_ms();
        if (now > last) {   /* after a host suspend the gap can be hours: catch up by at most 1 s */
            sim_advance_ms((uint32_t)(now - last > 1000 ? 1000 : now - last));
            last = now;
        }
        else usleep(1000);
    }
}

static int parse_time(const char *s, int64_t *out)
{
    struct tm tm = { 0 };
    int n = sscanf(s, "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    if (n < 5) return -1;
    if (tm.tm_mon < 1 || tm.tm_mon > 12 || tm.tm_mday < 1 || tm.tm_mday > 31 || tm.tm_hour < 0 || tm.tm_hour > 23 ||
        tm.tm_min < 0 || tm.tm_min > 59 || tm.tm_sec < 0 || tm.tm_sec > 59) return -1;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    *out = timegm(&tm);
    return 0;
}

int main(int argc, char **argv)
{
    const char *root = ".local/sim/play", *cmds = NULL;
    int zoom = 2, rc = 0;
    bool selftest = false, have_time = false;
    int64_t wall = SIM_DEFAULT_WALL_S;

    for (int i = 1; i < argc && !rc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(a, "--headless")) g_headless = true;
        else if (!strcmp(a, "--selftest")) selftest = true;
        else if (!strcmp(a, "--root") && more) root = argv[++i];
        else if (!strcmp(a, "--cmd") && more) cmds = argv[++i];
        else if (!strcmp(a, "--zoom") && more) zoom = atoi(argv[++i]);
        else if (!strcmp(a, "--mode") && more) {
            const char *m = argv[++i];
            if (!strcmp(m, "player") || !strcmp(m, "inst") || !strcmp(m, "usb") || !strcmp(m, "ext") || !strcmp(m, "cast") || !strcmp(m, "safe")) g_sim_mode = !strcmp(m, "cast") ? "cast" : !strcmp(m, "inst") ? "inst" : !strcmp(m, "usb") || !strcmp(m, "ext") ? "ext" : !strcmp(m, "safe") ? "safe" : "player";
            else { fprintf(stderr, "usage: sim [--root DIR] [--headless] [--zoom N] [--mode player|inst|ext|cast|safe] [--time \"YYYY-MM-DD HH:MM[:SS]\"] [--seed N] [--rotate 0|1] [--cmd \"a; b\"] [--selftest]\n"); rc = 2; }
        }
        else if (!strcmp(a, "--rotate") && more) sim_rotation_override(atoi(argv[++i]));
        else if (!strcmp(a, "--seed") && more) g_seed = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--time") && more) {
            if (parse_time(argv[++i], &wall)) { fprintf(stderr, "SIM bad --time %s\n", argv[i]); rc = 2; }
            have_time = true;
        } else { fprintf(stderr, "usage: sim [--root DIR] [--headless] [--zoom N] [--mode player|inst|ext|cast|safe] [--time \"YYYY-MM-DD HH:MM[:SS]\"] [--seed N] [--rotate 0|1] [--cmd \"a; b\"] [--selftest]\n"); rc = 2; }
    }
    { int w = sim_state_int("wall_s", -1); if (w >= 0) { wall = w; have_time = true; } }   /* SIM_STATE wall_s=<epoch s>: boot with that wall clock (0 = the unset clock of a board without a battery clock) */
    if (!rc && selftest) g_headless = true;   /* the self-test needs no window and no SDL */
    if (!rc && !selftest) {
        struct stat st;
        if (stat(root, &st) || !S_ISDIR(st.st_mode)) { printf("SIM root not found: %s\n", root); rc = 2; }
    }
    if (!rc) {
        if (zoom < 1) zoom = 1;
        sim_set_root(root);
        sim_clock_init(wall, !g_headless && !have_time);
        srand(g_seed);
        lv_init();
        g_lv_up = true;
        if (!display_up(zoom)) { fprintf(stderr, "SIM display init failed\n"); rc = 1; }
    }
    if (!rc) {
        if (selftest) rc = sim_selftest();
        else if (g_headless) {
            sim_boot(g_disp);
            if (cmds) run_cmds(cmds);
            else {
                char *ln = NULL;
                size_t cap = 0;
                while (!sim_quitting() && getline(&ln, &cap, stdin) > 0) sim_exec(ln);
                free(ln);
            }
        } else {
            sim_boot(g_disp);
            if (cmds) run_cmds(cmds);
            window_loop(!cmds);
        }
    }
    shutdown();
    return rc;
}
