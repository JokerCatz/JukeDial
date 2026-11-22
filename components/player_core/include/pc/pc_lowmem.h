// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Low-memory degradation (JC 2026-11-10, the trade-off notes): when internal RAM runs low, drop the looks first,
 * sound never. Levels add up:
 *   1 NO_COVER    no JPEG cover decode (the decoder needs ~45 KB): default disc instead
 *   2 STILL       cover stops turning, spectrum stops redrawing
 *   3 NO_BACKGND  defer background work (stats save, library rescan)
 * Down at once on a low sample; up one level only after PC_LOWMEM_CALM_MS above threshold + hysteresis. */
enum { PC_LOWMEM_OK = 0, PC_LOWMEM_NO_COVER = 1, PC_LOWMEM_STILL = 2, PC_LOWMEM_NO_BACKGND = 3 };
#define PC_LOWMEM_COVER_FREE   (100 * 1024)   /* level 1 below this free total ... */
#define PC_LOWMEM_COVER_BLOCK  (52 * 1024)    /* ... or below this largest block */
#define PC_LOWMEM_STILL_FREE   (48 * 1024)
#define PC_LOWMEM_BACKGND_FREE (32 * 1024)
#define PC_LOWMEM_HYST         (16 * 1024)
#define PC_LOWMEM_CALM_MS      5000
/* night3 A: the square panel's sharp cover (a 296 / 460 px decode, thumb_svc_request_big). BIG_OK: the picture is made at all only while the internal heap is above the level-1 line
 * (the stretched thumbnail stays otherwise, logged). BIG_ESP_OK: esp_new_jpeg may make it only when it also leaves the line standing after its transient: board 2026-11-21 (FakePod, style B,
 * the 460 px decode of an esp_new_jpeg picture) took internal free from 244 KB to a minimum of 35 KB, a ~209 KB peak, so on a heap like that the decoder that needs no internal RAM (ours) makes it. */
#define PC_LOWMEM_BIG_ESP_PEAK (209 * 1024)
bool pc_lowmem_big_ok(size_t free_b, size_t largest_b);
bool pc_lowmem_big_esp_ok(size_t free_b, size_t largest_b);
typedef struct { int level; uint32_t calm_since; bool calm; } pc_lowmem_t;
int pc_lowmem_update(pc_lowmem_t *m, size_t free_b, size_t largest_b, uint32_t now_ms);   /* returns the level */
#ifdef __cplusplus
}
#endif
