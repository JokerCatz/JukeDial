// SPDX-License-Identifier: LGPL-2.1-or-later
#include "thumb_svc.h"
#include "pc/pc_paths.h"
#include "library_svc.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "audio_svc.h"
#include "bt_svc.h"
#include "cfg.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_vfs_fat.h"
#include "jpeg_route.h"
#include "lowmem.h"
#include "pc/pc_lowmem.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "pc/pc_thumb.h"
#include "pc/pc_thumbpak.h"
#include "pc/pc_thumbbg.h"
#include "pc/pc_yield.h"
#include "png.h"
#include "ui.h"

#define S PC_THUMB_SIZE
#define APIC_MAX (4u << 20)
#define IMG_MAX (8u << 20)                 /* a cover image file bigger than this is skipped */
#define PNG_RGB_MAX (12u << 20)            /* a PNG whose RGB888 picture is bigger than this is skipped (about 2000 x 2000) */
#define PX_BYTES ((size_t)S * S * 2)
#define CARD PC_SD
#define PAK PC_THUMB_PAK
#define PAK_NEW PC_THUMB_PAK_NEW   /* a compaction in progress */
#define DIR_NAMES 96                        /* image files remembered per folder */
#define DIR_POOL 4096
#define REQ_NOW 0x80000000u                 /* job_t.len: the request is for the playing track (thumb_svc_request_now) */
#define ALIGN_UP(x, a) (((x) + (a) - 1) / (a) * (a))

typedef struct { char path[300]; uint32_t off, len, req; thumb_cb cb; void *u; uint8_t *mem; uint16_t size; } job_t;   /* size: 0 = the 240 px cover (pack, cache); else a larger picture made from the source, never cached (thumb_svc_request_big) */   /* mem: a JPEG in memory (PSRAM, the job owns it) instead of path */
typedef struct { uint32_t req; uint16_t *px; thumb_cb cb; void *u; } done_t;
/* job_t.req values above every request number: the task's own messages (the queue item keeps its size) */
enum { R_BENCH = UINT32_MAX, R_WAKE = UINT32_MAX - 1, R_BUILD = UINT32_MAX - 2, R_STAT = UINT32_MAX - 3, R_BUILDNOW = UINT32_MAX - 4 };   /* R_BUILDNOW: the page's 立即建置 (hires T8e) */

/* The library's tracks as the thumb task walks them (hires T8c): one PSRAM block, a snapshot of g_lib taken in LVGL context
 * (thumb_svc_library_changed), owned by the task once it took it. path: offset into pool. A track's cover: its embedded picture (key =
 * pc_thumb_key, an alias in the pack to the entry of the picture's bytes), else an image file next to it (resolved on the task: one
 * readdir per folder, one stat per image; key = pc_thumb_image_key, the data entry itself). gen: the library's generation (the pack
 * is compacted at most once per generation). */
enum { C_UNRES, C_APIC, C_IMG, C_NONE, C_ERR };
typedef struct { uint64_t key; uint32_t off, len, path; uint8_t st; } cover_t;
typedef struct { int n; uint32_t gen; cover_t *c; char *pool; } covers_t;

/* the cache's state, one struct (more than 8 bytes: PSRAM with the rest of libmain's .bss); the task's, except `pending` (exchanged
 * atomically with LVGL) */
static struct {
    covers_t *pending;            /* a new snapshot the task has not taken yet */
    covers_t *list;               /* the task's snapshot */
    pc_tpk_t pak;                 /* the pack's table (PSRAM, mem) */
    pc_tpk_ent_t *mem;
    int mem_cap;
    bool pak_failed;              /* the pack could not be opened (not a missing file, not a foreign one): logged once, nothing written this boot */
    int64_t pak_failed_at;        /* when (us): a read tries again after 60 s (re-review m4); writes stay off until `thumbcache build` */
    bool space_known, space_stale;   /* the card's free space, read only while idle (fix round 2, re-review I-4) and lowered by what we write */
    uint64_t space_total, space_free;
    const char *held;             /* the last reason a write was held (logged when it changes) */
    int64_t wait_since;           /* the playing track's request waits for its source to open (at most 2 s) */
    int pos;                      /* the builder's place in list */
    uint64_t bad_key;             /* the last cover that failed in this pass without a mark (a write was not possible) */
    bool run;                     /* a builder pass is on (a new snapshot, or `thumbcache build`) */
    bool resolve_err;             /* this pass could not list a folder or stat an image: no compaction from it (it would drop live entries) */
    const char *stopped;          /* why writes stopped (full, write, table_full, space, cap): logged once; cleared by a snapshot or a build */
    const char *why;              /* why the background waits now (pc_tbg_why_name: hires, opening, underrun, low_internal, library_scan, no_card, manual,
                                   * user, stop_needed; the compaction's gate also playing), NULL when it does not */
    /* hires T8e: the background pass also while a source of <= 48 kHz is loaded (pc_tbg_rule); the setting and the page's buttons (written by the
     * LVGL task, read here: single words), the underrun tracker, whether a source was loaded at the last gate, and the published progress */
    volatile bool manual, requested, user_paused;
    pc_tbg_under_t under;
    volatile uint32_t pseq;       /* fix 1 (review M3): the published progress's sequence (odd while it is written) */
    volatile bool want;           /* fix 1 (review I2): the page or 現況 asked for the numbers (the table may be read for them) */
    bool loaded;
    volatile uint8_t pstate, pwhy;
    volatile uint32_t pdone, ptotal, punchecked;
    volatile bool ptidy;
    bool waiting;                 /* `wait` was answered with the placeholder while hi-res plays: served when the pause ends */
    job_t wait;
    int64_t paused_since;         /* when the audio was first seen paused (us), 0 = not paused */
    /* compaction: entries of pak whose key is in live (sorted) are copied into PAK_NEW, one per step */
    bool compact, rerun;
    pc_tpk_t neu;
    pc_tpk_ent_t *neu_mem;
    uint64_t *live;
    int nlive, ci, kept;
    const char *compact_skip;     /* the last reason a compaction did not start (logged when it changes) */
    uint32_t built, failed, hits, shared;
    int64_t t_run;
    uint32_t int_free0, int_big0;   /* the internal heap (free, largest block) when the pass started */
    /* the image files of one folder (resolution) */
    char dir[300];
    char *names_pool;
    const char **names;
    int nnames;
    uint64_t dir_key; uint32_t dir_len; char dir_img[300];   /* the last image file keyed (a folder's shared cover: one stat) */
} s_tc;
static QueueHandle_t s_q;
static SemaphoreHandle_t s_jpeg_mx;     /* one JPEG decoder at a time (~45 KB internal RAM each): covers and the album */
bool thumb_jpeg_lock(int timeout_ms) { return s_jpeg_mx && xSemaphoreTake(s_jpeg_mx, timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)) == pdTRUE; }
void thumb_jpeg_unlock(void) { xSemaphoreGive(s_jpeg_mx); }
static volatile uint32_t s_req;
static volatile bool s_busy;
static struct { volatile uint32_t req, n, free; } s_low;   /* the last request skipped for low internal memory, how many, the free bytes then (> 8 B: PSRAM .bss) */
bool thumb_svc_low_mem(uint32_t req) { return req && req == s_low.req; }

/* why the last picture could not be made (the log line's reason): a route name (pc_jpeg_route_name), "format", "memory", "low_internal", "png" */
static const char *s_why;
EXT_RAM_BSS_ATTR static int s_sz_v;        /* 0 = S; the picture size the decoders below make: S, or the larger size of a thumb_svc_request_big job (set and put back by the task only; PSRAM bss: no new internal static data) */
#define s_sz (s_sz_v ? s_sz_v : S)
static bool low_internal(void);
static bool low_gate(uint32_t req);
EXT_RAM_BSS_ATTR static bool s_esp_skipped;   /* decode(): this picture's esp_new_jpeg route was skipped for the internal-RAM peak (the log line says so) */
EXT_RAM_BSS_ATTR static pc_yield_t s_y;       /* night3 B: the own decoder's yield state (the JPEG lock holder only) */
static void y_poll(void)
{
    if (pc_yield_poll(&s_y, esp_timer_get_time())) {
        vTaskDelay(pdMS_TO_TICKS(PC_YIELD_SLEEP_MS));   /* IDLE0 and the UI get the core; the longest un-yielded stretch is what the log line shows */
        pc_yield_resumed(&s_y, esp_timer_get_time());
    }
}

/* esp_new_jpeg, scaled down by the decoder so the short side stays >= S (at most 8x): the fallback for a NATIVE picture the JPEG engine
 * cannot take (too big for its full-size output, or refused). ~45 KB internal RAM while it runs (the low-internal gate). *nomem: an
 * allocation or a decoder that cannot open (internal RAM), not the picture's fault */
static uint16_t *make_esp(const uint8_t *jpg, uint32_t len, int w, int h, bool *nomem)
{
    int shortside = w < h ? w : h, sshort = s_sz;
    if (sshort * 8 < shortside) sshort = ALIGN_UP((shortside + 7) / 8, 8);
    int sw = ALIGN_UP((int)((int64_t)w * sshort / shortside), 8), sh = ALIGN_UP((int)((int64_t)h * sshort / shortside), 8);
    if (sw > w || sh > h) { sw = w & ~7; sh = h & ~7; }
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    if (sw != w || sh != h) { cfg.scale.width = (uint16_t)sw; cfg.scale.height = (uint16_t)sh; }
    jpeg_dec_handle_t dec = NULL;
    *nomem = true;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) return NULL;
    jpeg_dec_io_t io = { .inbuf = (uint8_t *)jpg, .inbuf_len = (int)len };
    jpeg_dec_header_info_t hi;
    uint8_t *full = NULL;
    uint16_t *out = NULL;
    int olen = 0;
    int ph = jpeg_dec_parse_header(dec, &io, &hi);
    if (ph != JPEG_ERR_OK && ph != JPEG_ERR_NO_MEM) *nomem = false;
    if (ph == JPEG_ERR_OK && jpeg_dec_get_outbuf_len(dec, &olen) == JPEG_ERR_OK &&
        (full = heap_caps_aligned_calloc(16, 1, (size_t)olen, MALLOC_CAP_SPIRAM))) {
        io.outbuf = full;
        int prc = jpeg_dec_process(dec, &io);
        if (prc != JPEG_ERR_OK && prc != JPEG_ERR_NO_MEM) *nomem = false;
        if (prc == JPEG_ERR_OK && (out = heap_caps_malloc((size_t)s_sz * s_sz * 2, MALLOC_CAP_SPIRAM)))
            pc_thumb_crop_scale((const uint16_t *)full, sw, sh, out, s_sz);
    }
    if (full) heap_caps_free(full);
    jpeg_dec_close(dec);
    return out;
}

/* a w x h picture (PSRAM) into the 240 x 240 cover; the picture is freed */
static uint16_t *to_cover(uint16_t *full, int w, int h)
{
    uint16_t *out = full ? heap_caps_malloc((size_t)s_sz * s_sz * 2, MALLOC_CAP_SPIRAM) : NULL;
    if (out) pc_thumb_crop_scale(full, w, h, out, s_sz);
    heap_caps_free(full);
    return out;
}

/* A JPEG into the cover by its route (jpeg_route.h): NATIVE -> the JPEG engine, else esp_new_jpeg (behind the low-internal gate), else our
 * decoder; OWN -> our decoder. fmt (may be NULL): set when no decoder can make the picture (progressive, unsupported, broken: s_why says
 * which); a failed allocation or a closed gate is not a format error (tried again later, fix round 2 re-review I-3). which (may be NULL):
 * the decoder that made it ("hw" "esp" "own") */
static uint16_t *make(const uint8_t *jpg, uint32_t len, bool *fmt, uint32_t req, const char **which)
{
    pc_jpeg_info_t in;
    int route = pc_jpeg_probe(jpg, len, &in);
    s_why = pc_jpeg_route_name(route);
    if (which) *which = "-";
    if (route != PC_JPEG_NATIVE && route != PC_JPEG_OWN) { if (fmt) *fmt = true; return NULL; }
    int w, h;
    bool nomem = false;
    if (route == PC_JPEG_NATIVE) {
        const char *hw_why;
        uint16_t *full = jpeg_route_hw(jpg, len, &in, &w, &h, &hw_why);
        if (full) { if (which) *which = "hw"; uint16_t *o = to_cover(full, w, h); if (!o) s_why = "memory"; return o; }
        if (!strcmp(hw_why, "no_psram")) { s_why = "memory"; return NULL; }
        if (pc_jpeg_min_bytes(&in) > PC_JPEG_BUDGET_BYTES) { s_why = "too_big"; if (fmt) *fmt = true; return NULL; }   /* never fits even at 1/8: marked, not retried */
        if (s_sz_v && !pc_lowmem_big_esp_ok(heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL))) {
            /* night3 A: the sharp (> 240 px) decode through esp_new_jpeg peaked ~209 KB of internal RAM on the board (free 244 KB -> min 35 KB): our PSRAM-only decoder makes it instead (below) */
            s_esp_skipped = true;
        } else {
            if (low_gate(req)) { s_why = "low_internal"; return NULL; }
            uint16_t *o = make_esp(jpg, len, in.w, in.h, &nomem);
            if (o) { if (which) *which = "esp"; return o; }
            if (nomem) { s_why = "memory"; return NULL; }
        }
    }
    int rc;
    uint16_t *full = jpeg_route_own(jpg, len, s_sz, &w, &h, &rc);   /* OWN, or a NATIVE picture both chip decoders refused */
    if (full) { if (which) *which = "own"; uint16_t *o = to_cover(full, w, h); if (!o) s_why = "memory"; return o; }
    s_why = rc == PC_JPEG_E_NOMEM ? "memory" : rc == PC_JPEG_E_TOO_BIG ? "too_big" : "format";   /* too_big: only our decoder's cap */
    if (rc != PC_JPEG_E_NOMEM && fmt) *fmt = true;
    return NULL;
}

static void deliver(void *arg)
{
    done_t *d = arg;
    if (d->req == s_req) d->cb(d->req, d->px, d->u);
    else if (d->px) heap_caps_free(d->px);
    free(d);
}

/* esp_new_jpeg takes ~45 KB of internal RAM: with Bluetooth up that can run the heap dry - then the default cover instead (2026-11-09).
 * hires 8d: only in front of esp_new_jpeg and libpng now; the JPEG engine and our decoder use PSRAM */
static bool low_internal(void)
{
    return lowmem_level() >= PC_LOWMEM_NO_COVER || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < PC_LOWMEM_COVER_BLOCK
        || heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < PC_LOWMEM_COVER_FREE;
}
/* the gate before a decoder that needs internal RAM (esp_new_jpeg, libpng): true = skipped now; req (0: the background pass):
 * thumb_svc_low_mem(req) says why it got NULL */
static bool low_gate(uint32_t req)
{
    if (!low_internal()) return false;
    s_low.free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_low.n++;
    printf("APP thumb skip reason=low_internal free=%u n=%u\n", (unsigned)s_low.free, (unsigned)s_low.n);
    if (req) s_low.req = req;
    return true;
}

static bool paused_now(uint32_t *src)
{
    aud_info_t a;
    audio_svc_info(&a);
    if (src) *src = a.src_rate;
    return pc_thumb_hires_pause(a.src_rate);
}
bool thumb_svc_paused(void) { return paused_now(NULL); }

_Static_assert((int)AUD_IDLE == (int)PC_THUMB_AUD_IDLE && (int)AUD_PLAYING == (int)PC_THUMB_AUD_PLAYING && (int)AUD_ENDED == (int)PC_THUMB_AUD_ENDED
               && (int)AUD_ERROR == (int)PC_THUMB_AUD_ERROR && (int)AUD_FADED == (int)PC_THUMB_AUD_FADED && (int)AUD_PAUSED == (int)PC_THUMB_AUD_PAUSED
               && (int)AUD_NEAR == (int)PC_THUMB_AUD_NEAR && (int)AUD_NEXT == (int)PC_THUMB_AUD_NEXT,
               "pc_thumb's audio states follow aud_state_t");

/* a play / mixer / queued source was handed to the audio service and it has not published it yet: its rate is not known (fix round 2,
 * re-review I-1 B: player_ctl_changed comes right after audio_svc_play_ex is queued, before the audio task opened the new source). From the
 * live generation (fix round 3, re-review 2 I-A): a stop or a fade clears it, so a stopped player is never "opening" */
static bool opening_now(void)
{
    aud_status_t st;
    audio_svc_status(&st);
    return pc_thumb_opening(audio_svc_live_gen(), st.gen);
}

/* review I4 ruling: background work only while nothing plays, or after 10 s of pause (pc_thumb_bg_allowed); between two tracks (ENDED),
 * after a fade or an error: like a pause (re-review m1) */
static bool idle_now(void)
{
    aud_status_t st;
    audio_svc_status(&st);
    bool playing = st.state == AUD_PLAYING || st.state == AUD_NEAR || st.state == AUD_NEXT, paused = pc_thumb_paused_like(st.state);
    int64_t now = esp_timer_get_time();
    if (!paused) s_tc.paused_since = 0;
    else if (!s_tc.paused_since) s_tc.paused_since = now;
    return pc_thumb_bg_allowed(playing, paused, paused ? (uint32_t)((now - s_tc.paused_since) / 1000) : 0);
}

/* fix round 1 (review I1): underruns charged to the pass - the wired I2S underruns of the source plus the Bluetooth sink's counted dry blocks
 * (bt_svc_sink_underflows: only in a steady, playing stream), baselined right before each pass step and checked after it and at the next wake */
static void under_sample(uint32_t *gen, uint32_t *count, int32_t *pos)
{
    aud_info_t a;
    aud_status_t st;
    audio_svc_info(&a);
    audio_svc_status(&st);
    bool playing = st.state == AUD_PLAYING || st.state == AUD_NEAR || st.state == AUD_NEXT;
    *gen = playing ? st.gen : 0;
    *count = a.underruns_src + bt_svc_sink_underflows();
    *pos = st.pos_ms;
}
static void under_step(bool open) { uint32_t g, c; int32_t p; under_sample(&g, &c, &p); pc_tbg_under_step(&s_tc.under, open ? PC_TBG_RUN : PC_TBG_N, g, c); }   /* fix 2 (N1) */
static void under_check(void)
{
    if (!s_tc.under.armed) return;
    uint32_t g, c;
    int32_t p;
    under_sample(&g, &c, &p);
    if (pc_tbg_under_check(&s_tc.under, g, c, p, esp_timer_get_time() / 1000))
        printf("APP thumbcache underrun gen=%lu count=%lu sink=%lu holds=%lu\n", (unsigned long)g, (unsigned long)c, (unsigned long)bt_svc_sink_underflows(), (unsigned long)s_tc.under.holds);
}

/* hires T8e: the background pass runs while stopped AND while a source of <= 48 kHz is loaded (pc_tbg_rule; "loaded" =
 * not idle_now: playing, or paused / between tracks for less than 10 s). An underrun charged to the pass (under_check, fix round 1) holds it
 * PC_TBG_QUIET_MS, after PC_TBG_GIVEUP holds it runs only while idle. The free space is still read only while idle. decode_next:
 * the low-internal gate too (only in front of a decode). Sets s_tc.loaded (the breather, the pieces of a read). */
static int bg_rule(bool decode_next)
{
    aud_info_t a;
    aud_status_t st;
    audio_svc_info(&a);
    audio_svc_status(&st);
    bool loaded = !idle_now();
    int64_t now = esp_timer_get_time() / 1000;
    s_tc.loaded = loaded;
    pc_tbg_in_t in = { .card = g_sd_ok, .scanning = library_scanning(), .manual = s_tc.manual, .requested = s_tc.requested, .user_paused = s_tc.user_paused,
                       .loaded = loaded, .src_rate = a.src_rate, .opening = opening_now(), .underrun_age_ms = pc_tbg_under_age(&s_tc.under, now),
                       .underrun_holds = s_tc.under.holds, .space_known = s_tc.space_known, .low_mem = decode_next && low_internal() };
    return pc_tbg_rule(&in);
}

/* PNG (libpng's simplified API, linked already for FreeType; its allocations prefer PSRAM): RGB888 in PSRAM, then the centre square */
/* libpng reports an allocation failure in its message ("...memory..."): that one is not a format error */
static bool png_fmt_error(const png_image *im) { return !strstr(im->message, "memory"); }
static uint16_t *make_png(const uint8_t *buf, uint32_t len, bool *fmt)
{
    png_image im;
    memset(&im, 0, sizeof im);
    im.version = PNG_IMAGE_VERSION;
    uint16_t *out = NULL;
    if (!png_image_begin_read_from_memory(&im, buf, len)) { if (fmt) *fmt = png_fmt_error(&im); png_image_free(&im); return NULL; }
    im.format = PNG_FORMAT_RGB;
    size_t sz = PNG_IMAGE_SIZE(im);
    bool big = sz > PNG_RGB_MAX;
    uint8_t *rgb = big ? NULL : heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (rgb && !png_image_finish_read(&im, NULL, rgb, 0, NULL)) { if (fmt) *fmt = png_fmt_error(&im); }
    else if (rgb && (out = heap_caps_malloc((size_t)s_sz * s_sz * 2, MALLOC_CAP_SPIRAM)))
        pc_thumb_crop_scale_rgb888(rgb, (int)im.width, (int)im.height, out, s_sz);
    if (big) { if (fmt) *fmt = true; printf("APP thumb skip reason=png_too_big w=%u h=%u\n", (unsigned)im.width, (unsigned)im.height); }
    png_image_free(&im);
    heap_caps_free(rgb);
    return out;
}

/* the bytes of an embedded picture (off > 0) or of an image file (off 0) into PSRAM; NULL when they cannot be read. hires T8e: in pieces of
 * READ_PIECE, and while a source is loaded a tick between two pieces, so a picture of several MB never holds the card from the audio's reads */
#define READ_PIECE (64u << 10)
static uint8_t *read_bytes(const char *path, uint32_t off, uint32_t len)
{
    if (!len || len > IMG_MAX) return NULL;
    uint8_t *buf = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    FILE *m = buf ? fopen(path, "rb") : NULL;
    bool ok = m && fseek(m, (long)off, SEEK_SET) == 0;
    for (uint32_t at = 0; ok && at < len; at += READ_PIECE) {
        uint32_t k = len - at < READ_PIECE ? len - at : READ_PIECE;
        if (at && s_tc.loaded) vTaskDelay(1);
        ok = fread(buf + at, 1, k, m) == k;
    }
    if (m) fclose(m);
    if (!ok) { heap_caps_free(buf); buf = NULL; }
    return buf;
}

/* Decoded on demand since 2026-11-09 (68 covers: 206 ms median, 411 ms p90, 1.3 s worst); since hires T8c the result also goes into the
 * pack (Batch B M-B4: a 96k source underruns while covers are decoded from the card it streams from). png: decode as PNG, else JPEG. */
static uint16_t *decode(const uint8_t *buf, uint32_t len, bool png, const char *path, bool *fmt, uint32_t req)
{
    *fmt = false;
    if (png && low_gate(req)) return NULL;
    int64_t t0 = esp_timer_get_time();
    uint16_t *px = NULL;
    const char *which = "png";
    s_why = "png";
    s_esp_skipped = false;
    const size_t fr0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), mn0 = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    if (thumb_jpeg_lock(-1)) {
        pc_yield_begin(&s_y, esp_timer_get_time(), PC_YIELD_SLICE_US);
        jpeg_route_set_poll(y_poll);                               /* only our own decoder calls it; the chip's decoders run as before */
        px = png ? make_png(buf, len, fmt) : make(buf, len, fmt, req, &which);
        jpeg_route_set_poll(NULL);
        pc_yield_end(&s_y, esp_timer_get_time());
        thumb_jpeg_unlock();
    }
    const int64_t wall_ms = (esp_timer_get_time() - t0) / 1000;
    if (!px) { if (strcmp(s_why, "low_internal")) printf("APP thumb fail reason=%s path=%s\n", s_why, path); return NULL; }
    /* longest_ms: the longest stretch without a yield (a decoder that does not yield: the whole wall time); yields: how many times it slept; esp_skipped: the esp_new_jpeg route was left for its internal-RAM peak */
    const size_t mn1 = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    printf("APP thumb built ms=%lld dec=%s longest_ms=%lld yields=%lu size=%d int_peak=%u%s\n", wall_ms, which, (long long)(s_y.longest / 1000), (unsigned long)s_y.yields, s_sz,
           mn1 < mn0 ? (unsigned)(fr0 - mn1) : 0u,   /* internal RAM this decode took at its deepest, when it set a new all-time low (else 0 = not more than the earlier low); only esp / libpng use any */
           s_esp_skipped ? " esp_skipped=1" : "");
    return px;
}


/* ---- the cover of a track: its embedded picture, else an image file next to it (pc_thumb_pick_image) ---- */
/* false: the folder could not be listed now (not "no folder"): the caller does not decide anything from it */
static bool list_dir(const char *dir)
{
    if (!strcmp(dir, s_tc.dir) || !s_tc.names) return s_tc.names != NULL;
    s_tc.nnames = 0;
    s_tc.dir[0] = 0;
    size_t used = 0;
    errno = 0;
    DIR *d = opendir(dir);
    if (!d && errno != ENOENT && errno != ENOTDIR) return false;
    struct dirent *e;
    while (d && (e = readdir(d)) && s_tc.nnames < DIR_NAMES) {
        if (e->d_type == DT_DIR || !pc_thumb_is_image(e->d_name, true)) continue;
        size_t l = strlen(e->d_name) + 1;
        if (used + l > DIR_POOL) break;
        memcpy(s_tc.names_pool + used, e->d_name, l);
        s_tc.names[s_tc.nnames++] = s_tc.names_pool + used;
        used += l;
    }
    if (d) closedir(d);
    snprintf(s_tc.dir, sizeof s_tc.dir, "%s", dir);
    return true;
}

/* the cover's kind (C_APIC / C_IMG / C_NONE, C_ERR = could not look now) and key; for C_IMG the image file in img and its size in *len */
static int resolve(const char *path, uint32_t off, uint32_t *len, char *img, size_t cap, uint64_t *key)
{
    if (*len && *len <= APIC_MAX) { *key = pc_thumb_key(path, off, *len); return C_APIC; }
    const char *slash = strrchr(path, '/');
    if (!slash) return C_NONE;
    char dir[300];
    snprintf(dir, sizeof dir, "%.*s", (int)(slash - path), path);
    if (!list_dir(dir)) return C_ERR;
    bool shared;
    int k = pc_thumb_pick_image(slash + 1, s_tc.names, s_tc.nnames, true, &shared);
    if (k < 0) return C_NONE;
    snprintf(img, cap, "%s/%s", dir, s_tc.names[k]);
    struct stat st;
    if (!strcmp(img, s_tc.dir_img) && s_tc.dir_key) { *key = s_tc.dir_key; }
    else {
        errno = 0;
        if (stat(img, &st)) return errno == ENOENT ? C_NONE : C_ERR;
        if (st.st_size <= 0 || (uint32_t)st.st_size > IMG_MAX) return C_NONE;
        *key = pc_thumb_image_key(img, (uint32_t)st.st_size, (int64_t)st.st_mtime);
        snprintf(s_tc.dir_img, sizeof s_tc.dir_img, "%s", img);
        s_tc.dir_key = *key;
        s_tc.dir_len = (uint32_t)st.st_size;
    }
    *len = s_tc.dir_len;
    return C_IMG;
}
static bool is_png(const char *img) { const char *dot = strrchr(img, '.'); return dot && !strcasecmp(dot, ".png"); }

/* ---- the pack ---- */
static void tpk_yield(void) { vTaskDelay(1); }

static pc_tpk_ent_t *tpk_alloc(int cap, void *u)
{
    (void)u;
    if (cap <= s_tc.mem_cap) return s_tc.mem;
    heap_caps_free(s_tc.mem);
    s_tc.mem = heap_caps_malloc(sizeof(pc_tpk_ent_t) * (size_t)cap, MALLOC_CAP_SPIRAM);
    s_tc.mem_cap = s_tc.mem ? cap : 0;
    return s_tc.mem;
}

static void pak_fail(int err)
{
    if (!s_tc.pak_failed) printf("APP thumbcache open failed err=%d\n", err);
    s_tc.pak_failed = true;
    s_tc.pak_failed_at = esp_timer_get_time();
}

/* The pack's table into PSRAM. create (a write is about to happen): first the boot-time order of an interrupted compaction
 * (pc_tpk_recover: a failed rename keeps thumbs.new and fails here, never an empty pack in its place - re-review I-2), then
 * pc_tpk_attach, which makes a new pack only when there is no file or it is not a pack after a full read (review I1). Any other failure
 * is logged once and nothing is written this boot. A read (create false) never makes, renames or deletes anything; after a failure it
 * tries again after 60 s (re-review m4). */
static bool pak_ready(int want_cap, bool create)
{
    if (s_tc.pak.open) return true;
    if (!g_sd_ok) return false;
    if (s_tc.pak_failed && (create || esp_timer_get_time() - s_tc.pak_failed_at < 60000000)) return false;
    int r = create ? pc_tpk_recover(PAK, PAK_NEW) : 0;
    if (r) { pak_fail(-r); return false; }
    struct stat st;
    if (!create && stat(PAK, &st)) return false;               /* nothing to read yet */
    r = pc_tpk_attach(&s_tc.pak, PAK, tpk_alloc, NULL, pc_tpk_cap_for(want_cap), S, create);
    if (r < 0) {
        if (!create && (r == -ENOENT || r == -EBADMSG)) return false;   /* the builder makes (or remakes) it */
        if (!create) { s_tc.pak_failed_at = esp_timer_get_time(); if (!s_tc.pak_failed) printf("APP thumbcache open failed err=%d (read)\n", -r); s_tc.pak_failed = true; return false; }
        pak_fail(-r);
        return false;
    }
    if (r == 1) printf("APP thumbcache new pack cap=%d\n", s_tc.pak.cap);
    s_tc.pak.yield = tpk_yield;
    printf("APP thumbcache pack entries=%d cap=%d bytes=%lu gen=%08lx\n", s_tc.pak.n, s_tc.pak.cap, (unsigned long)s_tc.pak.end, (unsigned long)s_tc.pak.gen);
    return true;
}

static void stop_writes(const char *why, int err)
{
    if (s_tc.stopped) return;
    s_tc.stopped = why;
    printf("APP thumbcache stop reason=%s errno=%d entries=%d cap=%d bytes=%lu\n", why, err, s_tc.pak.n, s_tc.pak.cap, (unsigned long)s_tc.pak.end);
}

/* the card's free space, read only while idle (the first read after mount can scan the allocation bitmap or the FAT: never during
 * playback - re-review I-4); between reads it is lowered by what the pack writes */
static void space_refresh(void)
{
    uint64_t t = 0, f = 0;
    int64_t t0 = esp_timer_get_time();
    s_tc.space_known = esp_vfs_fat_info(CARD, &t, &f) == ESP_OK;
    s_tc.space_stale = false;
    s_tc.space_total = t;
    s_tc.space_free = f;
    printf("APP thumbcache space known=%d free_mb=%llu margin_mb=%llu ms=%lld\n", s_tc.space_known, (unsigned long long)(f >> 20),
           (unsigned long long)(pc_thumb_space_margin(t) >> 20), (esp_timer_get_time() - t0) / 1000);
}
static bool space_for(uint64_t need) { return s_tc.space_known && pc_thumb_space_ok(s_tc.space_total, s_tc.space_free, need); }
static void space_used(uint64_t n) { s_tc.space_free = s_tc.space_free > n ? s_tc.space_free - n : 0; }

static void held(const char *why)
{
    if (why != s_tc.held) printf("APP thumbcache write held reason=%s\n", why);
    s_tc.held = why;
}

/* Every write to the pack goes through here (fix round 2): the gate is asked at the moment of the write (re-review I-1: a source above
 * 48 kHz loaded or still opening, background work while something plays, the free space not read yet); the bytes - with the table when
 * this write makes the pack (re-review I-4) - must keep the margin free and stay under the cap. Held = nothing written, the caller drops
 * its pixels and tries again later; space / cap / a failed write stop writes (logged once). */
enum { W_DATA, W_ALIAS, W_SHARED, W_FORGET };
enum { WR_OK, WR_HELD, WR_STOPPED };
static int pak_write(bool background, int op, uint64_t key, uint64_t content, const uint16_t *px)
{
    if (s_tc.stopped || s_tc.compact || s_tc.pak_failed || !g_sd_ok) return WR_STOPPED;
    const char *g = pc_thumb_write_gate(paused_now(NULL), opening_now(), background, !background || idle_now() || bg_rule(false) == PC_TBG_RUN,   /* T8e: or the pass's rule */
                                        op == W_FORGET || s_tc.space_known);
    if (g) { held(g); return WR_HELD; }
    s_tc.held = NULL;
    uint64_t need = op == W_DATA ? PX_BYTES + PC_TPK_SLOT : op == W_SHARED ? PX_BYTES + 2 * PC_TPK_SLOT : op == W_ALIAS ? PC_TPK_SLOT : 0;
    int want = 2 * (s_tc.list ? s_tc.list->n : 0);              /* a slot per track (alias) + one per picture */
    if (!s_tc.pak.open && op != W_FORGET) need += PC_TPK_HDR + (uint64_t)pc_tpk_cap_for(want) * PC_TPK_SLOT;   /* this write may make the pack (counted either way) */
    if (op != W_FORGET && !space_for(need)) {
        if (!s_tc.stopped) printf("APP thumbcache stop reason=space free=%llu\n", (unsigned long long)(s_tc.space_free >> 20));
        s_tc.stopped = "space";
        return WR_STOPPED;
    }
    if (!pak_ready(want, true)) return WR_STOPPED;           /* a table made here is in need (subtracted once, below) */
    if (op != W_FORGET && !pc_tpk_span_ok(s_tc.pak.end, (uint32_t)need, PC_TPK_BYTES_MAX)) {
        if (!s_tc.stopped) printf("APP thumbcache stop reason=cap bytes=%lu\n", (unsigned long)s_tc.pak.end);
        s_tc.stopped = "cap";
        return WR_STOPPED;
    }
    errno = 0;
    int r = op == W_DATA ? pc_tpk_append(&s_tc.pak, key, S, S, px) : op == W_SHARED ? pc_tpk_add_shared(&s_tc.pak, key, content, S, S, px)
          : op == W_ALIAS ? pc_tpk_alias(&s_tc.pak, key, content) : pc_tpk_forget(&s_tc.pak, key);
    if (r == 0 || r == -EEXIST || (op == W_FORGET && r == -ENOENT)) { space_used(need); return WR_OK; }
    if (r == -ENOSPC) stop_writes("table_full", 0);           /* compacted into a bigger table at the end of the pass */
    else if (r == -EFBIG) stop_writes("cap", 0);
    else stop_writes(errno == ENOSPC ? "full" : "write", errno);
    return WR_STOPPED;
}

static uint16_t *pak_get(uint64_t key)
{
    const pc_tpk_ent_t *e = pak_ready(0, false) ? pc_tpk_lookup(&s_tc.pak, key) : NULL;
    if (!e || e->w != S || e->h != S) return NULL;
    uint16_t *px = heap_caps_malloc(PX_BYTES, MALLOC_CAP_SPIRAM);
    if (px && pc_tpk_read(&s_tc.pak, e, px)) { heap_caps_free(px); px = NULL; }
    return px;
}
static bool pak_has(uint64_t key) { return pc_tpk_lookup(&s_tc.pak, key) || pc_tpk_is_bad(&s_tc.pak, key); }

/* make one cover and add it: an embedded picture's bytes are hashed first, and a picture already in the pack (another track of the album)
 * only gets an alias - no decode, 32 bytes written. *out (when out) gets the pixels (caller frees), written or not. MC_HELD: made but
 * not written (the write gate closed meanwhile: tried again later); MC_FAIL: no cover now (read, allocation: tried again later); MC_BAD:
 * the bytes are not a picture the decoders can make (marked, not tried again until `thumbcache build`). */
enum { MC_OK, MC_HELD, MC_FAIL, MC_BAD };
static int make_cover(bool bg, int kind, const char *path, uint32_t off, uint32_t len, const char *img, uint64_t key, uint16_t **out, uint32_t req)
{
    if (out) *out = NULL;
    uint8_t *buf = kind == C_IMG ? read_bytes(img, 0, len) : read_bytes(path, off, len);
    if (!buf) { printf("APP thumb fail path=%s (read)\n", kind == C_IMG ? img : path); return MC_FAIL; }
    uint16_t *px = NULL;
    bool fmt = false;
    int w;
    if (kind == C_APIC) {
        uint64_t content = pc_thumb_content_key(buf, len);
        const pc_tpk_ent_t *c = s_tc.pak.open ? pc_tpk_find(&s_tc.pak, content) : NULL;
        if (c && !pc_tpk_is_alias(c)) {                        /* the album's picture is there: share it */
            heap_caps_free(buf);
            w = pak_write(bg, W_ALIAS, key, content, NULL);
            if (w == WR_OK) s_tc.shared++;
            if (out) *out = pak_get(content);
            return w == WR_HELD ? MC_HELD : MC_OK;
        }
        px = decode(buf, len, len >= 8 && !memcmp(buf, "\x89PNG\r\n\x1a\n", 8), path, &fmt, req);   /* re-review 2 m8: an embedded PNG */
        heap_caps_free(buf);
        w = px ? pak_write(bg, W_SHARED, key, content, px) : WR_OK;
    } else {
        px = decode(buf, len, is_png(img), img, &fmt, req);
        heap_caps_free(buf);
        w = px ? pak_write(bg, W_DATA, key, 0, px) : WR_OK;
    }
    if (!px) {
        if (!fmt) return MC_FAIL;
        if (pak_write(bg, W_ALIAS, key, 0, NULL) != WR_OK) return MC_FAIL;   /* the mark itself must wait: so must the cover */
        return MC_BAD;
    }
    if (out) *out = px;
    else heap_caps_free(px);
    return w == WR_HELD ? MC_HELD : MC_OK;
}

/* ---- the builder ---- */
static void set_why(const char *why)
{
    if (why && why != s_tc.why) printf("APP thumbcache wait reason=%s\n", why);
    s_tc.why = why;
}
/* the compaction's gate (background work only while idle, review I4 - kept for the compaction by hires T8e: it copies the whole pack): no card,
 * hi-res, a source opening, playing, a scan, low internal RAM (decode only); the free space is read here (idle) when it is not known or stale */
static const char *gate(bool decode_next)
{
    const char *why = !g_sd_ok ? "no_card" : paused_now(NULL) ? "hires" : opening_now() ? "opening" : !idle_now() ? "playing"
                    : library_scanning() ? "library_scan" : decode_next && low_internal() ? "low_internal" : NULL;
    if (why && !strcmp(why, "playing")) s_tc.space_stale = true;   /* re-review 2 m6: other writers during playback: read again at the next idle step */
    if (!why && (!s_tc.space_known || s_tc.space_stale)) space_refresh();
    return why;
}
/* hires T8e: the pass's gate (pc_tbg_rule via bg_rule): the free space is read only while idle; while a source is loaded it is marked stale (read
 * again at the next idle step: other writers use the card too) and a pass that never read it waits for a stop */
static const char *pass_gate(bool decode_next)
{
    int r = bg_rule(decode_next);
    if (s_tc.loaded) s_tc.space_stale = true;
    else if (r == PC_TBG_RUN && (!s_tc.space_known || s_tc.space_stale)) space_refresh();
    return r == PC_TBG_RUN ? NULL : pc_tbg_why_name(r);
}

static void pass_start(void)
{
    s_tc.pos = 0; s_tc.bad_key = 0; s_tc.run = true; s_tc.resolve_err = false; s_tc.stopped = NULL; s_tc.why = NULL;
    s_tc.built = s_tc.failed = s_tc.shared = 0;
    s_tc.space_stale = true;                                  /* read again at the first idle step (other writers use the card too) */
    s_tc.t_run = esp_timer_get_time();
    s_tc.int_free0 = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);   /* hires 8d: the internal heap before and after a pass */
    s_tc.int_big0 = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
}

static int by_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }
static bool is_live(uint64_t k) { return bsearch(&k, s_tc.live, (size_t)s_tc.nlive, sizeof *s_tc.live, by_u64) != NULL; }

/* the end of a compaction: ok = every live entry is in PAK_NEW. The old pack is unlinked, then PAK_NEW renamed; a failure of either keeps the
 * complete file (pak_ready's pc_tpk_recover takes PAK_NEW when PAK is gone, drops it while PAK is valid) - nothing is made empty */
static void compact_end(bool ok)
{
    pc_tpk_close(&s_tc.neu);
    heap_caps_free(s_tc.live); s_tc.live = NULL;
    heap_caps_free(s_tc.neu_mem); s_tc.neu_mem = NULL;
    s_tc.compact = false;
    if (!ok) { unlink(PAK_NEW); if (s_tc.requested && !s_tc.run) { s_tc.rerun = false; pass_start(); } return; }   /* T8e: a 立即建置 that waited for it */
    int dropped = s_tc.pak.n - s_tc.kept;
    pc_tpk_close(&s_tc.pak);
    int rc = unlink(PAK) ? errno : 0;
    if (!rc) rc = rename(PAK_NEW, PAK) ? errno : 0;
    printf("APP thumbcache compact kept=%d dropped=%d rc=%d ms=%lld\n", s_tc.kept, dropped, rc, (esp_timer_get_time() - s_tc.t_run) / 1000);
    pak_ready(0, true);                                       /* recover + attach: the complete pack, whichever file it is in */
    if (s_tc.rerun) { s_tc.rerun = false; pass_start(); }   /* the table was full (or 立即建置 came meanwhile): build what is still missing */
}

static void compact_skip(const char *why, uint64_t a, uint64_t b)
{
    if (why != s_tc.compact_skip) printf("APP thumbcache compact skipped reason=%s need=%llu free_mb=%llu\n", why, (unsigned long long)a, (unsigned long long)b);
    s_tc.compact_skip = why;
}

/* the end of a whole pass: entries no track uses any more (or a full table) make a compacted copy - at most once per library
 * generation (stored in the new pack's header), only when the copy fits the free-space margin and the cap, never from a pass that
 * could not look at every folder */
static void pass_end(void)
{
    covers_t *L = s_tc.list;
    bool req = s_tc.requested;
    s_tc.run = false;
    s_tc.requested = false;                                   /* hires T8e: 立即建置's pass is over (手動 holds the next one again) */
    set_why(NULL);
    int none = 0;
    for (int i = 0; i < L->n; i++) none += L->c[i].st == C_NONE;
    printf("APP thumbcache done tracks=%d none=%d built=%lu shared=%lu failed=%lu ms=%lld int_free=%lu>%u int_big=%lu>%u\n", L->n, none, (unsigned long)s_tc.built,
           (unsigned long)s_tc.shared, (unsigned long)s_tc.failed, (esp_timer_get_time() - s_tc.t_run) / 1000, (unsigned long)s_tc.int_free0,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned long)s_tc.int_big0, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (!s_tc.pak.open) return;
    if (s_tc.resolve_err) { compact_skip("resolve", 0, 0); return; }
    uint64_t *live = heap_caps_malloc(sizeof(uint64_t) * (size_t)(2 * L->n + 1), MALLOC_CAP_SPIRAM);
    if (!live) return;
    int n = 0;
    for (int i = 0; i < L->n; i++) {
        const cover_t *c = &L->c[i];
        if (c->st != C_APIC && c->st != C_IMG) continue;
        live[n++] = c->key;
        const pc_tpk_ent_t *e = pc_tpk_find(&s_tc.pak, c->key);
        if (pc_tpk_is_alias(e) && pc_tpk_alias_target(e)) live[n++] = pc_tpk_alias_target(e);   /* the picture an alias shows */
    }
    qsort(live, (size_t)n, sizeof *live, by_u64);
    int u = 0;
    for (int i = 0; i < n; i++) if (!u || live[u - 1] != live[i]) live[u++] = live[i];
    s_tc.live = live; s_tc.nlive = u;
    int stale = 0, kept_data = 0, kept = 0;
    for (int i = 0; i < s_tc.pak.n; i++) {
        bool l = is_live(s_tc.pak.ent[i].key);
        stale += !l;
        kept += l;
        kept_data += l && !pc_tpk_is_alias(&s_tc.pak.ent[i]);
    }
    bool full = s_tc.stopped && !strcmp(s_tc.stopped, "table_full");
    int cap = pc_tpk_cap_for(u);
    uint64_t need = (uint64_t)PC_TPK_HDR + (uint64_t)cap * PC_TPK_SLOT + (uint64_t)kept_data * PX_BYTES;
    if (stale || full) space_refresh();                       /* idle here (the pass's last step passed the gate) */
    const char *skip = !stale && !full ? "none_stale" : s_tc.pak.gen == L->gen ? "same_library"
                     : need >= PC_TPK_BYTES_MAX ? "cap" : !space_for(need) ? "space" : NULL;
    if (skip) {
        if (strcmp(skip, "none_stale")) compact_skip(skip, need, s_tc.space_free >> 20);
        heap_caps_free(s_tc.live); s_tc.live = NULL;
        return;
    }
    s_tc.compact_skip = NULL;
    const char *g = pc_thumb_write_gate(paused_now(NULL), opening_now(), true, idle_now(), s_tc.space_known);   /* re-review 2 I-B: at the write */
    if (g) {                                                  /* the end of the pass again at the next idle step (everything else is in the pack) */
        held(g);
        heap_caps_free(s_tc.live); s_tc.live = NULL;
        s_tc.run = true;
        s_tc.pos = L->n;                                      /* build_step asks the compaction's gate (idle only) for this end */
        s_tc.requested = req;
        return;
    }
    s_tc.neu_mem = heap_caps_malloc(sizeof(pc_tpk_ent_t) * (size_t)cap, MALLOC_CAP_SPIRAM);
    if (!s_tc.neu_mem || pc_tpk_create(PAK_NEW, cap, S, L->gen) || pc_tpk_open(&s_tc.neu, PAK_NEW, s_tc.neu_mem, cap)) {
        printf("APP thumbcache compact failed to start errno=%d\n", errno);
        s_tc.compact = true; compact_end(false); return;
    }
    space_used((uint64_t)PC_TPK_HDR + (uint64_t)cap * PC_TPK_SLOT);
    s_tc.neu.yield = tpk_yield;
    s_tc.ci = 0; s_tc.kept = 0;
    s_tc.compact = true; s_tc.rerun = full;
    s_tc.stopped = NULL;
    s_tc.t_run = esp_timer_get_time();
    printf("APP thumbcache compact start entries=%d stale=%d live=%d cap=%d bytes=%llu\n", s_tc.pak.n, stale, kept, cap, (unsigned long long)need);
}

/* one live entry copied per step (a data entry: two card transfers of 115 KB; an alias: one slot). The gate is asked before the read and
 * again right before the write (re-review I-1: drop the read and try again later), the margin before every write (re-review m3) */
static bool compact_step(void)
{
    const char *why = gate(false);
    set_why(why);
    if (why) return false;
    while (s_tc.ci < s_tc.pak.n && !is_live(s_tc.pak.ent[s_tc.ci].key)) s_tc.ci++;
    if (s_tc.ci >= s_tc.pak.n) { compact_end(true); return true; }
    const pc_tpk_ent_t *e = &s_tc.pak.ent[s_tc.ci];
    bool alias = pc_tpk_is_alias(e);
    uint64_t need = alias ? PC_TPK_SLOT : (uint64_t)e->size + PC_TPK_SLOT;
    if (!space_for(need)) { printf("APP thumbcache compact stopped reason=space free_mb=%llu\n", (unsigned long long)(s_tc.space_free >> 20)); compact_end(false); return true; }
    uint8_t *buf = NULL;
    int r = 0;
    if (!alias) {
        buf = heap_caps_malloc(e->size, MALLOC_CAP_SPIRAM);
        r = !buf ? -ENOMEM : pc_tpk_read(&s_tc.pak, e, buf) ? -EIO : 0;
    }
    if (r == -ENOMEM) return false;                           /* PSRAM busy: the same entry again later */
    if (r) { heap_caps_free(buf); printf("APP thumbcache compact failed reason=read\n"); compact_end(false); return true; }   /* the old pack stays as it is */
    const char *g = pc_thumb_write_gate(paused_now(NULL), opening_now(), true, idle_now(), s_tc.space_known);
    if (g) { heap_caps_free(buf); held(g); return false; }   /* the same entry again later */
    r = alias ? pc_tpk_alias(&s_tc.neu, e->key, pc_tpk_alias_target(e)) : pc_tpk_append(&s_tc.neu, e->key, e->w, e->h, buf);
    heap_caps_free(buf);
    if (r) {
        int err = errno;
        printf("APP thumbcache compact failed rc=%d errno=%d\n", r, err);
        compact_end(false);
        stop_writes(r == -EIO && err == ENOSPC ? "full" : "write", err);
        return true;
    }
    space_used(need);
    s_tc.ci++;
    s_tc.kept++;
    return !alias;
}

/* one step of a pass: tracks whose cover is in the pack (or that have none) are passed over (a few resolved per step: one readdir per
 * folder), the first missing one is made and added. Returns true when it did card work worth a breather. */
static bool build_step(void)
{
    covers_t *L = s_tc.list;
    if (!L) { s_tc.run = false; return false; }
    const char *why = s_tc.pos >= L->n && L->n ? gate(false) : pass_gate(false);   /* T8e: a held end of the pass (its compaction) waits for idle */
    set_why(why);
    under_step(!why);                                         /* fix 2 (re-review N1): the window opens only when the gate lets the step work */
    if (why) return false;
    if (!pak_ready(2 * L->n, false) && s_tc.pak_failed) { s_tc.run = false; return false; }   /* logged once by pak_ready */
    for (int budget = 16; s_tc.pos < L->n; s_tc.pos++) {
        cover_t *c = &L->c[s_tc.pos];
        char img[300] = "";
        uint32_t len = c->len;
        if (c->st == C_UNRES || c->st == C_ERR) {           /* a few per step: one readdir per folder, one stat per image */
            if (budget-- <= 0) return true;
            c->st = (uint8_t)resolve(L->pool + c->path, c->off, &len, img, sizeof img, &c->key);
            if (c->st == C_ERR) { s_tc.resolve_err = true; continue; }   /* looked at again next pass; no compaction from this one */
        }
        if (c->st == C_NONE || c->key == s_tc.bad_key || pak_has(c->key)) continue;
        if (s_tc.stopped || s_tc.pak_failed) continue;       /* no writes: the pass still resolves every track (for the compaction) */
        if ((why = pass_gate(true))) { set_why(why); return false; }
        if (c->st == C_IMG && !img[0]) {
            int k = resolve(L->pool + c->path, c->off, &len, img, sizeof img, &c->key);
            if (k != C_IMG) { if (k == C_ERR) s_tc.resolve_err = true; else c->st = (uint8_t)k; continue; }
        }
        uint32_t sh = s_tc.shared;
        int m = make_cover(true, c->st, L->pool + c->path, c->off, c->st == C_IMG ? len : c->len, img, c->key, NULL, 0);
        if (m == MC_HELD) return true;                        /* the gate closed while it was made: this track again later */
        if (m == MC_OK) s_tc.built += s_tc.shared == sh && !s_tc.stopped;
        else { s_tc.failed++; if (m == MC_FAIL) s_tc.bad_key = c->key; }   /* MC_FAIL: next pass; MC_BAD: marked */
        s_tc.pos++;
        return true;
    }
    pass_end();
    return true;
}

/* hires T8e: the pass as the 封面快取 page and 現況 show it (thumb_svc_progress): done = covers in the pack (or marked as not a picture), total =
 * tracks that have or may have a cover (not resolved yet counts); the state from the run / compaction and the last reason. A held end of the
 * pass (its compaction waits for idle) and a waiting compaction read as done: every cover is in the pack. Stopped writes are said. */
static void progress_update(void)
{
    covers_t *L = s_tc.list;
    uint32_t done = 0, total = 0, unchecked = 0;
    bool active = s_tc.run && (!s_tc.manual || s_tc.requested);   /* fix 1 (review I2): no card access for the numbers during hi-res or an open; */
    if (L && !s_tc.pak.open && pc_tbg_may_read_table(paused_now(NULL), opening_now(), s_tc.want, active))   /* 手動 at boot: only once someone looks */
        pak_ready(0, false);
    s_tc.want = false;                                        /* fix 2 (re-review N3): asked again by the page / 現況 at their next refresh */
    for (int i = 0; L && i < L->n; i++) {
        const cover_t *c = &L->c[i];
        if (c->st == C_NONE) continue;
        if (c->st == C_UNRES || c->st == C_ERR) { unchecked++; continue; }   /* fix 0 (C5): folder not looked at yet: apart, not in total / 剩 */
        total++;
        done += s_tc.pak.open && pak_has(c->key);
    }
    bool ending = s_tc.run && L && s_tc.pos >= L->n;
    bool running = ((s_tc.run && !ending) || (s_tc.compact && !s_tc.why)) && (done < total || unchecked);   /* nothing missing: 完成 */
    __atomic_store_n(&s_tc.pseq, s_tc.pseq + 1, __ATOMIC_RELEASE);   /* fix 1 (review M3): odd while written */
    s_tc.ptidy = !running && (ending || (s_tc.compact && s_tc.why));   /* fix 0 (C4): everything in, a compaction (or the pass's end that starts it) waits for a stop */
    s_tc.punchecked = unchecked;
    int why = s_tc.stopped ? pc_tbg_why_of(s_tc.stopped) : s_tc.pak_failed ? PC_TBG_WRITE : running ? pc_tbg_why_of(s_tc.why) : PC_TBG_RUN;
    s_tc.pdone = done;
    s_tc.ptotal = total;
    s_tc.pwhy = (uint8_t)why;
    s_tc.pstate = (uint8_t)pc_tbg_state(running, why);
    __atomic_store_n(&s_tc.pseq, s_tc.pseq + 1, __ATOMIC_RELEASE);
}

static void post(uint32_t req);
void thumb_svc_progress_want(void) { if (!s_tc.want) { s_tc.want = true; if (!s_tc.pak.open) post(R_WAKE); } }   /* fix 1/2: the page / 現況 need the numbers (one read; asked at every refresh while they are open) */

void thumb_svc_progress(pc_tbg_prog_t *o)
{
    for (int k = 0; k < 8; k++) {                             /* one consistent snapshot (review M3): retried while the task writes it */
        uint32_t s1 = __atomic_load_n(&s_tc.pseq, __ATOMIC_ACQUIRE);
        *o = (pc_tbg_prog_t){ .state = s_tc.pstate, .why = s_tc.pwhy, .done = s_tc.pdone, .total = s_tc.ptotal, .manual = s_tc.manual, .user_paused = s_tc.user_paused,
                              .unchecked = s_tc.punchecked, .tidy = s_tc.ptidy };
        if (!(s1 & 1) && __atomic_load_n(&s_tc.pseq, __ATOMIC_ACQUIRE) == s1) break;
    }
    if (o->state != PC_TBG_ST_HELD) o->why = PC_TBG_RUN;
}

static const char *const STATE_NAMES[4] = { "done", "building", "held", "manual" };   /* PC_TBG_ST_* */
static void cache_stat(void)
{
    progress_update();
    int missing = 0, none = 0, unres = 0, alias = 0, bad = 0;
    covers_t *L = s_tc.list;
    for (int i = 0; L && i < L->n; i++) {
        const cover_t *c = &L->c[i];
        if (c->st == C_UNRES || c->st == C_ERR) unres++;
        else if (c->st == C_NONE) none++;
        else missing += !pak_has(c->key);
    }
    for (int i = 0; s_tc.pak.open && i < s_tc.pak.n; i++) {
        alias += pc_tpk_is_alias(&s_tc.pak.ent[i]);
        bad += pc_tpk_is_alias(&s_tc.pak.ent[i]) && !pc_tpk_alias_target(&s_tc.pak.ent[i]);
    }
    printf("APP thumbcache stat count=%d bytes=%lu cap=%d aliases=%d bad=%d missing=%d none=%d unresolved=%d tracks=%d building=%d compacting=%d wait=%s stopped=%s held=%s hits=%lu free_mb=%lld margin_mb=%llu int_free=%u int_big=%u jpeghw=%d"
           " pass=%s why=%s done=%lu total=%lu mode=%s user=%d underrun_holds=%lu unchecked=%lu tidy=%d\n",
           s_tc.pak.open ? s_tc.pak.n - alias : 0, (unsigned long)(s_tc.pak.open ? s_tc.pak.end : 0), s_tc.pak.open ? s_tc.pak.cap : 0, alias - bad, bad,
           missing, none, unres, L ? L->n : 0, s_tc.run, s_tc.compact, s_tc.why ? s_tc.why : "-", s_tc.stopped ? s_tc.stopped : s_tc.pak_failed ? "open_failed" : "-",
           s_tc.held ? s_tc.held : "-", (unsigned long)s_tc.hits, s_tc.space_known ? (long long)(s_tc.space_free >> 20) : -1LL,
           (unsigned long long)(pc_thumb_space_margin(s_tc.space_total) >> 20), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), jpeg_route_hw_ready(),
           STATE_NAMES[s_tc.pstate & 3], pc_tbg_why_name(s_tc.pwhy), (unsigned long)s_tc.pdone, (unsigned long)s_tc.ptotal, s_tc.manual ? "manual" : "auto",
           s_tc.user_paused, (unsigned long)s_tc.under.holds, (unsigned long)s_tc.punchecked, s_tc.ptidy);
}

/* test (2026-11-09): decode every library cover without the cache, to decide whether a cache is needed. hires 8d: ms = the routed decode
 * (dec= the decoder that made it); for a NATIVE picture also the software path alone (esp_new_jpeg, else ours) with sw_ms= and diff= (the mean
 * RGB565 channel difference, 0..31/63, between the two covers: the JPEG engine's colours against the software's) */
static uint32_t cover_diff(const uint16_t *a, const uint16_t *b)
{
    uint64_t d = 0;
    for (int i = 0; i < S * S; i++) {
        int x = a[i], y = b[i];
        d += (uint64_t)(abs((x >> 11) - (y >> 11)) + abs(((x >> 5) & 63) - ((y >> 5) & 63)) + abs((x & 31) - (y & 31)));
    }
    return (uint32_t)(d * 100 / (3 * S * S));                /* x100 */
}
static void bench(void)
{
    int n = 0;
    int64_t total = 0, worst = 0;
    for (uint32_t i = 0; i < g_lib.n; i++) {
        const pc_track_t *t = &g_lib.tracks[i];
        if (!t->apic_len || t->apic_len > APIC_MAX) continue;
        uint8_t *jpg = heap_caps_malloc(t->apic_len, MALLOC_CAP_SPIRAM);
        FILE *m = jpg ? fopen(pc_str(&g_lib, t->path), "rb") : NULL;
        bool ok = m && fseek(m, (long)t->apic_off, SEEK_SET) == 0 && fread(jpg, 1, t->apic_len, m) == t->apic_len;
        if (m) fclose(m);
        const char *which = "-";
        if (!thumb_jpeg_lock(-1)) { heap_caps_free(jpg); continue; }   /* review M-4: the album and the pass use the decoders too */
        int64_t t0 = esp_timer_get_time();
        uint16_t *px = ok ? make(jpg, t->apic_len, NULL, 0, &which) : NULL;
        int64_t ms = (esp_timer_get_time() - t0) / 1000;
        pc_jpeg_info_t in;
        int route = ok ? pc_jpeg_probe(jpg, t->apic_len, &in) : PC_JPEG_BAD;
        char extra[64] = "";
        if (px && !strcmp(which, "hw")) {                     /* the same picture through the software path */
            bool nomem;
            int w, h, rc;
            int64_t t1 = esp_timer_get_time();
            uint16_t *sw = low_internal() ? NULL : make_esp(jpg, t->apic_len, in.w, in.h, &nomem);
            if (!sw) { uint16_t *full = jpeg_route_own(jpg, t->apic_len, S, &w, &h, &rc); sw = full ? to_cover(full, w, h) : NULL; }
            if (sw) snprintf(extra, sizeof extra, " sw_ms=%lld diff=%u", (esp_timer_get_time() - t1) / 1000, (unsigned)cover_diff(px, sw));
            heap_caps_free(sw);
        }
        thumb_jpeg_unlock();
        heap_caps_free(jpg);
        printf("APP thumbbench idx=%lu bytes=%lu ms=%lld ok=%d route=%s dec=%s%s\n", (unsigned long)i, (unsigned long)t->apic_len, ms, px != NULL,
               pc_jpeg_route_name(route), which, extra);
        heap_caps_free(px);
        n++;
        total += ms;
        if (ms > worst) worst = ms;
    }
    printf("APP thumbbench covers=%d avg_ms=%lld worst_ms=%lld\n", n, n ? total / n : 0, worst);
}
bool thumb_svc_busy(void) { return s_busy; }

/* one request: the pack, else (<= 48 kHz, and the playing track or nothing playing) make it now and add it; above 48 kHz the placeholder
 * now and the request waits (the newest one) for the pause to end; another track's cover while music plays: the placeholder. The playing
 * track's request waits (at most 2 s, no answer meanwhile) until the audio service has opened the new source, so the decision uses its
 * rate, not the previous track's (re-review I-1 B); settled: that wait is over */
static void serve(const job_t *j, bool settled)
{
    char img[300] = "";
    bool now = j->len & REQ_NOW;
    uint32_t alen = j->len & ~REQ_NOW, len = alen, src = 0;
    uint64_t key = 0;
    int kind = g_sd_ok ? resolve(j->path, j->off, &len, img, sizeof img, &key) : C_NONE;
    bool found = kind == C_APIC || kind == C_IMG;
    s_busy = true;
    uint16_t *px = found ? pak_get(key) : NULL;
    if (!px && found && now && !settled && opening_now()) {   /* no answer yet: decided when the source is open */
        s_busy = false;
        s_tc.wait = *j;
        s_tc.waiting = true;
        s_tc.wait_since = esp_timer_get_time();
        return;
    }
    done_t *d = malloc(sizeof *d);
    if (!d) { s_busy = false; heap_caps_free(px); return; }
    d->req = j->req; d->cb = j->cb; d->u = j->u; d->px = px;
    if (px) s_tc.hits++;
    else if (found && pak_ready(0, false) && pc_tpk_is_bad(&s_tc.pak, key)) ;   /* tried before: the placeholder */
    else if (found && paused_now(&src)) {
        if (!s_tc.waiting || s_tc.wait.req != j->req) printf("APP thumb wait reason=hires src=%lu\n", (unsigned long)src);
        s_tc.wait = *j;
        s_tc.waiting = true;
        s_tc.wait_since = 0;                                  /* the hi-res wait: no time limit */
    } else if (found && (now || idle_now()) && !opening_now()) {   /* re-review 2 m4: after the 2 s, still opening: the placeholder, no read */
        if (!now && !s_tc.space_known) space_refresh();       /* nothing plays: the first free-space read may happen here */
        make_cover(!now, kind, j->path, j->off, kind == C_IMG ? len : alen, img, key, &d->px, j->req);   /* shown even when the write is held */
    }
    s_busy = false;
    ui_async(deliver, d);
}

/* the phone's cover art (speaker mode, AVRCP; thumb_svc_request_jpeg): a JPEG in memory, made with the same decoder, lock and low-memory
 * gate as a library cover (thumb_svc_low_mem says when that gate refused it). Never written to the pack: it is not a library track and has
 * no key there. Not held by the hi-res pause: that pause keeps the decoder off the card a hi-res source streams from, and these bytes come
 * from memory (the phone's stream itself is the playing source, at its A2DP rate) */
static void serve_mem(const job_t *j)
{
    done_t *d = malloc(sizeof *d);
    if (!d) return;
    d->req = j->req; d->cb = j->cb; d->u = j->u;
    bool fmt;
    s_busy = true;
    d->px = decode(j->mem, j->len, j->len >= 8 && !memcmp(j->mem, "\x89PNG\r\n\x1a\n", 8), j->path, &fmt, j->req);
    s_busy = false;
    ui_async(deliver, d);
}

/* the square panel's now-playing page (thumb_svc_request_big): the playing track's cover at a larger size, straight from its embedded picture or the image file next to it, with the
 * same decoders, lock and gates as the 240 px one. Never read from or written to the pack (it keeps 240 px; the card's format is unchanged), so a track change costs one more decode.
 * Nothing is made while a source above 48 kHz plays or while the source is opening (the caller keeps the 240 px picture then: NULL). */
static void serve_big(const job_t *j)
{
    done_t *d = malloc(sizeof *d);
    if (!d) return;
    d->req = j->req; d->cb = j->cb; d->u = j->u; d->px = NULL;
    char img[300] = "";
    uint32_t len = j->len & ~REQ_NOW;
    uint64_t key = 0;
    const int kind = g_sd_ok ? resolve(j->path, j->off, &len, img, sizeof img, &key) : C_NONE;
    s_busy = true;
    const size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), int_big = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if ((kind == C_APIC || kind == C_IMG) && !pc_lowmem_big_ok(int_free, int_big)) {
        /* night3 A: below the project's low-memory line (pc_lowmem.h) no sharp decode is started: the stretched thumbnail stays (the caller keeps it on NULL) */
        printf("APP thumb big skip reason=low_internal free=%u largest=%u line=%u\n", (unsigned)int_free, (unsigned)int_big, (unsigned)PC_LOWMEM_COVER_FREE);
    } else if ((kind == C_APIC || kind == C_IMG) && !paused_now(NULL) && !opening_now()) {
        uint8_t *buf = kind == C_IMG ? read_bytes(img, 0, len) : read_bytes(j->path, j->off, len);
        if (buf) {
            bool fmt;
            const bool png = kind == C_IMG ? is_png(img) : len >= 8 && !memcmp(buf, "\x89PNG\r\n\x1a\n", 8);
            s_sz_v = j->size;
            const uint32_t min0 = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
            d->px = decode(buf, len, png, kind == C_IMG ? img : j->path, &fmt, j->req);
            s_sz_v = 0;
            { const uint32_t min1 = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);   /* the internal heap's all-time minimum before and after: a drop here is this decode's peak */
              printf("APP thumb big done px=%d free_before=%u int_min_before=%u int_min_after=%u peak_cost=%u\n", d->px != NULL, (unsigned)int_free, (unsigned)min0, (unsigned)min1,
                     min1 < min0 ? (unsigned)(int_free - min1) : 0u); }
            heap_caps_free(buf);
        }
    }
    s_busy = false;
    ui_async(deliver, d);
}

static void take_snapshot(void)
{
    covers_t *n = __atomic_exchange_n(&s_tc.pending, NULL, __ATOMIC_ACQ_REL);
    if (!n) return;
    if (s_tc.compact) compact_end(false);          /* a new library: its own pass decides what is stale */
    heap_caps_free(s_tc.list);
    s_tc.list = n;
    s_tc.dir[0] = 0; s_tc.dir_img[0] = 0; s_tc.dir_key = 0;
    pass_start();
    printf("APP thumbcache pass tracks=%d gen=%08lx\n", n->n, (unsigned long)n->gen);
}

/* `thumbcache build`: the user's retry - every bad mark is forgotten (re-review I-3), open failures are tried again, a pass starts */
static void build_cmd(void)
{
    if (s_tc.compact) { printf("APP thumbcache build deferred reason=compacting\n"); s_tc.rerun = true; return; }   /* review M10; T8e: a pass after it */
    s_tc.pak_failed = false;
    s_tc.stopped = NULL;
    pak_ready(0, false);                                      /* re-review 2 m1: the marks on the card, even before any cover was asked for */
    int cleared = 0, kept = 0;
    for (int i = s_tc.pak.open ? s_tc.pak.n - 1 : -1; i >= 0; i--) {
        if (i >= s_tc.pak.n) continue;                        /* the table may have been re-read */
        const pc_tpk_ent_t *e = &s_tc.pak.ent[i];
        if (!pc_tpk_is_alias(e) || pc_tpk_alias_target(e)) continue;
        if (pak_write(false, W_FORGET, e->key, 0, NULL) == WR_OK) cleared++;
        else kept++;
    }
    printf("APP thumbcache build tracks=%d cleared_bad=%d kept_bad=%d\n", s_tc.list ? s_tc.list->n : 0, cleared, kept);
    pass_start();
}

static void task(void *arg)
{
    (void)arg;
    static job_t j;
    bool worked = false;
    for (;;) {
        take_snapshot();
        TickType_t wait = portMAX_DELAY;
        if (s_tc.waiting) wait = pdMS_TO_TICKS(s_tc.wait_since ? 100 : 500);
        if (s_tc.run || s_tc.compact) { TickType_t w = s_tc.why || s_tc.held ? pdMS_TO_TICKS(1000) : worked ? pdMS_TO_TICKS(pc_tbg_breather_ms(s_tc.loaded)) : 0;   /* T8e: longer while a source is loaded */ if (w < wait) wait = w; }
        worked = false;
        progress_update();                                    /* hires T8e: what the page and 現況 read */
        bool got = xQueueReceive(s_q, &j, wait) == pdTRUE;
        under_check();                                        /* fix 1: the breather window of the last step ends here */
        pc_tbg_under_disarm(&s_tc.under);
        if (got) {
            if (j.mem) {                                      /* the phone's cover art: the job owns its copy */
                if (j.req == s_req) { s_tc.waiting = false; if (j.size) s_sz_v = j.size; serve_mem(&j); s_sz_v = 0; }   /* else superseded */
                heap_caps_free(j.mem);
            }
            else if (j.req == R_BENCH) bench();
            else if (j.req == R_STAT) cache_stat();
            else if (j.req == R_BUILD || j.req == R_BUILDNOW) { s_tc.requested = true; if (j.req == R_BUILDNOW) s_tc.user_paused = false; build_cmd(); }   /* T8e: either mode */
            else if (j.size) { if (j.req == s_req) { s_tc.waiting = false; serve_big(&j); } }   /* a larger cover (the square panel's now-playing page); else superseded */
            else if (j.req != R_WAKE && j.req == s_req) { s_tc.waiting = false; serve(&j, false); }   /* else superseded */
            continue;                                         /* requests before the builder */
        }
        if (s_tc.waiting) {
            if (s_tc.wait.req != s_req) s_tc.waiting = false;     /* a newer request came (and was answered) */
            else if (s_tc.wait_since) {                       /* the playing track's source is opening */
                if (!opening_now() || esp_timer_get_time() - s_tc.wait_since > 2000000) { s_tc.waiting = false; serve(&s_tc.wait, true); continue; }
            }
            else if (!paused_now(NULL)) { s_tc.waiting = false; serve(&s_tc.wait, true); continue; }
        }
        if (s_tc.compact) worked = compact_step();
        else if (s_tc.run) { worked = build_step(); under_check(); pc_tbg_under_step_end(&s_tc.under, worked); }   /* fix 1/2: only a working step's (and its breather's) underruns */
    }
}

void thumb_svc_start(void)
{
    s_tc.manual = !cfg_int("cover_auto", 1);                  /* hires T8e: 封面快取 自動 (default) / 手動 */
    s_tc.names_pool = heap_caps_malloc(DIR_POOL, MALLOC_CAP_SPIRAM);
    s_tc.names = heap_caps_malloc(sizeof(char *) * DIR_NAMES, MALLOC_CAP_SPIRAM);
    s_q = xQueueCreate(4, sizeof(job_t));
    s_jpeg_mx = xSemaphoreCreateMutex();
    jpeg_route_init();                            /* hires 8d: the JPEG engine, once, before the heap is cut up */
    xTaskCreatePinnedToCoreWithCaps(task, "thumb", 16 * 1024, NULL, 1, NULL, 0, MALLOC_CAP_SPIRAM);
}

static void post(uint32_t req) { job_t j = { .req = req }; if (s_q) xQueueSend(s_q, &j, 0); }
/* hires T8e: the setting and the 封面快取 page (LVGL task); the task takes them at its next step (R_WAKE: at once) */
void thumb_svc_set_auto(bool on) { s_tc.manual = !on; printf("APP thumbcache mode=%s\n", on ? "auto" : "manual"); post(R_WAKE); }
void thumb_svc_user_pause(bool on) { s_tc.user_paused = on; printf("APP thumbcache user_pause=%d\n", on); post(R_WAKE); }
void thumb_svc_build_now(void) { printf("APP thumbcache build_now\n"); post(R_BUILDNOW); }
void thumb_svc_bench(void) { post(R_BENCH); }
void thumb_svc_cache_stat(void) { post(R_STAT); }
void thumb_svc_cache_build(void)
{
    uint32_t src;
    if (paused_now(&src)) { printf("APP thumbcache build refused reason=hires src=%lu\n", (unsigned long)src); return; }
    post(R_BUILD);
}

/* LVGL context (g_lib's owner): the library's tracks as they are now, after an index load or a scan that succeeded (the index is
 * published first; the pack follows in the background while nothing plays) */
void thumb_svc_library_changed(void)
{
    size_t pool = 0;
    for (int i = 0; i < g_lib.n; i++) pool += strlen(pc_str(&g_lib, g_lib.tracks[i].path)) + 1;
    covers_t *c = heap_caps_malloc(sizeof *c + sizeof(cover_t) * (size_t)g_lib.n + pool, MALLOC_CAP_SPIRAM);
    if (!c) { printf("APP thumbcache snapshot failed tracks=%d\n", g_lib.n); return; }
    c->n = g_lib.n;
    c->c = (cover_t *)(c + 1);
    c->pool = (char *)(c->c + g_lib.n);
    uint32_t gen = 2166136261u;
    size_t used = 0;
    for (int i = 0; i < g_lib.n; i++) {
        const pc_track_t *t = &g_lib.tracks[i];
        const char *p = pc_str(&g_lib, t->path);
        size_t l = strlen(p) + 1;
        memcpy(c->pool + used, p, l);
        bool apic = t->apic_len && t->apic_len <= APIC_MAX;
        c->c[i] = (cover_t){ .key = apic ? pc_thumb_key(p, t->apic_off, t->apic_len) : 0, .off = t->apic_off, .len = apic ? t->apic_len : 0,
                             .path = (uint32_t)used, .st = apic ? C_APIC : C_UNRES };
        gen = (gen ^ pc_thumb_hash(p) ^ t->apic_off ^ (t->apic_len << 7)) * 16777619u;   /* the library's generation */
        used += l;
    }
    c->gen = gen | 1;                             /* never 0 (a pack made by attach has gen 0) */
    heap_caps_free(__atomic_exchange_n(&s_tc.pending, c, __ATOMIC_ACQ_REL));   /* one the task never took */
    post(R_WAKE);                                 /* before thumb_svc_start (boot): the task takes it when it starts */
}

static uint32_t request(const char *path, uint32_t off, uint32_t len, thumb_cb cb, void *u)
{
    job_t j = { .off = off, .len = len, .cb = cb, .u = u, .req = ++s_req };
    snprintf(j.path, sizeof j.path, "%s", path);
    xQueueSend(s_q, &j, 0);
    return j.req;
}
/* len 0: the track has no embedded picture (an image file next to it may be its cover) */
uint32_t thumb_svc_request(const char *path, uint32_t off, uint32_t len, thumb_cb cb, void *u) { return request(path, off, len & ~REQ_NOW, cb, u); }
uint32_t thumb_svc_request_now(const char *path, uint32_t off, uint32_t len, thumb_cb cb, void *u) { return request(path, off, (len & ~REQ_NOW) | REQ_NOW, cb, u); }

static uint32_t request_jpeg(const uint8_t *jpg, uint32_t len, int size, thumb_cb cb, void *u)
{
    if (!jpg || !len || len > APIC_MAX) return 0;
    job_t j = { .len = len, .cb = cb, .u = u, .mem = heap_caps_malloc(len, MALLOC_CAP_SPIRAM), .size = (uint16_t)size };
    if (!j.mem) return 0;
    memcpy(j.mem, jpg, len);
    snprintf(j.path, sizeof j.path, "bt");                /* what a failure line names */
    j.req = ++s_req;
    if (xQueueSend(s_q, &j, 0) != pdTRUE) { heap_caps_free(j.mem); return 0; }   /* the queue owns it once sent */
    return j.req;
}
uint32_t thumb_svc_request_jpeg(const uint8_t *jpg, uint32_t len, thumb_cb cb, void *u) { return request_jpeg(jpg, len, 0, cb, u); }
uint32_t thumb_svc_request_jpeg_big(const uint8_t *jpg, uint32_t len, int size, thumb_cb cb, void *u)
{
    return size >= PC_THUMB_SIZE && size <= THUMB_BIG_MAX ? request_jpeg(jpg, len, size, cb, u) : 0;
}
uint32_t thumb_svc_request_big(const char *path, uint32_t off, uint32_t len, int size, thumb_cb cb, void *u)
{
    if (size < PC_THUMB_SIZE || size > THUMB_BIG_MAX) return 0;
    job_t j = { .off = off, .len = (len & ~REQ_NOW) | REQ_NOW, .cb = cb, .u = u, .req = ++s_req, .size = (uint16_t)size };
    snprintf(j.path, sizeof j.path, "%s", path);
    xQueueSend(s_q, &j, 0);
    return j.req;
}
