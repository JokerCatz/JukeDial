// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The power-on picture: ONE JPEG from the card, drawn once, made to breathe by ramping the backlight: 0 -> the user's brightness
 * -> a third of it -> the user's brightness, then the UI. Pure rules here (host-tested); the drawing and the boot hooks are app/main/splash.c.
 * Changeable defaults: the constants below. */
#define PC_SPLASH_UP1_MS   700u    /* 0 -> user */
#define PC_SPLASH_DOWN_MS  600u    /* user -> user / PC_SPLASH_LOW_DIV */
#define PC_SPLASH_UP2_MS   700u    /* back up to user */
#define PC_SPLASH_LOW_DIV  3u
#define PC_SPLASH_TOTAL_MS (PC_SPLASH_UP1_MS + PC_SPLASH_DOWN_MS + PC_SPLASH_UP2_MS)
#define PC_SPLASH_NSEG     3
#define PC_SPLASH_MAX_FILE (1u << 20)   /* a bigger file is not read (the card read is bounded); the decoder's own limits (8 MiB of RGB565) come after */

/* the boot modes that can show a picture: the file name stem under PC_SPLASH_DIR */
extern const char *const pc_splash_modes[];   /* NULL terminated: "player" "usb" "inst" "cast" "charge" */

/* Lookup order of the files for a boot in `mode`: i = 0 the per-mode file, i = 1 the general one, i >= 2 none (false).
 * false too for a mode name that is not [a-z0-9_]+ (no path tricks) or a buffer that is too small. */
bool pc_splash_path(const char *mode, int i, char *out, size_t cap);

/* one segment of the ramp: from the end of the previous one (the first starts at 0) to to_pct over ms; false when i is out of 0..NSEG-1.
 * The low point is never 0 for a user brightness above 0 (an AMOLED's 0 % is display-off). */
typedef struct { uint8_t to_pct; uint16_t ms; } pc_splash_seg_t;
bool pc_splash_seg(int i, uint8_t user_pct, pc_splash_seg_t *s);
/* the segment running at `elapsed_ms` since the start (PC_SPLASH_NSEG: the cycle is over) */
int  pc_splash_seg_at(uint32_t elapsed_ms);
/* the brightness (percent) at elapsed_ms, straight lines between the segment ends in the perceived (square-root) domain; user_pct after the end */
uint8_t pc_splash_level(uint32_t elapsed_ms, uint8_t user_pct);
#ifdef __cplusplus
}
#endif
