// SPDX-License-Identifier: LGPL-2.1-or-later
/* Clock service: SNTP, the alarm (one, built-in chime or one MP3), pomodoro. Checks once a second on an LVGL timer. */
#include "clock_svc.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "time/clock.h"
#include "time/tz.h"
#include "audio_svc.h"
#include "bsp/bsp_haptic.h"
#include "bsp/bsp_rtc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifndef SIM
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
#endif
#include "cfg.h"
#include "drink_svc.h"
#include "player_ctl.h"
#include "pc/pc_wifihold.h"
#include "wifi_svc.h"
#include "ui/font.h"
#include "ui/screens.h"
#include "ui/ui.h"

extern const uint8_t alarm_mp3_start[] asm("_binary_alarm_mp3_start");
extern const uint8_t alarm_mp3_end[] asm("_binary_alarm_mp3_end");
extern const uint8_t chime_mp3_start[] asm("_binary_chime_mp3_start");
extern const uint8_t chime_mp3_end[] asm("_binary_chime_mp3_end");

static pc_alarm_t s_alarm;
static pc_pomo_t s_pomo;
static bool s_sntp, s_ringing, s_testing;
static int64_t s_prev_utc, s_ring_started;
static uint32_t s_gen;
static char s_sound[160];
static pc_reader_t s_rd;
static FILE *s_file;
static const now_ops_t s_ops;

bool clock_time_valid(void) { return time(NULL) > 1735689600; }   /* after 2025-01-01: synced */
int64_t clock_now_utc(void) { return (int64_t)time(NULL); }

/* ---- the time zone (cfg "tz", a POSIX TZ string) and the sunrise location (cfg sun_lat / sun_lon, 0.01 deg; absent = none) ----
 * The zone is set only on the web page. The parse is cached: the stored string (shorter than 64 bytes and valid, else UTC+8) is parsed again only when it changed. */
EXT_RAM_BSS_ATTR static char s_tz_src[64], s_tz_eff[64];
EXT_RAM_BSS_ATTR static pc_tz_t s_tz;
static const pc_tz_t *tz_cached(void)
{
    const char *s = cfg_has("tz") ? cfg_str("tz", "") : "";
    if (strlen(s) >= sizeof s_tz_src) s = "";
    if (!s_tz_eff[0] || strcmp(s, s_tz_src)) {
        snprintf(s_tz_src, sizeof s_tz_src, "%s", s);
        if (pc_tz_parse(s_tz_src, &s_tz)) snprintf(s_tz_eff, sizeof s_tz_eff, "%s", s_tz_src);
        else { snprintf(s_tz_eff, sizeof s_tz_eff, "%s", pc_tz_default()); pc_tz_parse(s_tz_eff, &s_tz); }
    }
    return &s_tz;
}
const char *clock_tz_str(void) { tz_cached(); return s_tz_eff; }
int32_t clock_tz_min_at(int64_t utc) { return clock_clamp_tz_min(pc_tz_offset_min(tz_cached(), utc)); }
int32_t clock_tz_min(void) { return clock_tz_min_at(clock_now_utc()); }
int32_t clock_tz_day_min(int y, int m, int d) { return clock_tz_min_at(clock_days_from_civil(y, m, d) * 86400 + 12 * 3600 - (int64_t)clock_tz_min() * 60); }   /* the offset in force at noon of that local day */
bool clock_tz_valid(const char *posix)
{
    pc_tz_t z;
    return posix && strlen(posix) < sizeof s_tz_src && pc_tz_parse(posix, &z);
}
bool clock_tz_set(const char *posix)
{
    if (!clock_tz_valid(posix)) return false;
    cfg_set_str("tz", posix);
    return true;
}
void clock_zone_label(char *out, size_t n)
{
    char b[16];
    clock_tz_label(clock_tz_min(), b, sizeof b);
    bool dst = pc_tz_is_dst(tz_cached(), clock_now_utc());
    snprintf(out, n, "%s%s%s", b, dst ? " " : "", dst ? T(S_DST) /* 夏令 */ : "");
}
bool clock_sun_has(void) { return cfg_has("sun_lat") && cfg_has("sun_lon"); }
int32_t clock_sun_lat_e4(void) { return cfg_int("sun_lat", 0) * 100; }
int32_t clock_sun_lon_e4(void) { return cfg_int("sun_lon", 0) * 100; }
void clock_sun_set(int lat_c, int lon_c) { cfg_set_int("sun_lat", lat_c); cfg_set_int("sun_lon", lon_c); }
void clock_sun_clear(void) { cfg_del("sun_lat"); cfg_del("sun_lon"); }

/* ---- the battery clock (FakePod PCF85063, bsp_rtc.h; the round board has none: every call answers absent) ----
 * Boot: clock_svc_start reads it and, when it is valid (oscillator-stop flag clear, fields in range, year >= 2025) and the system clock is not set yet, sets the system time from it.
 * That is the same state as a manual set (time(NULL) is what alarms, faces and drink_svc read), NOT an SNTP sync: s_synced stays false, so nothing waits on or reports a sync.
 * Writes: after every SNTP sync and every clock_set_manual. The I2C transfer runs in a one-shot task (a stuck bus must not stall the LVGL task); the task writes the time of the moment it runs. */
EXT_RAM_BSS_ATTR static volatile bool s_rtc_ok;                               /* the chip is there and holds a trustworthy time (valid at boot, or the last write was read back) */
EXT_RAM_BSS_ATTR static volatile bool s_rtc_dirty, s_rtc_busy;   /* PSRAM: no new internal static data */
bool clock_rtc_keeps(void) { return s_rtc_ok; }
static bool rtc_write_now(void)
{
    time_t t = time(NULL);
    struct tm g;
    gmtime_r(&t, &g);
    esp_err_t e = bsp_rtc_set(&g);
    s_rtc_ok = e == ESP_OK;
    printf("APP rtc write %04d-%02d-%02dT%02d:%02d:%02dZ rc=%d\n", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec, (int)e);
    return e == ESP_OK;
}
static void rtc_task(void *arg)
{
    (void)arg;
    do {
        while (s_rtc_dirty) { s_rtc_dirty = false; rtc_write_now(); }
        s_rtc_busy = false;
    } while (s_rtc_dirty && !__atomic_exchange_n(&s_rtc_busy, true, __ATOMIC_ACQ_REL));   /* a request that came in while the task was finishing */
#ifdef SIM
    vTaskDelete(NULL);
#else
    vTaskDeleteWithCaps(NULL);
#endif
}
static void rtc_write_async(void)
{
    if (!bsp_rtc_present()) return;
    s_rtc_dirty = true;
    if (__atomic_exchange_n(&s_rtc_busy, true, __ATOMIC_ACQ_REL)) return;   /* the running task picks it up */
#ifdef SIM
    if (xTaskCreatePinnedToCore(rtc_task, "rtc", 3072, NULL, 2, NULL, 0) != pdPASS) s_rtc_busy = false;
#else
    if (xTaskCreatePinnedToCoreWithCaps(rtc_task, "rtc", 3072, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM) != pdPASS) s_rtc_busy = false;   /* PSRAM stack: a few I2C transfers and a printf */
#endif
}
bool clock_rtc_write_now(void) { return bsp_rtc_present() && rtc_write_now(); }   /* console `rtc set` (its own task) */
static void rtc_boot(void)
{
    bool valid = false, os = false;
    struct tm g = { 0 };
    const bool present = bsp_rtc_present();
    if (present) { bsp_rtc_status(&os, &g); bsp_rtc_get(&g, &valid); }
    if (valid && g.tm_year + 1900 < 2025) valid = false;     /* a chip set to some earlier date: not a time to adopt */
    char iso[24] = "-";
    if (valid) snprintf(iso, sizeof iso, "%04d-%02d-%02dT%02d:%02d:%02dZ", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);
    printf("APP rtc present=%d valid=%d os=%d time=%s\n", present, valid, os, iso);
    s_rtc_ok = present && valid;
    if (valid && !clock_time_valid()) {
        const int64_t utc = clock_days_from_civil(g.tm_year + 1900, g.tm_mon + 1, g.tm_mday) * 86400 + g.tm_hour * 3600 + g.tm_min * 60 + g.tm_sec;
        struct timeval tv = { .tv_sec = (time_t)utc, .tv_usec = 0 };
        if (settimeofday(&tv, NULL) == 0) printf("APP clock from rtc utc=%lld\n", (long long)utc);
    }
}

/* Manual time (settings / clock menu "Set time"): the owner types LOCAL time. The same state follows as after an SNTP sync: time(NULL) is what everyone reads
 * (alarm tick, faces, pomodoro uses the monotonic clock and is untouched, drink_svc reads clock_now_utc); the alarm's previous-tick memory is dropped so a jump
 * neither rings nor skips an alarm by itself (the tick also ignores jumps over 120 s). */
bool clock_set_manual(int y, int mo, int d, int h, int mi)
{
    if (y < 2025 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > clock_days_in_month(y, mo) || h < 0 || h > 23 || mi < 0 || mi > 59) return false;
    const int64_t local = clock_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60;
    const int64_t utc = pc_tz_local_to_utc(tz_cached(), local);
    struct timeval tv = { .tv_sec = (time_t)utc, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) return false;
    s_prev_utc = 0;
    rtc_write_async();
    printf("APP clock set manual %04d-%02d-%02d %02d:%02d utc=%lld\n", y, mo, d, h, mi, (long long)utc);
    return true;
}

/* A sync holds Wi-Fi (PC_WH_CLOCK) until SNTP reports the time, or 60 s; then the radio goes 60 s later (wifi_svc grace).
 * The sync callback runs on the lwIP task: it only sets a flag, the once-a-second tick (LVGL task) lets go. */
#define SYNC_HOLD_MS 60000
static volatile bool s_synced;
static bool s_sync_hold, s_sync_kicked;
static int64_t s_sync_t0_ms, s_sync_end_ms;                /* start of the running hold; end of the last one */
#define SYNC_RETRY_MS 300000                                 /* an alarm still without the time asks again this long after a hold ended */
EXT_RAM_BSS_ATTR static volatile bool s_rtc_after_sync;                       /* set on the lwIP task, taken by the tick (the RTC write is not made from there) */
static void on_sync(struct timeval *tv) { (void)tv; s_synced = true; s_rtc_after_sync = true; }
static void sntp_up(void)
{
    if (s_sntp) { esp_sntp_restart(); return; }
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    c.sync_cb = on_sync;
    s_sntp = esp_netif_sntp_init(&c) == ESP_OK;
    printf("APP clock sntp start=%d\n", s_sntp);
}
static void sync_begin(void)
{
    if (wifi_svc_hold(PC_WH_CLOCK) != 0) { s_sync_end_ms = esp_timer_get_time() / 1000; return; }                    /* refused (broadcast mode): nothing to wait for */
    s_sync_hold = true; s_sync_kicked = false; s_synced = false;
    s_sync_t0_ms = esp_timer_get_time() / 1000;
}
static void sync_tick(void)
{
    if (!s_sync_hold) return;
    if (wifi_svc_connected() && !s_sync_kicked) { s_sync_kicked = true; sntp_up(); }   /* a fresh request after every (re)connect */
    if (s_synced || esp_timer_get_time() / 1000 - s_sync_t0_ms >= SYNC_HOLD_MS) {
        printf("APP clock sync hold end synced=%d\n", s_synced);
        s_sync_hold = false;
        s_sync_end_ms = esp_timer_get_time() / 1000;
        wifi_svc_release(PC_WH_CLOCK);
    }
}
void clock_sync_now(void) { sync_begin(); if (wifi_svc_connected()) { s_sync_kicked = true; sntp_up(); } }

pc_alarm_t *clock_alarm(void) { return &s_alarm; }
void clock_alarm_saved(void)
{
    pc_alarm_edited(&s_alarm);                     /* any edit (off, time, days) cancels a pending snooze */
    cfg_set_int("alarm_on", s_alarm.on);
    cfg_set_int("alarm_min", s_alarm.min);
    cfg_set_int("alarm_days", s_alarm.days);
}
const char *clock_alarm_sound(void) { return s_sound; }
void clock_alarm_set_sound(const char *path) { snprintf(s_sound, sizeof s_sound, "%s", path ? path : ""); cfg_set_str("alarm_sound", s_sound); printf("APP clock alarm sound=%s\n", s_sound); }

/* ---- the ring: built-in chime from flash, or the chosen MP3 (built-in when it is missing) ---- */
static void close_file(void *arg) { if (arg) fclose(arg); }
static bool start_sound(void)
{
    s_file = s_sound[0] ? fopen(s_sound, "rb") : NULL;
    if (s_file) setvbuf(s_file, NULL, _IONBF, 0);
    if (s_file && pc_reader_file(&s_rd, s_file)) { fclose(s_file); s_file = NULL; }
    if (!s_file) pc_reader_mem(&s_rd, alarm_mp3_start, (uint32_t)((uintptr_t)alarm_mp3_end - (uintptr_t)alarm_mp3_start));   /* never silent */
    s_gen = audio_svc_play(&pc_backend_mp3, &s_rd, 0, 0, close_file, s_file);
    printf("APP clock ring sound=%s gen=%lu\n", s_file ? s_sound : "built-in", (unsigned long)s_gen);
    return s_gen != 0;
}
static void forget(void) { s_ringing = s_testing = false; }
static void on_event(const aud_event_t *e)
{
    if (e->gen != s_gen || !(s_ringing || s_testing)) return;
    if (e->type == AUD_ENDED && s_ringing) start_sound();          /* loop until stopped */
    else if (e->type == AUD_ERROR && s_file) { s_sound[0] = 0; start_sound(); }   /* a bad file: the built-in chime */
    else if (s_testing) { s_testing = false; player_ctl_release(); player_ctl_changed(); }
}
static void ring_volume(void)                                       /* 20 % up to the set volume over 30 s */
{
    int full = app_volume();
    int64_t t = esp_timer_get_time() - s_ring_started;
    int v = t >= 30000000 ? full : full / 5 + (int)((int64_t)(full - full / 5) * t / 30000000);
    audio_svc_set_volume(v);
}
static void ring_start(void)
{
    s_ringing = true;
    s_ring_started = esp_timer_get_time();
    player_ctl_take(&s_ops, forget, on_event);
    audio_svc_set_volume(app_volume() / 5);
    start_sound();
    ui_wake();
    if (ui_top() != &scr_ring) ui_push(&scr_ring, NULL);
    if (ui_top() != &scr_ring) { ui_home(); ui_push(&scr_ring, NULL); }   /* the push was refused (a full stack): the ringing alarm always gets its page */
    player_ctl_changed();
}
static void ring_end(void)
{
    s_ringing = s_testing = false;
    audio_svc_stop(false);
    audio_svc_set_volume(app_volume());
    player_ctl_release();
    player_ctl_changed();
}
bool clock_ringing(void) { return s_ringing; }
void clock_ring_stop(void) { if (s_ringing) { printf("APP clock ring stop\n"); ring_end(); } }
void clock_ring_snooze(void) { if (s_ringing) { pc_alarm_snooze(&s_alarm, clock_now_utc(), 9); printf("APP clock snooze 9 min\n"); ring_end(); } }
void clock_alarm_test(bool on)
{
    if (!on) { if (s_testing) ring_end(); return; }
    printf("APP clock alarm test on\n");
    s_testing = true;
    s_ring_started = esp_timer_get_time();
    player_ctl_take(&s_ops, forget, on_event);
    audio_svc_set_volume(app_volume());
    start_sound();
    player_ctl_changed();
}

/* ---- pomodoro ---- */
static int64_t mono_s(void) { return esp_timer_get_time() / 1000000; }
pc_pomo_t *clock_pomo(void) { return &s_pomo; }
void clock_pomo_start(void) { pc_pomo_start(&s_pomo, mono_s()); }
void clock_pomo_stop(void) { pc_pomo_stop(&s_pomo); }
void clock_pomo_pause(void) { pc_pomo_pause(&s_pomo, mono_s()); }
void clock_pomo_resume(void) { pc_pomo_resume(&s_pomo, mono_s()); }
bool clock_pomo_chiming(void)                                       /* a phase began less than 5 s ago (its chime): no static, from the timer itself */
{
    if (s_pomo.phase == PC_POMO_IDLE || s_pomo.paused) return false;
    int total = s_pomo.phase == PC_POMO_FOCUS ? s_pomo.focus_s : s_pomo.phase == PC_POMO_LONG ? s_pomo.long_s : s_pomo.break_s;
    return total - pc_pomo_left(&s_pomo, esp_timer_get_time() / 1000000) < 5;
}
static void chime(void)
{
    bsp_haptic_play(47);                                            /* a firm buzz, also when music plays */
    const now_ops_t *o = player_ctl_now();
    if (o && o->playing && o->playing()) return;                    /* never cut the user's music for a chime */
    static pc_reader_t r;
    pc_reader_mem(&r, chime_mp3_start, (uint32_t)((uintptr_t)chime_mp3_end - (uintptr_t)chime_mp3_start));
    audio_svc_play(&pc_backend_mp3, &r, 0, 0, NULL, NULL);
}

/* ---- the once-a-second check ---- */
static void tick(lv_timer_t *t)
{
    (void)t;
    if (wifi_svc_connected() && !s_sntp) sntp_up();
    sync_tick();
    if (s_rtc_after_sync) { s_rtc_after_sync = false; rtc_write_async(); }
    if (s_alarm.on && !s_sync_hold && !clock_time_valid() && esp_timer_get_time() / 1000 - s_sync_end_ms >= SYNC_RETRY_MS) sync_begin();   /* no RTC: the alarm needs the time of this boot */
    int64_t now = clock_now_utc();
    if (clock_time_valid()) {
        drink_svc_tick();                                       /* 喝水提醒 (ideas G7): the day, the next reminder */
        if (s_prev_utc && now >= s_prev_utc && now - s_prev_utc < 120 && !s_ringing && pc_alarm_check_tz(&s_alarm, s_prev_utc, now, tz_cached())) {
            printf("APP clock alarm %02d:%02d\n", s_alarm.min / 60, s_alarm.min % 60);
            if (!s_alarm.on) clock_alarm_saved();                   /* a once-alarm switched itself off */
            ring_start();
        }
        s_prev_utc = now;
    }
    if (s_ringing) {
        ring_volume();
        if (esp_timer_get_time() - s_ring_started > 10LL * 60 * 1000000) { printf("APP clock ring timeout\n"); ring_end(); }
    }
    if (pc_pomo_tick(&s_pomo, mono_s())) { printf("APP clock pomo phase=%d done=%d\n", s_pomo.phase, s_pomo.done); chime(); }
}

static void title(char *out, size_t cap) { snprintf(out, cap, "%s", s_testing ? T(S_ALARM_SOUND) /* 試聽鈴聲 */ : T(S_ALARM) /* 鬧鐘 */); }
static bool playing(void) { return s_ringing || s_testing; }
static void stop(void) { ring_end(); }
static const now_ops_t s_ops = { .title = title, .stop = stop, .playing = playing, .no_art = true };

void clock_svc_start(void)
{
    s_alarm = (pc_alarm_t){ .on = cfg_int("alarm_on", 0), .min = cfg_int("alarm_min", 7 * 60), .days = (uint8_t)cfg_int("alarm_days", PC_DAYS_WEEKDAYS) };
    snprintf(s_sound, sizeof s_sound, "%s", cfg_str("alarm_sound", ""));
    rtc_boot();                                               /* the I2C bus is up (app_main: bsp_i2c_init before power_svc_start); before the alarm check and drink_svc read the time */
    drink_svc_start();                                        /* after gamesave_start */
    if (s_alarm.on) sync_begin();                             /* an alarm needs the time: sync once at boot */
    lv_timer_create(tick, 1000, NULL);
}
