// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes thumb_svc (cover thumbnails decoded from the APIC JPEG on core 0). */
/* Behaviour: a request is answered in LVGL context (next step) with a 240x240 test cover drawn from the album name of the library track at that path (sim_cover.c); a track without a cover (apic_len 0) or not in the library gets NULL; the newest request wins.
 * hires T8c: the pause rule is the firmware's (pc_thumb_hires_pause on the fake audio_svc_info src_rate, `simaudiorate`): while it holds, a request with a cover gets NULL (the placeholder) and
 * waits; a 250 ms LVGL timer serves it (the same req) once the pause ends. The pause's answers print "SIM thumb req=<n> px=<0|1> why=<hires|after|none>" (a plain answer prints nothing). The pack is never there (no card files; image files next to tracks are not looked for):
 * `thumbcache stat` prints the board's line with count=0 bytes=0 missing=<tracks with an embedded picture>, `thumbcache build` is refused while hi-res plays, as on the board, else prints the board's start line and does nothing. */
/* thumb_svc_request_jpeg (the phone's cover art): answered the same way, the test cover drawn from "bt <FNV-1a of the bytes>" (any bytes: nothing is decoded); as on the board it is
 * not held by the hi-res pause (no card read) and never goes into the pack. */
/* "simthumblow <n>": the next n of those (the phone's cover) are refused as for low internal memory (NULL, thumb_svc_low_mem true), as the board's guard does. */
/* hires 8d: the embedded picture is read and routed as on the board (pc_jpeg_probe). OWN pictures (the layouts the S31's decoders refuse:
 * ffmpeg's 1x2 4:4:4, 4:4:0, chroma that is not 1x1) are decoded by the same pc_jpeg_decode and cropped as on the board ("SIM thumb req=<n>
 * px=1 why=own"); PROGRESSIVE, UNSUPPORTED and TOO_BIG get the placeholder ("px=0 why=progressive|unsupported|too_big"). NATIVE pictures (the JPEG engine
 * and esp_new_jpeg on the board: no host build) and bytes that are no JPEG (the fixture's stand-ins) still get the drawn test cover.
 * Empty: bench, the JPEG lock (always free), the card cache's files.
 * hires T8e: the background pass is modelled with the firmware's rule (pc_tbg_rule on the fake audio service: rate, loaded, opening, underruns_src
 * rising = simunderrun / simdac fault; the scan, the card, the setting and the page's buttons): the "pack" is a set of track paths, full at boot
 * (the fixture's card has a complete cache: no picture changes), `simthumbpack clear` empties it and starts a pass; one cover is "built" per
 * breather (250 ms idle, 1 s loaded), `simthumbpack hold 1` freezes the building (pictures of the 建置中 state). thumb_svc_progress, `thumbcache stat`
 * (pass= why= done= total= at the end) and the 現況 fragment read it. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_svc.h"
#include "esp_heap_caps.h"
#include "library_svc.h"
#include "pc/pc_jpeg.h"
#include "pc/pc_thumb.h"
#include "pc/pc_thumbbg.h"
#include "cfg.h"
#include "esp_timer.h"
#include "sim.h"
#include "thumb_svc.h"
#include "bt_svc.h"
#include "ui.h"

#define THUMB_PX 240

static struct { bool active, waiting; uint32_t req; char path[256]; thumb_cb cb; void *u; bool mem; int size; } s_pend;   /* mem: path holds the test cover's key; size: 0 = 240 px, else a thumb_svc_request_big picture */
static int psz(void) { return s_pend.size ? s_pend.size : THUMB_PX; }
static uint32_t s_req;
static int s_low_n;
static uint32_t s_low_req;
void fake_thumb_low(int n) { s_low_n = n; }
bool thumb_svc_low_mem(uint32_t req) { return req && req == s_low_req; }

bool thumb_svc_paused(void)
{
    aud_info_t a;
    audio_svc_info(&a);
    return pc_thumb_hires_pause(a.src_rate);
}

static const pc_track_t *track_of(const char *path)
{
    for (int i = 0; i < g_lib.n; i++)
        if (!strcmp(pc_str(&g_lib, g_lib.tracks[i].path), path)) return &g_lib.tracks[i];
    return NULL;
}

/* the track's embedded picture by its route: 1 = decided (px set or NULL, why set), 0 = the drawn test cover */
static int routed(const pc_track_t *t, uint16_t **px, const char **why)
{
    FILE *f = fopen(s_pend.path, "rb");
    uint8_t *b = f ? malloc(t->apic_len) : NULL;
    bool ok = b && fseek(f, (long)t->apic_off, SEEK_SET) == 0 && fread(b, 1, t->apic_len, f) == t->apic_len;
    if (f) fclose(f);
    pc_jpeg_info_t in;
    int r = ok ? pc_jpeg_probe(b, t->apic_len, &in) : PC_JPEG_BAD, w, h;
    *px = NULL;
    *why = pc_jpeg_route_name(r);
    if (r == PC_JPEG_OWN) {
        uint16_t *full = NULL;
        if (pc_jpeg_decode(b, t->apic_len, pc_jpeg_shift_for(in.w, in.h, psz()), NULL, &full, &w, &h) == PC_JPEG_OK
            && (*px = heap_caps_malloc((size_t)psz() * psz() * sizeof **px, MALLOC_CAP_SPIRAM)))
            pc_thumb_crop_scale(full, w, h, *px, psz());
        free(full);
    }
    free(b);
    return r == PC_JPEG_OWN || r == PC_JPEG_PROGRESSIVE || r == PC_JPEG_UNSUPPORTED || r == PC_JPEG_TOO_BIG;
}

static void answer(const char *why)
{
    uint16_t *px = NULL;
    const pc_track_t *t = track_of(s_pend.path);
    const char *route = NULL, *name = NULL;
    if (t && t->apic_len && routed(t, &px, &name)) route = name;
    else if (t && t->apic_len && (px = heap_caps_malloc((size_t)psz() * psz() * sizeof *px, MALLOC_CAP_SPIRAM)))
        sim_cover_draw(pc_str(&g_lib, t->album), psz(), px);
    if (s_pend.size) printf("SIM thumb big req=%u size=%d px=%d\n", (unsigned)s_pend.req, s_pend.size, px != NULL);
    else if (route) printf("SIM thumb req=%u px=%d why=%s\n", (unsigned)s_pend.req, px != NULL, route);   /* a routed picture (hires 8d) */
    else if (strcmp(why, "cover")) printf("SIM thumb req=%u px=%d why=%s\n", (unsigned)s_pend.req, px != NULL, px ? why : "none");   /* the pause's answers only (exact-output tests) */
    if (s_pend.cb) s_pend.cb(s_pend.req, px, s_pend.u);
}

static void deliver(void *arg)
{
    (void)arg;
    if (!s_pend.active) return;
    s_pend.active = false;
    uint16_t *px = NULL;
    if (s_pend.mem) {                                          /* the phone's cover art: in memory, not paused (no card read) */
        if (s_low_n > 0) { s_low_n--; s_low_req = s_pend.req; printf("APP thumb skip reason=low_internal free=0\n"); }
        else if ((px = heap_caps_malloc((size_t)psz() * psz() * sizeof *px, MALLOC_CAP_SPIRAM))) sim_cover_draw(s_pend.path, psz(), px);
        if (s_pend.cb) s_pend.cb(s_pend.req, px, s_pend.u);
        return;
    }
    const pc_track_t *t = track_of(s_pend.path);
    if (s_pend.size && thumb_svc_paused()) {                   /* a larger cover is not made above 48 kHz and does not wait: NULL, the caller keeps the 240 px one */
        printf("SIM thumb big req=%u size=%d px=0 why=hires\n", (unsigned)s_pend.req, s_pend.size);
        if (s_pend.cb) s_pend.cb(s_pend.req, NULL, s_pend.u);
        return;
    }
    if (t && t->apic_len && thumb_svc_paused()) {              /* no cover decoded above 48 kHz: the placeholder now, the cover after */
        s_pend.waiting = true;
        printf("SIM thumb req=%u px=0 why=hires\n", (unsigned)s_pend.req);
        if (s_pend.cb) s_pend.cb(s_pend.req, NULL, s_pend.u);
        return;
    }
    answer("cover");
}

/* ---- hires T8e: the fake pass ---- */
static struct { char **path; int n, cap; bool seeded, run, hold, manual, requested, user, tidy; pc_tbg_under_t under; int64_t last_ms; int why; } s_bg;
static bool in_pack(const char *p) { for (int i = 0; i < s_bg.n; i++) if (!strcmp(s_bg.path[i], p)) return true; return false; }
static void pack_add(const char *p)
{
    if (s_bg.n == s_bg.cap) { int c = s_bg.cap ? 2 * s_bg.cap : 32; char **np = realloc(s_bg.path, sizeof *np * (size_t)c); if (!np) return; s_bg.path = np; s_bg.cap = c; }
    if ((s_bg.path[s_bg.n] = strdup(p))) s_bg.n++;
}
static void pack_clear(void) { for (int i = 0; i < s_bg.n; i++) free(s_bg.path[i]); free(s_bg.path); s_bg.path = NULL; s_bg.n = s_bg.cap = 0; }
static int bg_rule(bool *loaded)
{
    aud_info_t a;
    aud_status_t st;
    audio_svc_info(&a);
    audio_svc_status(&st);
    *loaded = st.state != AUD_IDLE;
    int64_t now = esp_timer_get_time() / 1000;
    /* fix 1: charged only while a step's window is open (the fake's step is instant: the window is the breather after it), as the board */
    bool playing = st.state == AUD_PLAYING || st.state == AUD_NEAR || st.state == AUD_NEXT;
    uint32_t count = a.underruns_src + bt_svc_sink_underflows();
    if (pc_tbg_under_check(&s_bg.under, playing ? st.gen : 0, count, st.pos_ms, now))
        printf("APP thumbcache underrun gen=%lu count=%lu holds=%lu\n", (unsigned long)st.gen, (unsigned long)count, (unsigned long)s_bg.under.holds);
    pc_tbg_in_t in = { .card = g_sd_ok, .scanning = library_scanning(), .manual = s_bg.manual, .requested = s_bg.requested, .user_paused = s_bg.user,
                       .loaded = *loaded, .src_rate = a.src_rate, .opening = pc_thumb_opening(audio_svc_live_gen(), st.gen),
                       .underrun_age_ms = pc_tbg_under_age(&s_bg.under, now), .underrun_holds = s_bg.under.holds, .space_known = true };
    return pc_tbg_rule(&in);
}
static void bg_step(void)
{
    if (!s_bg.run) return;
    bool loaded;
    int r = bg_rule(&loaded);
    if (r != s_bg.why && r) printf("APP thumbcache wait reason=%s\n", pc_tbg_why_name(r));
    s_bg.why = r;
    int64_t now = esp_timer_get_time() / 1000;
    if (now - s_bg.last_ms >= (int64_t)pc_tbg_breather_ms(loaded)) pc_tbg_under_disarm(&s_bg.under);   /* the last step's window is over (bg_rule checked it) */
    if (r || s_bg.hold || now - s_bg.last_ms < (int64_t)pc_tbg_breather_ms(loaded)) return;
    for (int i = 0; i < g_lib.n; i++) {
        const char *p = pc_str(&g_lib, g_lib.tracks[i].path);
        if (!g_lib.tracks[i].apic_len || in_pack(p)) continue;
        aud_info_t a;
        aud_status_t st;
        audio_svc_info(&a);
        audio_svc_status(&st);
        bool playing = st.state == AUD_PLAYING || st.state == AUD_NEAR || st.state == AUD_NEXT;
        pc_tbg_under_arm(&s_bg.under, playing ? st.gen : 0, a.underruns_src + bt_svc_sink_underflows());   /* fix 1: this step's baseline */
        pack_add(p);
        s_bg.last_ms = now;
        return;
    }
    s_bg.run = s_bg.requested = false;
    printf("APP thumbcache done tracks=%d\n", g_lib.n);
}
void thumb_svc_progress(pc_tbg_prog_t *o)
{
    uint32_t done = 0, total = 0;
    for (int i = 0; i < g_lib.n; i++) if (g_lib.tracks[i].apic_len) { total++; done += in_pack(pc_str(&g_lib, g_lib.tracks[i].path)); }
    bool running = s_bg.run && done < total;                 /* nothing missing: 完成, as the board */
    int st = pc_tbg_state(running, running ? s_bg.why : PC_TBG_RUN);
    *o = (pc_tbg_prog_t){ .state = (uint8_t)st, .why = (uint8_t)(st == PC_TBG_ST_HELD ? s_bg.why : PC_TBG_RUN), .done = done, .total = total, .manual = s_bg.manual, .user_paused = s_bg.user,
                          .tidy = !running && s_bg.tidy };
}
void thumb_svc_progress_want(void) {}                         /* the fake has no table to read */
void thumb_svc_set_auto(bool on) { s_bg.manual = !on; printf("APP thumbcache mode=%s\n", on ? "auto" : "manual"); bg_step(); }
void thumb_svc_user_pause(bool on) { s_bg.user = on; printf("APP thumbcache user_pause=%d\n", on); bg_step(); }
void thumb_svc_build_now(void) { printf("APP thumbcache build_now\n"); s_bg.requested = s_bg.run = true; s_bg.user = false; bg_step(); }
bool fake_thumb_pack(const char *a)
{
    if (!strcmp(a, "clear")) { pack_clear(); s_bg.run = true; s_bg.last_ms = esp_timer_get_time() / 1000; return true; }
    if (!strcmp(a, "hold 1") || !strcmp(a, "hold 0")) { s_bg.hold = a[5] == '1'; return true; }
    if (!strncmp(a, "fill ", 5)) {                           /* a fixed state, independent of the virtual-time phase: the pack holds the first n covers, the pass runs and is held (pictures of 建置中) */
        int want = atoi(a + 5);
        pack_clear();
        for (int i = 0; i < g_lib.n && s_bg.n < want; i++) if (g_lib.tracks[i].apic_len) pack_add(pc_str(&g_lib, g_lib.tracks[i].path));
        s_bg.run = s_bg.hold = true; s_bg.user = false; s_bg.last_ms = esp_timer_get_time() / 1000;
        return true;
    }
    if (!strcmp(a, "tidy 1") || !strcmp(a, "tidy 0")) { s_bg.tidy = a[5] == '1'; return true; }   /* fix 0: a compaction waits for a stop (the fake has none of its own) */
    if (!strcmp(a, "full")) {                               /* back to the fixture's state (after pictures): every cover in, no pass, not held or paused */
        pack_clear();
        for (int i = 0; i < g_lib.n; i++) if (g_lib.tracks[i].apic_len) pack_add(pc_str(&g_lib, g_lib.tracks[i].path));
        s_bg.run = s_bg.hold = s_bg.user = s_bg.requested = s_bg.tidy = false;
        return true;
    }
    return false;
}
static void bg_free(void) { pack_clear(); }

static void wait_tick(lv_timer_t *t)
{
    (void)t;
    bg_step();
    if (!s_pend.waiting || s_pend.active || thumb_svc_paused()) return;
    s_pend.waiting = false;
    answer("after");
}

void thumb_svc_start(void) { s_bg.manual = !cfg_int("cover_auto", 1); lv_timer_create(wait_tick, 250, NULL); atexit(bg_free); }
bool thumb_svc_busy(void) { return false; }
void thumb_svc_bench(void) {}
uint32_t thumb_svc_request(const char *path, uint32_t apic_off, uint32_t apic_len, thumb_cb cb, void *u)
{
    (void)apic_off; (void)apic_len;
    s_pend.active = true;                        /* an undelivered older request is superseded */
    s_pend.waiting = false;
    s_pend.req = ++s_req;
    snprintf(s_pend.path, sizeof s_pend.path, "%s", path ? path : "");
    s_pend.cb = cb;
    s_pend.u = u;
    s_pend.mem = false;
    s_pend.size = 0;
    ui_async(deliver, NULL);
    return s_pend.req;
}
uint32_t thumb_svc_request_big(const char *path, uint32_t apic_off, uint32_t apic_len, int size, thumb_cb cb, void *u)
{
    if (size < THUMB_PX || size > THUMB_BIG_MAX) return 0;
    uint32_t r = thumb_svc_request(path, apic_off, apic_len, cb, u);
    s_pend.size = size;
    return r;
}
uint32_t thumb_svc_request_jpeg(const uint8_t *jpg, uint32_t len, thumb_cb cb, void *u)
{
    if (!jpg || !len) return 0;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) h = (h ^ jpg[i]) * 16777619u;
    s_pend.active = true;
    s_pend.waiting = false;                      /* a hi-res wait for a library cover is superseded too */
    s_pend.req = ++s_req;
    snprintf(s_pend.path, sizeof s_pend.path, "bt %08x", (unsigned)h);
    s_pend.cb = cb;
    s_pend.u = u;
    s_pend.mem = true;
    s_pend.size = 0;
    ui_async(deliver, NULL);
    return s_pend.req;
}
uint32_t thumb_svc_request_jpeg_big(const uint8_t *jpg, uint32_t len, int size, thumb_cb cb, void *u)
{
    if (size < THUMB_PX || size > THUMB_BIG_MAX) return 0;
    uint32_t r = thumb_svc_request_jpeg(jpg, len, cb, u);
    if (r) s_pend.size = size;
    return r;
}
uint32_t thumb_svc_request_now(const char *path, uint32_t apic_off, uint32_t apic_len, thumb_cb cb, void *u) { return thumb_svc_request(path, apic_off, apic_len, cb, u); }
bool thumb_jpeg_lock(int timeout_ms) { (void)timeout_ms; return true; }
void thumb_jpeg_unlock(void) {}
void thumb_svc_library_changed(void)
{
    if (!s_bg.seeded) {                                     /* the first library: the fixture's card has every cover in its pack */
        s_bg.seeded = true;
        for (int i = 0; i < g_lib.n; i++) if (g_lib.tracks[i].apic_len) pack_add(pc_str(&g_lib, g_lib.tracks[i].path));
    }
    s_bg.run = true;                                        /* a pass over the new library (passes over what is there) */
    printf("APP thumbcache pass tracks=%d\n", g_lib.n);
}
/* simthumbgate: the write gate a cover would meet now (the playing track's request: not background), from the fake audio service's live and
 * published generations through the same pure rules as thumb_svc.c */
void fake_thumb_gate(void)
{
    aud_status_t st;
    audio_svc_status(&st);
    uint32_t live = audio_svc_live_gen();
    bool opening = pc_thumb_opening(live, st.gen);
    const char *g = pc_thumb_write_gate(thumb_svc_paused(), opening, false, true, true);
    printf("SIM thumbgate opening=%d write=%s live=%lu pub=%lu\n", opening, g ? g : "ok", (unsigned long)live, (unsigned long)st.gen);
}
void thumb_svc_cache_stat(void)
{
    int covers = 0;
    for (int i = 0; i < g_lib.n; i++) covers += g_lib.tracks[i].apic_len != 0;
    pc_tbg_prog_t p;
    thumb_svc_progress(&p);
    static const char *const ST[4] = { "done", "building", "held", "manual" };
    printf("APP thumbcache stat count=0 bytes=0 cap=0 aliases=0 bad=0 missing=%d none=%d unresolved=0 tracks=%d building=%d compacting=0 wait=%s stopped=- hits=0 free_mb=-1 margin_mb=64 int_free=0 int_big=0 jpeghw=0"
           " pass=%s why=%s done=%lu total=%lu mode=%s user=%d underrun_holds=%lu unchecked=0 tidy=%d\n", covers - (int)p.done, g_lib.n - covers, g_lib.n, s_bg.run,
           thumb_svc_paused() ? "hires" : s_bg.run && s_bg.why ? pc_tbg_why_name(s_bg.why) : "-", ST[p.state & 3], pc_tbg_why_name(p.why), (unsigned long)p.done, (unsigned long)p.total,
           s_bg.manual ? "manual" : "auto", s_bg.user, (unsigned long)s_bg.under.holds, p.tidy);
}
void thumb_svc_cache_build(void)
{
    aud_info_t a;
    audio_svc_info(&a);
    if (pc_thumb_hires_pause(a.src_rate)) { printf("APP thumbcache build refused reason=hires src=%lu\n", (unsigned long)a.src_rate); return; }
    printf("APP thumbcache build tracks=%d\n", g_lib.n);
    s_bg.requested = s_bg.run = true;                       /* hires T8e: a pass in either mode */
}
