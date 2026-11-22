// SPDX-License-Identifier: LGPL-2.1-or-later
/* Photo album cache (spec 2026-11-11-album-design.md). One core-0 task at priority 1: serves frame loads first,
 * then looks at the card (when asked), then builds missing frames one at a time between them. */
#include "album_svc.h"
#include "pc/pc_paths.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "jpeg_route.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pc/pc_album.h"
#include "pc/pc_lowmem.h"
#include "cfg.h"
#include "library_svc.h"
#include "lowmem.h"
#include "power_svc.h"
#include "thumb_svc.h"
#include "ui.h"

#define MAX_PHOTOS 2000
#define POOL_BYTES (256 * 1024)
#define MAX_JPEG (8u << 20)                  /* bigger files are skipped (PSRAM stays unfragmented) */
#define FRAME_BYTES (ALBUM_PX * ALBUM_PX * 2)
#define HDR_BYTES 8
#define MAX_DEPTH 4
#define RESCAN_MS 60000

enum { ST_MISSING, ST_READY, ST_FAILED };
typedef struct { const char *path; uint64_t key; uint8_t state; } photo_t;
typedef struct { int type, idx; uint32_t req; album_cb cb; void *u; } msg_t;
typedef struct { uint32_t req; uint16_t *px; album_cb cb; void *u; } done_t;
enum { M_LOAD, M_SCAN, M_CLEAR };

static QueueHandle_t s_q;
static photo_t *s_ph;
static uint16_t *s_ready;                    /* indices into s_ph, path order */
static char *s_pool;
static size_t s_pool_used;
static int s_n, s_missing;
static volatile int s_nready, s_total;
static volatile bool s_building;
static volatile uint32_t s_req;
static int64_t s_last_scan_ms = -RESCAN_MS;
static bool s_scan_pending;
static const char *volatile s_wait;          /* why building waits (can_build), NULL when not */
static uint32_t s_built, s_build_ms_sum, s_build_ms_max, s_loads, s_load_ms_sum;

int album_svc_ready(void) { return s_nready; }
const char *album_svc_waiting(void) { return s_missing > 0 ? s_wait : NULL; }
int album_svc_total(void) { return s_total; }
bool album_svc_building(void) { return s_building; }

static void cache_path(uint64_t key, char *out, size_t n, const char *ext)
{
    char nm[PC_ALBUM_NAME_MAX];
    pc_album_cache_name(key, nm);
    if (ext) { char *dot = strrchr(nm, '.'); if (dot) strcpy(dot, ext); }
    snprintf(out, n, "%s/%s", ALBUM_CACHE, nm);
}

static void ready_rebuild(void)
{
    int k = 0;
    for (int i = 0; i < s_n; i++) if (s_ph[i].state == ST_READY) s_ready[k++] = (uint16_t)i;
    s_nready = k;
}

/* ---- scan ---- */
static void add(const char *path, const struct stat *st)
{
    size_t len = strlen(path) + 1;
    if (s_n >= MAX_PHOTOS || s_pool_used + len > POOL_BYTES) return;
    char *p = s_pool + s_pool_used;
    memcpy(p, path, len);
    s_pool_used += len;
    s_ph[s_n++] = (photo_t){ .path = p, .key = pc_album_key(path, (uint32_t)st->st_size, (int64_t)st->st_mtime), .state = ST_MISSING };
}
static void walk(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char p[300];
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (e->d_type == DT_DIR) { if (depth < MAX_DEPTH) walk(p, depth + 1); continue; }
        if (!pc_album_is_photo(e->d_name)) continue;
        struct stat st;
        if (!stat(p, &st) && st.st_size > 0) add(p, &st);
    }
    closedir(d);
}
static int by_path(const void *a, const void *b) { return strcmp(((const photo_t *)a)->path, ((const photo_t *)b)->path); }
static int by_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y; }

static void scan(void)
{
    int64_t t0 = esp_timer_get_time();
    s_n = 0;
    s_pool_used = 0;
    walk(ALBUM_DIR, 0);
    qsort(s_ph, (size_t)s_n, sizeof *s_ph, by_path);
    s_total = s_n;
    uint64_t *keys = heap_caps_malloc(sizeof(uint64_t) * (s_n + 1), MALLOC_CAP_SPIRAM);
    uint64_t *have = heap_caps_malloc(sizeof(uint64_t) * MAX_PHOTOS, MALLOC_CAP_SPIRAM);
    int nhave = 0, removed = 0;
    if (keys && have) {
        for (int i = 0; i < s_n; i++) keys[i] = s_ph[i].key;
        pc_album_sort_keys(keys, s_n);
        pc_int_mkdir(ALBUM_CACHE);
        DIR *d = opendir(ALBUM_CACHE);
        struct dirent *e;
        char p[300];
        while (d && (e = readdir(d))) {
            if (pc_album_stale(e->d_name, keys, s_n)) {         /* its photo is gone or changed, or a half-written file */
                snprintf(p, sizeof p, "%s/%s", ALBUM_CACHE, e->d_name);
                removed += unlink(p) == 0;
                continue;
            }
            uint64_t k;
            if (nhave < MAX_PHOTOS && pc_album_parse_name(e->d_name, &k)) have[nhave++] = k;
        }
        if (d) closedir(d);
        qsort(have, (size_t)nhave, sizeof *have, by_u64);
        for (int i = 0; i < s_n; i++)
            if (bsearch(&s_ph[i].key, have, (size_t)nhave, sizeof *have, by_u64)) s_ph[i].state = ST_READY;
    }
    heap_caps_free(keys);
    heap_caps_free(have);
    s_missing = 0;
    for (int i = 0; i < s_n; i++) s_missing += s_ph[i].state == ST_MISSING;
    ready_rebuild();
    s_last_scan_ms = esp_timer_get_time() / 1000;
    printf("APP album scan photos=%d ready=%d missing=%d stale_removed=%d ms=%lld\n", s_n, s_nready, s_missing, removed,
           (esp_timer_get_time() - t0) / 1000);
}

/* ---- build one frame ---- */
static bool can_build(const char **why)
{
    *why = !g_sd_ok ? "no_card" : lowmem_level() != PC_LOWMEM_OK ? "lowmem"
         : heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < PC_LOWMEM_COVER_BLOCK || heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < PC_LOWMEM_COVER_FREE ? "low_internal"
         : thumb_svc_busy() ? "cover" : library_scanning() ? "library_scan"
         : !pc_power_allows_heavy(power_svc_state()) ? "battery" : NULL;
    return !*why;
}

/* hires 8d: by the route (jpeg_route.h): NATIVE -> the JPEG engine when its full-size output fits, else esp_new_jpeg scaled; OWN -> our
 * decoder; progressive / unsupported / bad: the route name is the reason */
static uint16_t *decode_esp(const uint8_t *jpg, uint32_t len, int w, int h, int *ow, int *oh, const char **why);
static uint16_t *decode(const uint8_t *jpg, uint32_t len, int *ow, int *oh, const char **why)
{
    pc_jpeg_info_t in;
    int route = pc_jpeg_probe(jpg, len, &in);
    if (route == PC_JPEG_NATIVE) {
        const char *hw_why;
        uint16_t *px = jpeg_route_hw(jpg, len, &in, ow, oh, &hw_why);
        if (px) { *why = NULL; return px; }
        if (!strcmp(hw_why, "no_psram")) { *why = "no_psram"; return NULL; }
        if (pc_jpeg_min_bytes(&in) > PC_JPEG_BUDGET_BYTES) { *why = "too_big"; return NULL; }   /* never fits even at 1/8 */
        return decode_esp(jpg, len, in.w, in.h, ow, oh, why);
    }
    if (route == PC_JPEG_OWN) {
        int rc;
        uint16_t *px = jpeg_route_own(jpg, len, ALBUM_PX, ow, oh, &rc);
        *why = px ? NULL : rc == PC_JPEG_E_NOMEM ? "no_psram" : rc == PC_JPEG_E_TOO_BIG ? "too_big" : "decode";
        return px;
    }
    *why = route == PC_JPEG_BAD ? "not_jpeg" : pc_jpeg_route_name(route);
    return NULL;
}

static uint16_t *decode_esp(const uint8_t *jpg, uint32_t len, int w, int h, int *ow, int *oh, const char **why)
{
    int dw, dh;
    pc_album_decode_size(w, h, ALBUM_PX, &dw, &dh);
    if (dw < 8 || dh < 8) { *why = "tiny"; return NULL; }
    jpeg_dec_config_t c = DEFAULT_JPEG_DEC_CONFIG();
    c.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    if (dw != w || dh != h) { c.scale.width = (uint16_t)dw; c.scale.height = (uint16_t)dh; }
    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&c, &dec) != JPEG_ERR_OK) { *why = "open"; return NULL; }
    jpeg_dec_io_t io = { .inbuf = (uint8_t *)jpg, .inbuf_len = (int)len };
    jpeg_dec_header_info_t hi;
    uint8_t *out = NULL;
    int olen = 0;
    *why = "header";                                        /* progressive JPEGs end here */
    if (jpeg_dec_parse_header(dec, &io, &hi) == JPEG_ERR_OK && jpeg_dec_get_outbuf_len(dec, &olen) == JPEG_ERR_OK &&
        (out = heap_caps_aligned_calloc(16, 1, (size_t)olen, MALLOC_CAP_SPIRAM))) {
        io.outbuf = out;
        *why = "decode";
        if (jpeg_dec_process(dec, &io) != JPEG_ERR_OK) { heap_caps_free(out); out = NULL; }
        else *why = NULL;
    }
    jpeg_dec_close(dec);
    *ow = dw;
    *oh = dh;
    return (uint16_t *)out;
}

static bool write_frame(uint64_t key, const uint16_t *px)
{
    char tmp[300], fin[300];
    cache_path(key, tmp, sizeof tmp, ".tmp");
    cache_path(key, fin, sizeof fin, NULL);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    uint8_t hdr[HDR_BYTES] = { 'J', 'A', '5', '6', ALBUM_PX & 255, ALBUM_PX >> 8, ALBUM_PX & 255, ALBUM_PX >> 8 };
    bool ok = fwrite(hdr, 1, HDR_BYTES, f) == HDR_BYTES;
    const uint8_t *b = (const uint8_t *)px;
    for (size_t o = 0; ok && o < FRAME_BYTES; o += 16384) {   /* in pieces: the audio reads the card too */
        size_t n = FRAME_BYTES - o < 16384 ? FRAME_BYTES - o : 16384;
        ok = fwrite(b + o, 1, n, f) == n;
        vTaskDelay(1);
    }
    ok = (fclose(f) == 0) && ok;
    if (ok) { unlink(fin); ok = rename(tmp, fin) == 0; }
    if (!ok) unlink(tmp);
    return ok;
}

static void build_one(void)
{
    int i = 0;
    while (i < s_n && s_ph[i].state != ST_MISSING) i++;
    if (i == s_n) { s_missing = 0; return; }
    photo_t *p = &s_ph[i];
    const char *why = NULL;
    int64_t t0 = esp_timer_get_time(), t_read = 0, t_dec = 0, t_render = 0;
    struct stat st;
    uint8_t *jpg = NULL;
    uint16_t *full = NULL, *frame = NULL;
    int w = 0, h = 0, orient = 1;
    s_building = true;
    if (stat(p->path, &st) || st.st_size <= 0) why = "gone";
    else if ((uint32_t)st.st_size > MAX_JPEG) why = "too_big";
    else if (!(jpg = heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_SPIRAM))) why = "no_psram";
    else {
        FILE *f = fopen(p->path, "rb");
        size_t got = 0;
        for (size_t o = 0; f && o < (size_t)st.st_size; o += got) {   /* in pieces, as above */
            size_t n = (size_t)st.st_size - o < 32768 ? (size_t)st.st_size - o : 32768;
            got = fread(jpg + o, 1, n, f);
            if (got != n) { got = 0; break; }
            vTaskDelay(1);
        }
        bool ok = f && got;
        if (f) fclose(f);
        t_read = esp_timer_get_time();
        if (!ok) why = "read";
        else if (!thumb_jpeg_lock(0)) { why = "busy"; }    /* a cover is being decoded: try again later */
        else {
            orient = pc_exif_orientation(jpg, (size_t)st.st_size);
            full = decode(jpg, (uint32_t)st.st_size, &w, &h, &why);
            thumb_jpeg_unlock();
            t_dec = esp_timer_get_time();
        }
    }
    heap_caps_free(jpg);
    if (full && (frame = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM))) {
        pc_album_render(full, w, h, orient, frame, ALBUM_PX);
        t_render = esp_timer_get_time();
        if (!write_frame(p->key, frame)) why = "write";
    } else if (full) why = "no_psram";
    heap_caps_free(full);
    heap_caps_free(frame);
    s_building = false;
    if (why && !strcmp(why, "busy")) return;                /* stays missing */
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    if (why) {
        p->state = ST_FAILED;
        printf("APP album skip reason=%s bytes=%ld path=%s\n", why, (long)st.st_size, p->path);
    } else {
        p->state = ST_READY;
        s_built++;
        s_build_ms_sum += (uint32_t)ms;
        if ((uint32_t)ms > s_build_ms_max) s_build_ms_max = (uint32_t)ms;
        printf("APP album built ms=%d read=%lld decode=%lld render=%lld write=%lld jpeg_bytes=%ld src=%dx%d orient=%d\n", ms,
               (t_read - t0) / 1000, (t_dec - t_read) / 1000, (t_render - t_dec) / 1000, (esp_timer_get_time() - t_render) / 1000,
               (long)st.st_size, w, h, orient);
    }
    s_missing--;
    ready_rebuild();
}

/* ---- load ---- */
static void deliver(void *arg)
{
    done_t *d = arg;
    d->cb(d->req, d->px, d->u);
    free(d);
}
static void serve_load(const msg_t *m)
{
    done_t *d = malloc(sizeof *d);
    if (!d) return;
    *d = (done_t){ .req = m->req, .cb = m->cb, .u = m->u };
    int64_t t0 = esp_timer_get_time();
    if (m->idx >= 0 && m->idx < s_nready) {
        char p[300];
        cache_path(s_ph[s_ready[m->idx]].key, p, sizeof p, NULL);
        FILE *f = fopen(p, "rb");
        uint8_t hdr[HDR_BYTES];
        uint16_t *px = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
        bool ok = f && px && fread(hdr, 1, HDR_BYTES, f) == HDR_BYTES && !memcmp(hdr, "JA56", 4);
        for (size_t o = 0; ok && o < FRAME_BYTES; o += 32768) {
            size_t n = FRAME_BYTES - o < 32768 ? FRAME_BYTES - o : 32768;
            ok = fread((uint8_t *)px + o, 1, n, f) == n;
        }
        if (f) fclose(f);
        if (ok) d->px = px;
        else heap_caps_free(px);
        if (!ok) printf("APP album load fail idx=%d\n", m->idx);
    }
    uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    s_loads++;
    s_load_ms_sum += ms;
    ui_async(deliver, d);
}

static void clear(void)                      /* console test: every frame goes, the scan after it rebuilds */
{
    DIR *d = opendir(ALBUM_CACHE);
    struct dirent *e;
    char p[300];
    int n = 0;
    while (d && (e = readdir(d))) { snprintf(p, sizeof p, "%s/%s", ALBUM_CACHE, e->d_name); n += unlink(p) == 0; }
    if (d) closedir(d);
    for (int i = 0; i < s_n; i++) s_ph[i].state = ST_MISSING;
    s_missing = s_n;
    ready_rebuild();
    printf("APP album cleared=%d rmdir=%d\n", n, rmdir(ALBUM_CACHE));
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        const char *why = NULL;
        TickType_t wait = portMAX_DELAY;
        if (s_scan_pending) wait = 0;
        else if (s_missing > 0) { wait = pdMS_TO_TICKS(can_build(&why) ? 500 : 5000); s_wait = why; }   /* a breather between photos */
        else s_wait = NULL;
        msg_t m;
        if (xQueueReceive(s_q, &m, wait) == pdTRUE) {
            if (m.type == M_LOAD) serve_load(&m);
            else if (m.type == M_CLEAR) { clear(); s_scan_pending = true; }
            else if (esp_timer_get_time() / 1000 - s_last_scan_ms >= RESCAN_MS) s_scan_pending = true;
            continue;                                         /* loads before anything else */
        }
        if (s_scan_pending) { s_scan_pending = false; if (g_sd_ok) scan(); continue; }
        if (s_missing > 0 && can_build(&why)) build_one();
    }
}

static void soon_cb(void *arg) { (void)arg; album_svc_wake(); }

void album_svc_start(bool soon)
{
    s_ph = heap_caps_calloc(MAX_PHOTOS, sizeof *s_ph, MALLOC_CAP_SPIRAM);
    s_ready = heap_caps_calloc(MAX_PHOTOS, sizeof *s_ready, MALLOC_CAP_SPIRAM);
    s_pool = heap_caps_malloc(POOL_BYTES, MALLOC_CAP_SPIRAM);
    s_q = xQueueCreate(6, sizeof(msg_t));
    if (!s_ph || !s_ready || !s_pool || !s_q) { printf("APP album start failed\n"); return; }
    xTaskCreatePinnedToCoreWithCaps(task, "album", 8 * 1024, NULL, 1, NULL, 0, MALLOC_CAP_SPIRAM);
    if (soon) {
        static esp_timer_handle_t t;
        const esp_timer_create_args_t a = { .callback = soon_cb, .name = "album" };
        if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, 30ULL * 1000 * 1000);
    }
}

void album_svc_wake(void)
{
    msg_t m = { .type = M_SCAN };
    if (s_q) xQueueSend(s_q, &m, 0);
}

uint32_t album_svc_load(int idx, album_cb cb, void *u)
{
    msg_t m = { .type = M_LOAD, .idx = idx, .req = ++s_req, .cb = cb, .u = u };
    if (!s_q || xQueueSend(s_q, &m, 0) != pdTRUE) return 0;
    return m.req;
}

void album_svc_stat(void)
{
    printf("APP album stat photos=%d ready=%d missing=%d building=%d built=%lu build_avg_ms=%lu build_max_ms=%lu loads=%lu load_avg_ms=%lu\n",
           s_total, s_nready, s_missing, s_building, (unsigned long)s_built, (unsigned long)(s_built ? s_build_ms_sum / s_built : 0),
           (unsigned long)s_build_ms_max, (unsigned long)s_loads, (unsigned long)(s_loads ? s_load_ms_sum / s_loads : 0));
}

void album_svc_clear(void) { msg_t m = { .type = M_CLEAR }; if (s_q) xQueueSend(s_q, &m, 0); }
