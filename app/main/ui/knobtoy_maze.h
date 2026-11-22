// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "game_style.h"
#include "pc/pc_game_maze.h"
/* The maze toys of the knob-toys page (ideas G6b, rotating maze): the page's rules gravity / repel / attract / tilt on ONE shared field (ui_arena, not full). The maze never turns on screen:
 * the walls are objects set once (arcs of the rings, rects of the tilt grid; only a drifting gap redraws its ring), only the marble and the rim's direction marker move.
 * Rules: pc_game_maze (host-tested). This file only composes the shared field; the knobtoy page owns the HEAD row, the status line, the card and the exit. */
typedef struct {
    void (*status)(void);                 /* the text changed: the page reads kt_maze_text */
    void (*finished)(void);               /* the marble is out / in / at the goal: the page shows the card */
    void (*paused)(bool on);
    void (*again)(void);                  /* a tap on the field after the end */
} kt_maze_host_t;
lv_obj_t *kt_maze_create(lv_obj_t *page, const kt_maze_host_t *host);   /* the field (NULL: no memory); kt_maze_field() is its guarded pointer (NULL once deleted) */
lv_obj_t *kt_maze_field(void);
void kt_maze_new(int rule, int drift, uint32_t seed, bool gold, const game_style_t *st);   /* rule: pc_game_maze's PC_MZ_*; a new game on the field made; the sensor's vector is kept */
void kt_maze_style(const game_style_t *st, bool gold);
const pc_mz_t *kt_maze_game(void);
void kt_maze_text(char *b, size_t n, char *low, size_t m);   /* the line above the field (progress) and the line below it (tap to start / the end) */
void kt_maze_rezero(void);                             /* the tilt rule: take the pose held now as level (imu_svc_zero; the "hold still" message when the device is moving) */
void kt_maze_grav(float dx, float dy);                 /* the vector input: the IMU's tilt (FakePod, later), the console */
bool kt_maze_cmd(const char *arg);                     /* console `maze <arg>`: "grav <dx> <dy>" (each -100..100, percent of a full tilt); true: taken */
