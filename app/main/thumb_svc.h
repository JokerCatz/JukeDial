// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_thumbbg.h"
/* Cover thumbnails (spec 4.5, 5.7): 240x240 RGB565 from the track's embedded picture or an image file next to it (JPEG: by its route,
 * jpeg_route.h - the S31's JPEG engine, esp_new_jpeg scaled decoding, or our decoder for the layouts both refuse; PNG: libpng) on core 0
 * at low priority. hires T8c: kept in one pack, /sdcard/_player/index/thumbs.pak (pc_thumbpak.h; the
 * embedded pictures by their bytes, so an album's identical pictures share one entry), written by the background pass (every library
 * cover missing from the pack, after an index load or a scan; hires T8e: while stopped and while a source of <= 48 kHz plays, pc_tbg_rule) and by requests: a request is served from the
 * pack with one file read; a missing cover is made now only for the playing track (thumb_svc_request_now) or when nothing plays.
 * While a source above 48 kHz plays (pc_thumb_hires_pause on audio_svc_info().src_rate) no cover is decoded: a request that the pack
 * cannot answer gets NULL (the placeholder) at once and is served again (the same req) when the pause ends, if it is still the newest. */
typedef void (*thumb_cb)(uint32_t req, uint16_t *pixels, void *u);   /* LVGL context; pixels NULL = none (yet); caller frees with heap_caps_free */
void thumb_svc_start(void);
bool thumb_svc_busy(void);   /* a thumbnail is being loaded or decoded for a request (the spectrum pauses meanwhile) */
void thumb_svc_bench(void);  /* console test: decode every cover without the pack, print the times */
/* newest request wins; apic_len 0: no embedded picture (an image file next to path may be the cover). _now: the playing track's cover
 * (made now when missing, at <= 48 kHz, also while music plays) */
uint32_t thumb_svc_request(const char *path, uint32_t apic_off, uint32_t apic_len, thumb_cb cb, void *u);
uint32_t thumb_svc_request_now(const char *path, uint32_t apic_off, uint32_t apic_len, thumb_cb cb, void *u);
/* the phone's cover art (speaker mode): a JPEG in memory (copied to PSRAM first), the same decoder and low-memory gate, never written to
 * the pack, not held by the hi-res pause (no card read); 0: not queued (no memory) */
uint32_t thumb_svc_request_jpeg(const uint8_t *jpg, uint32_t len, thumb_cb cb, void *u);
/* fppolish: the square panel's now-playing page asks for the playing track's cover at a larger size (296 / 460 px, `size` in 240..THUMB_BIG_MAX), size x size RGB565 in PSRAM, made from the track's
 * embedded picture or the image file next to it (or the phone's JPEG) by the same decoders, lock and gates; never read from or written to the pack (the card keeps 240 px) and never made while a
 * source above 48 kHz plays or is still opening: NULL then (the caller keeps the 240 px picture). Newest request wins, as above; 0: not queued. */
#define THUMB_BIG_MAX 480
uint32_t thumb_svc_request_big(const char *path, uint32_t apic_off, uint32_t apic_len, int size, thumb_cb cb, void *u);
uint32_t thumb_svc_request_jpeg_big(const uint8_t *jpg, uint32_t len, int size, thumb_cb cb, void *u);
bool thumb_svc_low_mem(uint32_t req);   /* request req was answered NULL because internal RAM was too low for the decoder (not a broken picture) */
bool thumb_jpeg_lock(int timeout_ms);   /* the JPEG decoder (covers, album): -1 waits; true = taken */
void thumb_jpeg_unlock(void);
bool thumb_svc_paused(void);            /* any task: cover decoding is paused now (a source above 48 kHz plays); 現況 says so */
void thumb_svc_library_changed(void);   /* LVGL context, after an index load or a scan that succeeded: the background pass over the new library */
void thumb_svc_cache_stat(void);        /* console: APP thumbcache stat count=<entries> bytes=<n> ... missing=<covers not in the pack> ... */
void thumb_svc_cache_build(void);       /* console: forget the bad marks (the user's retry), then a pass over every track (in either mode, by the pass's rule); refused (with the reason) while hi-res plays */
/* hires T8e: the 封面快取 page and 現況. progress: any task (the thumb task publishes it after every step). set_auto: the setting cover_auto (1 自動,
 * 0 手動; boot reads it); user_pause: the page's 暫停 / 繼續 (this boot); build_now: 立即建置 - the bad marks forgotten, a pass over every track in
 * either mode (held, with the reason, while hi-res plays), the page's 暫停 cleared. All three from the LVGL task. */
void thumb_svc_progress(pc_tbg_prog_t *out);
void thumb_svc_progress_want(void);   /* fix 1 (review I2): the page / 現況 need the numbers: the pack's table may then be read (never during hi-res) */
void thumb_svc_set_auto(bool on);
void thumb_svc_user_pause(bool on);
void thumb_svc_build_now(void);
