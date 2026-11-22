// SPDX-License-Identifier: LGPL-2.1-or-later
/* Speaker mode: the phone's track (AVRCP 1.6 CT metadata and cover art). Drives the pure core (pc_bma_*) and keeps what the UI shows; LVGL task only
 * (bt_meta_attach excepted). Kept in PSRAM: the app's .bss is mapped there (linker.lf) and this file has no small-data section (CMakeLists.txt). */
#include "bt_meta.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "font.h"
#include "screens.h"
#include "ui.h"

#define TICK_MS 50                      /* only while the core is busy (an answer awaited or coming, a cover downloading); events wake it */
#define DECODE_RETRY_MS 5000            /* a cover the decoder refused for low internal memory: tried once more after this */

static pc_bma_t s_sm;                   /* the core */
static bool s_sm_on;
static const bt_meta_phone_t *s_phone;
static lv_timer_t *s_tick, *s_retry;
static pc_btmeta_t s_m;                 /* what the page shows (the core's last publish) */
static uint8_t *s_jpg;                  /* the cover of s_art_hdl (PSRAM) */
static uint32_t s_jpg_len;
static char s_art_hdl[PC_BTMETA_HANDLE + 1];
static volatile bool s_asleep;          /* the tick is paused (read by the glue on other tasks) */
static bool s_low, s_retried;           /* the decoder refused this cover for low internal memory; the one retry was used */
static now_ops_t *s_ops;

static void *grow(void *p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
static void drop(void *p) { heap_caps_free(p); }

/* ---- what the page shows ---- */
static bool known(void) { return pc_btmeta_known(&s_m) && strcmp(s_m.title, PC_BTMETA_WAIT); }
static bool shown(void) { return pc_btmeta_known(&s_m); }   /* a title or the wait mark: the track layout */
static void cover_done(bool ok, bool low_mem);
static const char *cover_note(void);
static void ops_follow(void)
{
    if (!s_ops) return;
    s_ops->no_art = !shown();                           /* no title: the phone's name in the middle, as before AVRCP */
    s_ops->cover_jpeg = shown() ? bt_meta_art : NULL;   /* a title without a cover: the page's default record */
    s_ops->cover_done = shown() ? cover_done : NULL;
    s_ops->cover_note = shown() ? cover_note : NULL;
    s_ops->open_info = known() ? bt_meta_open_info : NULL;
}
static void shown_changed(void)
{
    ops_follow();
    if (s_ops && player_ctl_now() == s_ops) player_ctl_changed();
}

void bt_meta_bind(now_ops_t *ops) { s_ops = ops; ops_follow(); }

static void set_meta(const pc_btmeta_t *m)
{
    if (!memcmp(&s_m, m, sizeof s_m)) return;
    bool band = strcmp(s_m.title, m->title) || strcmp(s_m.artist, m->artist);   /* the title band and the layout come from these two */
    bool art_before = bt_meta_art(NULL, NULL);
    s_m = *m;
    if (!m->title[0] && !m->artist[0] && !m->album[0] && !m->art[0]) printf("APP bt meta clear\n");
    else printf("APP bt meta title=%s artist=%s album=%s art=%s\n", s_m.title, s_m.artist, s_m.album, s_m.art[0] ? s_m.art : "-");
    if (band || art_before != bt_meta_art(NULL, NULL)) shown_changed();   /* the album, track and length show only on 詳細 (read when it opens) */
}

static void set_art(uint8_t *jpg, uint32_t len, const char *handle)
{
    bool before = bt_meta_art(NULL, NULL);
    if (s_jpg) heap_caps_free(s_jpg);
    s_jpg = jpg;
    s_jpg_len = jpg ? len : 0;
    snprintf(s_art_hdl, sizeof s_art_hdl, "%s", jpg && handle ? handle : "");
    s_low = s_retried = false;
    if (s_retry) lv_timer_pause(s_retry);
    if (before || bt_meta_art(NULL, NULL)) shown_changed();   /* a new cover always redraws */
}

/* ---- the decoder's answer (scr_now's thumb_ready): honest state when it refused for low internal memory ---- */
static void retry_cb(lv_timer_t *t)
{
    lv_timer_pause(t);
    if (!s_low || !bt_meta_art(NULL, NULL)) return;
    printf("APP bt art decode retry\n");
    shown_changed();                                    /* the page asks the decoder again */
}
static void cover_done(bool ok, bool low_mem)
{
    if (!bt_meta_art(NULL, NULL)) return;               /* the placeholder record was asked for, not a cover */
    if (ok) { s_low = false; return; }
    if (!low_mem) return;                               /* a broken JPEG: the record, the log says why (APP thumb fail) */
    if (!s_low) printf("APP bt art shown=0 reason=low_internal\n");
    s_low = true;
    if (s_retried) return;                              /* once per cover; the next track tries again */
    s_retried = true;
    if (s_retry) { lv_timer_reset(s_retry); lv_timer_resume(s_retry); }
}
static const char *cover_note(void)
{
    return s_low && bt_meta_art(NULL, NULL) ? T(S_COVER_UNAVAILABLE_LOW_MEMORY) /* 封面暫時無法顯示（記憶體不足） */ : NULL;
}

/* ---- the core ---- */
static void sm_on(void)
{
    if (s_sm_on) return;
    pc_bma_init(&s_sm, grow, drop);
    s_sm_on = true;
}

static void run(const pc_bma_out_t *o)
{
    if (o->act & PC_BMA_DO_META) set_meta(o->meta);
    if (o->act & PC_BMA_DO_ART_CLEAR) set_art(NULL, 0, NULL);
    if (o->act & PC_BMA_DO_ART) {
        printf("APP bt art bytes=%lu ms=%lu\n", (unsigned long)o->art_len, (unsigned long)o->art_ms);
        set_art(o->art, o->art_len, o->art_hdl);
    }
    if (o->act & PC_BMA_DO_FAIL) printf("APP bt art fail reason=%s\n", o->fail);
    if (!s_phone) return;
    if (o->act & PC_BMA_DO_CAPS) s_phone->ask_caps();
    if (o->act & PC_BMA_DO_REG) s_phone->reg_track();
    if (o->act & PC_BMA_DO_ASK) s_phone->ask_meta(o->ask_mask);
    if (o->act & PC_BMA_DO_CA_DROP) s_phone->ca_drop();
    if (o->act & PC_BMA_DO_CA_CONNECT) s_phone->ca_connect();
    if (o->act & PC_BMA_DO_GET) s_phone->get(o->get);
}

void bt_meta_event(pc_bma_ev_t *ev)
{
    sm_on();
    ev->now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    pc_bma_out_t o;
    pc_bma_event(&s_sm, ev, &o);
    run(&o);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_phone && s_phone->poll) s_phone->poll();
    pc_bma_ev_t e = { .kind = PC_BMA_TICK };
    bt_meta_event(&e);
    if (!pc_bma_busy(&s_sm) && s_tick) { lv_timer_pause(s_tick); s_asleep = true; }   /* nothing open (idle-linked, no phone): asleep until the next event (bt_meta_wake) */
}
void bt_meta_start(void)
{
    sm_on();
    s_tick = lv_timer_create(tick_cb, TICK_MS, NULL);   /* both live as long as the app (made at boot: no page sees them appear) */
    s_retry = lv_timer_create(retry_cb, DECODE_RETRY_MS, NULL);
    if (s_tick) { lv_timer_pause(s_tick); s_asleep = true; }
    if (s_retry) lv_timer_pause(s_retry);
}
static void meta_run(void *arg) { (void)arg; if (s_tick) { lv_timer_resume(s_tick); s_asleep = false; } }
bool bt_meta_asleep(void) { return s_asleep; }
void bt_meta_wake(void) { ui_async(meta_run, NULL); }
void bt_meta_attach(const bt_meta_phone_t *p) { s_phone = p; }

/* ---- readers ---- */
const pc_btmeta_t *bt_meta_get(void) { return known() ? &s_m : NULL; }

bool bt_meta_art(const uint8_t **jpg, uint32_t *len)
{
    bool ok = s_jpg && s_art_hdl[0] && !strcmp(s_art_hdl, s_m.art);   /* only the cover of the handle the track names now */
    if (ok && jpg) *jpg = s_jpg;
    if (ok && len) *len = s_jpg_len;
    return ok;
}

bool bt_meta_title(char *out, size_t cap)
{
    if (!shown()) return false;
    pc_btmeta_line(&s_m, out, cap);
    return true;
}

void bt_meta_open_info(void) { if (known()) ui_open_once(&scr_meta, (void *)(intptr_t)-1); }

void bt_meta_print(void)
{
    const char *st = s_low && bt_meta_art(NULL, NULL) ? "no_mem" : s_sm_on && s_sm.art_state ? s_sm.art_state : "none";
    printf("APP btmeta title=%s artist=%s album=%s track=%u/%u ms=%lu art=%s art_bytes=%lu art_state=%s%s%s\n", s_m.title, s_m.artist, s_m.album,
           (unsigned)s_m.track, (unsigned)s_m.total, (unsigned long)s_m.play_ms, s_m.art[0] ? s_m.art : "-",
           (unsigned long)(bt_meta_art(NULL, NULL) ? s_jpg_len : 0), st, !strcmp(st, "fail") && s_sm.last_fail ? ":" : "",
           !strcmp(st, "fail") && s_sm.last_fail ? s_sm.last_fail : "");
}
