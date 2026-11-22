// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_paths.h"
#include <stdint.h>
/* Photo album (spec 2026-11-11-album-design.md): photos under /sdcard/photos (JPEG, subfolders too) are turned
 * once into 360x360 RGB565 frames in /sdcard/.album/<key>.565; the slideshow only reads those. Core 0, low
 * priority; JPEG decoding only at lowmem level 0, on USB power, when no cover is being decoded or library scanned. */
#define ALBUM_DIR PC_PHOTOS_DIR
#define ALBUM_CACHE PC_ALBUM_CACHE
#define ALBUM_PX 360
void album_svc_start(bool soon);       /* soon: scan and build ~30 s after boot (the now-playing screen shows photos) */
void album_svc_wake(void);             /* look at the card again (at most once a minute) and build what is missing */
int  album_svc_ready(void);            /* photos with a frame, any task */
int  album_svc_total(void);            /* photos found */
bool album_svc_building(void);
const char *album_svc_waiting(void);  /* frames missing and building waits: "battery", "lowmem", ...; else NULL */
/* load ready photo idx (0 .. ready-1, path order): cb runs in LVGL context with a new ALBUM_PX^2 RGB565 frame in
 * PSRAM that the receiver owns (heap_caps_free), or NULL when it could not be read. Returns the request id. */
typedef void (*album_cb)(uint32_t req, uint16_t *px, void *u);
uint32_t album_svc_load(int idx, album_cb cb, void *u);
void album_svc_stat(void);             /* console */
void album_svc_clear(void);            /* console test: delete every cache frame */
