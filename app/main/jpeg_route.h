// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_jpeg.h"
/* hires task 8d: the JPEG paths shared by the covers (thumb_svc.c) and the album (album_svc.c), under thumb_jpeg_lock.
 *   pc_jpeg_probe NATIVE -> the S31's JPEG engine (esp_driver_jpeg: DMA2D into PSRAM, no internal RAM per picture), when its full-size
 *                           output fits JPEG_HW_MAX_BYTES; otherwise, or when the engine refuses it, esp_new_jpeg (the caller's scaled
 *                           path, ~45 KB internal RAM while it runs)
 *   pc_jpeg_probe OWN    -> pc_jpeg_decode (our decoder, everything in PSRAM): ffmpeg's 1x2 4:4:4, 4:4:0, chroma that is not 1x1...
 *   PROGRESSIVE / UNSUPPORTED / BAD -> not decoded; the route name is the reason in the log.
 * The engine is made once at boot (jpeg_route_init, before the heap is cut up) and kept: its few KB of internal RAM are printed then. */
#define JPEG_HW_MAX_BYTES (8u << 20)   /* the engine writes the whole picture: 2048 x 2048 RGB565 at most */
void jpeg_route_init(void);
bool jpeg_route_hw_ready(void);
/* NATIVE pictures only: w x h RGB565 (little endian) in PSRAM, rows packed (the engine's MCU padding removed), or NULL with *why
 * ("no_engine" "too_big" "no_psram" "refused") */
uint16_t *jpeg_route_hw(const uint8_t *jpg, uint32_t len, const pc_jpeg_info_t *in, int *w, int *h, const char **why);
/* our decoder into PSRAM, scaled so the short side stays >= min_short; *rc a PC_JPEG_ result */
/* night3 B: a hook our decoder calls after every MCU / output row (NULL: none). Set by the caller that holds the JPEG lock (thumb_svc: it yields the core, pc_yield.h) and put back after. */
void jpeg_route_set_poll(void (*poll)(void));
uint16_t *jpeg_route_own(const uint8_t *jpg, uint32_t len, int min_short, int *w, int *h, int *rc);
