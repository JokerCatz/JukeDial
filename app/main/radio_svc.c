// SPDX-License-Identifier: LGPL-2.1-or-later
/* Network radio: a core-0 task reads HTTP (ICY metadata removed) into a PSRAM ring; the audio task decodes
 * it through pc_backend_stream (MP3 or AAC). The task owns the connection; LVGL owns the station choice. */
#include "radio_svc.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "pc/pc_radio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pc/pc_hls.h"
#include "pc/pc_icy.h"
#include "pc/pc_ts.h"
#include "pc/pc_stream.h"
#include "audio_svc.h"
#include "backend_sdec.h"
#include "bt_svc.h"
#include "cfg.h"
#include "player_ctl.h"
#include "pc/pc_wifihold.h"
#include "wifi_svc.h"
#include "ui/font.h"
#include "ui/i18n.h"
#include "ui/screens.h"
#include "ui/ui.h"

#define RING_SIZE (256 * 1024)
#define UA "JCPlayer/1.0 (ESP32-S31)"

static const char *const DEFAULTS =
    "# name|url[|backup url|backup url]  (http only)\n"
    "Retro Lofi Chiptune Radio|http://a5.asurahosting.com:8150/radio.mp3\n"
    "Radio GamePlay|http://c22.radioboss.fm:8144/GamePlay\n"
    "Kaaos Chiptune|http://stream.kaaosradio.fi:8000/chip\n"
    "Rainwave Chiptunes|http://allrelays.rainwave.cc/chiptune.mp3\n"
    "Radio Paradise|http://stream.radioparadise.com/mp3-192\n"
    "SomaFM Groove Salad|http://ice1.somafm.com/groovesalad-128-mp3\n";

/* the 簡中 default list: music stations only (owner list, probed over HTTP 2026-11-15; reachability inside mainland China is a board test). UTF-8 as octal escapes: the CJK literal scan (i18n) is for UI texts, this is card file content */
static const char *const DEFAULTS_CN[] = {
    "# name|url[|backup url|backup url]  (http only; music stations, probed 2026-11-15)\n",
    "\346\270\205\346\231\250\351\237\263\344\271\220\345\217\260|http://lhttp.qingting.fm/live/4915/64k.mp3\n",
    "\344\270\212\346\265\267\347\273\217\345\205\270\351\237\263\344\271\220\345\271\277\346\222\255|http://lhttp.qingting.fm/live/267/64k.mp3\n",
    "\345\223\210\345\260\224\346\273\250\345\217\244\345\205\270\351\237\263\344\271\220\345\271\277\346\222\255|http://lhttp.qingting.fm/live/5022338/64k.mp3\n",
    "\345\277\203\347\201\265\351\237\263\344\271\220\351\246\206|http://stream3.hndt.com/now/AFqvb0VX/playlist.m3u8\n",
    "\345\244\251\347\261\201\344\271\213\351\237\263 Hi-Fi|http://play-radio-stream3.hndt.com/now/JxkyN5mR/chunklist.m3u8\n",
    "\345\244\251\347\261\201\344\271\213\351\237\263 \345\233\275\351\243\216|http://play-radio-stream3.hndt.com/now/UzPxbRaT/chunklist.m3u8\n",
    "\346\260\221\350\260\243\302\267\350\223\235\350\260\203|http://stream3.hndt.com/now/XWfN89gh/playlist.m3u8\n",
    "\346\261\237\350\213\217\347\273\217\345\205\270\346\265\201\350\241\214\351\237\263\344\271\220|http://lzlive.vojs.cn/2wtSfrS/92/live.m3u8\n",
};

static pc_station_t *s_list;
static int s_n, s_cur = -1;
static uint32_t s_gen;                       /* audio play generation */

/* shared with the net task */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_req;              /* bumped by LVGL on every play/stop */
static char s_urls[1 + PC_STATION_ALTS][256]; /* the requested station: its address and the backups (under s_mux) */
EXT_RAM_BSS_ATTR static int s_nurl, s_start;  /* how many, and which one to try first (under s_mux); PSRAM: no internal RAM for the backup-address state */
EXT_RAM_BSS_ATTR static volatile int s_cand_now;              /* the address the net task is on (the decoder-failure retry moves on from it) */
static char s_title[160], s_state[64];       /* ICY title, connection state (under s_mux) */
static volatile bool s_audio_on;             /* the audio service holds s_st */
static volatile pc_fmt_t s_fmt;
static pc_ring_t s_ring;
static pc_stream_t s_st;
static pc_ts_t s_ts;                         /* HLS: the TS demuxer, kept across segments */
static TaskHandle_t s_task;
static const now_ops_t s_ops;
EXT_RAM_BSS_ATTR static volatile uint32_t s_stat_bytes, s_stat_reconn;   /* since boot (radio_stats): the soak line of the broadcast-radio boot; PSRAM, no internal RAM */
static int s_errs;                           /* decoder failures in a row */
static volatile bool s_finished;             /* HLS #EXT-X-ENDLIST reached and fed */
static volatile bool s_probing;              /* one probe at a time: it uses static buffers */
static int64_t s_started_us;
/* https (TLS in PSRAM, CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC): ~3 KB internal per connection plus the net task's stack. Refused when the internal heap cannot spare it
 * (broadcast + Wi-Fi sits at the 30 KB floor: no mode is refused outright, the heap check decides); the reason is shown (honest degrade).
 * Without the external allocation every connection would take ~25 KB internal and tls_ok() would pass with BT or broadcast starving behind it: the build refuses that. */
#if !CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC
#error "radio_svc.c needs CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y (TLS buffers in PSRAM): delete the stale sdkconfig of this build directory to regenerate it from sdkconfig.defaults"
#endif
/* the net task's stack, PSRAM (0 B internal): HTTP + the TLS handshake with the full certificate bundle run on it. 6 KB was never measured for a handshake (PEERM sized its
 * TLS run on 10 KB); 10 KB costs 4 KB more PSRAM, and the first https open logs the real high-water mark (APP radio tls stack free_min) for the board check. */
#define NET_STACK 10240
#define TLS_MIN_LARGEST (24 * 1024)
static bool tls_ok(void)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= TLS_MIN_LARGEST;
}
/* the http twin's outcome per ADDRESS (FNV-1a of the https URL; a station's addresses differ): 0 unknown, 1 the twin played, 2 the twin failed; cleared when the list is reloaded.
 * 48 slots, the oldest is replaced; PSRAM. */
#define TWIN_SLOTS 48
EXT_RAM_BSS_ATTR static struct { uint32_t h; uint8_t v; } s_twin[TWIN_SLOTS];
EXT_RAM_BSS_ATTR static uint8_t s_twin_next;
static uint8_t *twin_slot(const char *url)
{
    uint32_t h = 2166136261u;
    for (const char *p = url; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    if (!h) h = 1;
    for (int i = 0; i < TWIN_SLOTS; i++) if (s_twin[i].h == h) return &s_twin[i].v;
    int k = s_twin_next++ % TWIN_SLOTS;
    s_twin[k].h = h;
    s_twin[k].v = 0;
    return &s_twin[k].v;
}

EXT_RAM_BSS_ATTR static int s_ci, s_cn;   /* net task: the address being tried, of how many */
static void set_diag(pc_rdiag_t d);
static void set_state(const char *s) { taskENTER_CRITICAL(&s_mux); snprintf(s_state, sizeof s_state, "%s", s); taskEXIT_CRITICAL(&s_mux); }
static void changed_cb(void *arg) { (void)arg; player_ctl_changed(); }
/* the connection diagnosis, one line on the playing page: what went wrong and which of the addresses (2/3) it was */
static void set_diag(pc_rdiag_t d)
{
    const char *m = d == PC_RDIAG_TIMEOUT ? T(S_CONNECTION_TIMED_OUT) : d == PC_RDIAG_REFUSED ? T(S_CONNECTION_REFUSED)
                  : d == PC_RDIAG_STATUS ? T(S_SERVER_ANSWERED_WITH_AN_ERROR) : d == PC_RDIAG_UNSUPPORTED ? T(S_FORMAT_NOT_SUPPORTED)
                  : d == PC_RDIAG_HTTPS ? T(S_HTTPS_IS_NOT_SUPPORTED) : T(S_CANNOT_CONNECT);
    char b[64];
    if (s_cn > 1) snprintf(b, sizeof b, "%s (%d/%d)", m, s_ci + 1, s_cn);
    else snprintf(b, sizeof b, "%s", m);
    set_state(b);
}

/* ---- the station lists on the card: radio.txt (the main list), radio_<stem>.txt; the one in use is cfg "radio_list" ("radio" = the main one), else the language's default ---- */
static bool list_exists(const char *stem)
{
    char path[64];
    if (pc_radio_list_path(stem, path, sizeof path) < 0) return false;
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}
const char *radio_list_stem(void)
{
    const char *v = cfg_str("radio_list", NULL);
    if (v && !strcmp(v, "radio")) return "";
    if (v && v[0] && pc_radio_stem_ok(v) && list_exists(v)) return v;
    return pc_radio_default_stem((int)i18n_lang());
}
void radio_list_path(char *out, size_t cap)
{
    if (pc_radio_list_path(radio_list_stem(), out, cap) < 0) snprintf(out, cap, "%s", RADIO_LIST);
}
int radio_lists(char (*stems)[PC_RADIO_STEM_MAX], int max)
{
    return pc_radio_scan_lists(PC_RADIO_DIR, stems, max);
}
static void write_default(const char *stem)             /* a list that is not on the card yet: its default text (the 簡中 one is empty) */
{
    char path[64];
    if (!g_sd_ok || list_exists(stem) || pc_radio_list_path(stem, path, sizeof path) < 0) return;
    FILE *f = fopen(path, "wb");
    if (f) {
        if (stem[0]) for (size_t i = 0; i < sizeof DEFAULTS_CN / sizeof DEFAULTS_CN[0]; i++) fputs(DEFAULTS_CN[i], f);
        else fputs(DEFAULTS, f);
        fclose(f);
    }
}

int radio_load(void)
{
    if (!s_list) s_list = heap_caps_calloc(RADIO_MAX, sizeof *s_list, MALLOC_CAP_SPIRAM);
    if (!s_list) return 0;
    memset(s_twin, 0, sizeof s_twin);
    char *t = heap_caps_malloc(32 * 1024, MALLOC_CAP_SPIRAM);
    if (!t) return 0;
    write_default("");                                    /* the main list, and the language's own when it is another one (a chooser needs two) */
    write_default(pc_radio_default_stem((int)i18n_lang()));
    char path[64];
    radio_list_path(path, sizeof path);
    FILE *f = fopen(path, "rb");
    size_t len = 0;
    if (f) { len = fread(t, 1, 32 * 1024 - 1, f); fclose(f); }
    if (!len && !radio_list_stem()[0]) { snprintf(t, 32 * 1024, "%s", DEFAULTS); len = strlen(t); }   /* the main list never shows empty: no card or an empty file */
    t[len] = 0;
    char playing[256] = "";
    if (s_cur >= 0 && s_cur < s_n) snprintf(playing, sizeof playing, "%s", s_list[s_cur].url);
    s_n = pc_radio_parse(t, s_list, RADIO_MAX);
    free(t);
    if (s_cur >= 0) {                                     /* the list changed under a playing station: follow it */
        int at = -1;
        for (int i = 0; i < s_n && at < 0; i++) if (!strcmp(s_list[i].url, playing)) at = i;
        if (at >= 0) s_cur = at;
        else radio_stop();
    }
    printf("APP radio stations=%d list=%s\n", s_n, radio_list_stem()[0] ? radio_list_stem() : "radio");
    return s_n;
}
int radio_list_select(const char *stem)                  /* "" = the main list; -1 when there is no such list */
{
    if (!stem || !pc_radio_stem_ok(stem) || (stem[0] && !list_exists(stem)) || (!stem[0] && !list_exists(""))) return -1;
    cfg_set_str("radio_list", stem[0] ? stem : "radio");
    return radio_load();
}
int radio_count(void) { return s_n; }
const pc_station_t *radio_station(int i) { return i >= 0 && i < s_n ? &s_list[i] : NULL; }
int radio_current(void) { return s_cur; }
int radio_add_temp(const char *name, const char *url)      /* console tests: not saved */
{
    if (!s_list && !radio_load()) return -1;
    if (s_n >= RADIO_MAX) return -1;
    snprintf(s_list[s_n].name, sizeof s_list[s_n].name, "%s", name);
    snprintf(s_list[s_n].url, sizeof s_list[s_n].url, "%s", url);
    return s_n++;
}

/* ---- the connection (net task) ---- */
typedef struct { int metaint, br; char ctype[64]; } hdrs_t;
static esp_err_t on_http(esp_http_client_event_t *e)
{
    hdrs_t *h = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_HEADER && h) {
        if (!strcasecmp(e->header_key, "icy-metaint")) h->metaint = atoi(e->header_value);
        else if (!strcasecmp(e->header_key, "icy-br")) h->br = atoi(e->header_value);   /* "128,128": the first */
        else if (!strcasecmp(e->header_key, "content-type")) snprintf(h->ctype, sizeof h->ctype, "%s", e->header_value);
    }
    return ESP_OK;
}

static bool want(uint32_t req) { return s_req == req; }

/* waits up to ms, false when a new request came in */
static bool nap(uint32_t req, int ms)
{
    for (int t = 0; t < ms && want(req); t += 100) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    return want(req);
}

static void release_cb(void *arg) { (void)arg; s_audio_on = false; }   /* audio task: done with s_st */
static void start_audio(void *arg)                 /* LVGL */
{
    if ((uint32_t)(uintptr_t)arg != s_req || s_cur < 0) return;
    s_st.inner = s_fmt == PC_FMT_AAC ? &backend_aac : s_fmt == PC_FMT_OGG ? &backend_ogg : &pc_backend_mp3;
    pc_reader_t r = { .u = &s_st };
    s_audio_on = true;
    s_gen = audio_svc_play(&pc_backend_stream, &r, 0, 0, release_cb, NULL);
    if (!s_gen) s_audio_on = false;
    s_started_us = esp_timer_get_time();
    printf("APP radio audio fmt=%d gen=%lu\n", s_fmt, (unsigned long)s_gen);
    player_ctl_changed();
}

/* first time the ring holds the prebuffer: decide the decoder from the bytes, start the audio */
static void maybe_start(uint32_t req, bool *started)
{
    if (*started || pc_ring_used(&s_ring) < s_st.prebuffer) return;
    static uint8_t peek[16384];                        /* what the bytes are beats what the header says */
    pc_fmt_t real = pc_radio_sniff(peek, pc_ring_peek(&s_ring, peek, sizeof peek));
    printf("APP radio sniff=%d header=%d\n", real, s_fmt);
    if (real != PC_FMT_UNKNOWN) s_fmt = real;
    *started = true;
    set_state("");
    ui_async(start_audio, (void *)(uintptr_t)req);
}

static volatile bool s_got;                    /* the current try moved audio bytes (the ICY loop and the HLS segments both set it) */
/* plain GET of a whole small body (HLS playlists, http only, redirects followed); length or <0 */
static int get_text(const char *url0, char *out, int cap)
{
    char url[384];
    snprintf(url, sizeof url, "%s", url0);
    for (int hop = 0; hop < 4; hop++) {
        if (!pc_url_is_web(url) || (pc_url_is_https(url) && !tls_ok())) return -2;
        esp_http_client_config_t c = { .url = url, .user_agent = UA, .timeout_ms = 8000, .disable_auto_redirect = true, .crt_bundle_attach = esp_crt_bundle_attach };
        esp_http_client_handle_t cl = esp_http_client_init(&c);
        if (!cl) return -1;
        int st = esp_http_client_open(cl, 0) == ESP_OK && esp_http_client_fetch_headers(cl) >= -1 ? esp_http_client_get_status_code(cl) : 0;
        if (st >= 300 && st < 400) { esp_http_client_set_redirection(cl); esp_http_client_get_url(cl, url, sizeof url); esp_http_client_cleanup(cl); continue; }
        int n = 0, k;
        while (st == 200 && n < cap - 1 && (k = esp_http_client_read(cl, out + n, cap - 1 - n)) > 0) n += k;
        esp_http_client_cleanup(cl);
        out[n] = 0;
        if (n == cap - 1) { char *nl = strrchr(out, '\n'); if (nl) nl[1] = 0; }   /* cut off: drop the half line */
        return st == 200 ? n : -1;
    }
    return -1;
}

/* HLS: read the list, fetch each segment not played yet into the ring, read the list again. 0 reconnect, 1 new request */
static int hls_session(uint32_t req, const char *url0, const char *body0, bool *started, int *fails)
{
    static pc_hls_t h;
    static char body[16384], list[384];
    static uint8_t buf[4096];
    uint64_t last = UINT64_MAX;
    int idle = 0;
    pc_ts_init(&s_ts);
    snprintf(list, sizeof list, "%s", url0);
    snprintf(body, sizeof body, "%s", body0);
    set_state(*started ? "" : T(S_BUFFERING) /* 緩衝中… */);
    ui_async(changed_cb, NULL);
    for (int hops = 0;;) {
        if (!want(req)) return 1;
        if (!body[0] && get_text(list, body, sizeof body) < 0) { set_state(T(S_RECONNECTING) /* 重新連線… */); return 0; }
        int rc = pc_hls_parse(body, list, 200000, &h);
        body[0] = 0;
        if (rc) { set_state(T(S_CANNOT_READ_THE_HLS_LIST) /* 無法讀取 HLS 清單 */); return 0; }
        if (h.master) {
            if (++hops > 3) return 0;
            snprintf(list, sizeof list, "%s", h.variant);
            { char rl[256]; pc_url_redact(list, rl, sizeof rl); printf("APP radio hls variant=%s\n", rl); }
            continue;
        }
        int i = pc_hls_next(&h, last);
        if (i < 0) {                                         /* caught up: the list grows every few seconds */
            if (h.ended) { s_st.eof = true; s_finished = true; set_state(T(S_FINISHED) /* 播完了 */); ui_async(changed_cb, NULL); return 1; }
            if (++idle > 6) { set_state(T(S_THE_LIST_STOPPED_RECONNECTING) /* 清單沒有更新，重新連線… */); return 0; }   /* ~3 target lengths */
            if (!nap(req, h.target_s * 500)) return 1;
            continue;
        }
        idle = 0;
        for (; i < h.nseg; i++) {
            if (!want(req)) return 1;
            if (!pc_url_is_web(h.seg[i]) || (pc_url_is_https(h.seg[i]) && !tls_ok())) { set_diag(PC_RDIAG_HTTPS); return s_cn > 1 ? 0 : nap(req, 600000) ? 0 : 1; }   /* another address to try: go on at once */
            esp_http_client_config_t c = { .url = h.seg[i], .user_agent = UA, .timeout_ms = 8000, .buffer_size = 4096, .crt_bundle_attach = esp_crt_bundle_attach };
            esp_http_client_handle_t cl = esp_http_client_init(&c);
            if (!cl) return 0;
            int st = esp_http_client_open(cl, 0) == ESP_OK && esp_http_client_fetch_headers(cl) >= -1 ? esp_http_client_get_status_code(cl) : 0;
            int got = 0, n, kind = -1;                       /* -1 unknown, 0 TS, 1 packed audio (ADTS/MP3, maybe an ID3 tag first) */
            uint32_t skip = 0;
            static uint8_t es[4096];
            while (st == 200 && want(req) && (n = esp_http_client_read(cl, (char *)buf, sizeof buf)) > 0) {
                const uint8_t *p = buf;
                int m = n;
                if (kind < 0) {
                    kind = buf[0] == 0x47 ? 0 : 1;
                    if (kind && n >= 10 && !memcmp(buf, "ID3", 3))   /* packed audio starts with a timestamp tag */
                        skip = 10 + ((uint32_t)(buf[6] & 0x7F) << 21 | (uint32_t)(buf[7] & 0x7F) << 14 | (uint32_t)(buf[8] & 0x7F) << 7 | (buf[9] & 0x7F));
                }
                if (skip) { uint32_t k = skip < (uint32_t)m ? skip : (uint32_t)m; p += k; m -= (int)k; skip -= k; }
                if (kind == 0) { m = (int)pc_ts_feed(&s_ts, p, (size_t)m, es, sizeof es); p = es; }   /* TS -> the AAC/MP3 inside */
                for (int off = 0; off < m && want(req);) {   /* full ring: the decoder is behind, wait */
                    off += (int)pc_ring_write(&s_ring, p + off, (uint32_t)(m - off));
                    if (off < m) vTaskDelay(pdMS_TO_TICKS(20));
                }
                got += n;
                s_got = true;                                 /* I4: an HLS station over its http twin is audio too */
                maybe_start(req, started);
            }
            esp_http_client_cleanup(cl);
            if (st != 200 || got == 0) { printf("APP radio hls segment status=%d\n", st); set_state(T(S_RECONNECTING) /* 重新連線… */); return 0; }
            *fails = 0;
            last = h.seg_seq[i];
        }
    }
}

/* one connection: 0 = the server closed / failed (reconnect), 1 = a new request */
static int session_one(uint32_t req, const char *url0, bool *started, int *fails, bool twin)
{
    static hdrs_t h;
    static pc_icy_t icy;
    static uint8_t buf[4096];
    char url[256];
    snprintf(url, sizeof url, "%s", url0);
    for (int hop = 0; hop < 4; hop++) {
        if (!want(req)) return 1;                      /* I4: a station switch before the open: no connection, no TLS handshake for a request nobody wants */
        if (!pc_url_is_web(url) || (pc_url_is_https(url) && !tls_ok())) { set_diag(PC_RDIAG_HTTPS); return s_cn > 1 ? 0 : nap(req, 600000) ? 0 : 1; }   /* another address to try: go on at once */
        memset(&h, 0, sizeof h);
        esp_http_client_config_t c = { .url = url, .user_agent = UA, .timeout_ms = twin ? 3000 : 8000, .buffer_size = 4096,
                                       .event_handler = on_http, .user_data = &h, .disable_auto_redirect = true, .crt_bundle_attach = esp_crt_bundle_attach };
        esp_http_client_handle_t cl = esp_http_client_init(&c);
        if (!cl) return 0;
        esp_http_client_set_header(cl, "Icy-MetaData", "1");
        esp_http_client_set_header(cl, "Accept", "*/*");
        esp_err_t rc = esp_http_client_open(cl, 0);
        int64_t len = rc == ESP_OK ? esp_http_client_fetch_headers(cl) : -1;
        int status = rc == ESP_OK ? esp_http_client_get_status_code(cl) : 0;
        { char ru[256]; pc_url_redact(url, ru, sizeof ru); printf("APP radio open rc=%d status=%d type=%s metaint=%d url=%s\n", rc, status, h.ctype, h.metaint, ru); }   /* no userinfo, no query in the log */
        if (rc == ESP_OK && pc_url_is_https(url)) {   /* M8: the TLS handshake ran on this task's PSRAM stack: its high-water mark, once */
            static EXT_RAM_BSS_ATTR bool hwm_logged;   /* PSRAM */
            if (!hwm_logged) { hwm_logged = true; printf("APP radio tls stack free_min=%u of %u bytes\n", (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)NET_STACK); }
        }
        if (status >= 300 && status < 400) {           /* redirect: follow by hand (http only) */
            esp_http_client_set_redirection(cl);
            esp_http_client_get_url(cl, url, sizeof url);
            esp_http_client_cleanup(cl);
            if (twin && pc_url_is_https(url)) return 0;    /* the http twin only leads back to https: that is the https attempt's job */
            continue;
        }
        if (twin && (rc != ESP_OK || status != 200 || !strncasecmp(h.ctype, "text/html", 9))) { esp_http_client_cleanup(cl); return 0; }   /* quiet: the https try follows */
        if (rc != ESP_OK || status != 200) {
            int err = esp_http_client_get_errno(cl);
            esp_http_client_cleanup(cl);
            set_diag(pc_radio_diag(url, rc == ESP_OK, status, err, false));
            return 0;
        }
        pc_fmt_t fmt = pc_radio_guess(h.ctype, url);
        if (fmt == PC_FMT_PLAYLIST) {                  /* .pls / .m3u: the first stream in it; or HLS */
            static char text[16384];
            int n = 0, k;
            while (n < (int)sizeof text - 1 && (k = esp_http_client_read(cl, text + n, sizeof text - 1 - n)) > 0) n += k;
            esp_http_client_cleanup(cl);
            text[n] = 0;
            if (n == (int)sizeof text - 1) { char *nl = strrchr(text, '\n'); if (nl) nl[1] = 0; }   /* cut off: drop the half line */
            if (pc_hls_is_hls(text)) { if (!*started) s_fmt = PC_FMT_AAC; return hls_session(req, url, text, started, fails); }
            if (!pc_playlist_first_url(text, url, sizeof url)) { set_state(T(S_EMPTY_PLAYLIST) /* 播放清單是空的 */); return 0; }
            continue;
        }
        if (fmt == PC_FMT_UNKNOWN) fmt = PC_FMT_MP3;
        if (!*started) s_fmt = fmt;                    /* a reconnect keeps the decoder that is running */
        (void)len;
        pc_icy_init(&icy, h.metaint > 0 ? (uint32_t)h.metaint : 0);
        s_st.prebuffer = (uint32_t)pc_radio_prebuffer(h.br);   /* 2 s of this station */
        set_state(*started ? "" : T(S_BUFFERING) /* 緩衝中… */);
        ui_async(changed_cb, NULL);
        for (;;) {
            if (!want(req)) { esp_http_client_cleanup(cl); return 1; }
            int n = esp_http_client_read(cl, (char *)buf, sizeof buf);
            if (n <= 0) break;                         /* closed or timed out */
            *fails = 0;
            s_got = true;
            s_stat_bytes += (uint32_t)n;
            size_t a = pc_icy_feed(&icy, buf, (size_t)n);
            char t[160];
            if (pc_icy_take_title(&icy, t, sizeof t)) {
                taskENTER_CRITICAL(&s_mux); snprintf(s_title, sizeof s_title, "%s", t); taskEXIT_CRITICAL(&s_mux);
                printf("APP radio title=%s\n", t);
                ui_async(changed_cb, NULL);
            }
            for (size_t off = 0; off < a && want(req);) {   /* full ring: the decoder is behind, wait */
                off += pc_ring_write(&s_ring, buf + off, (uint32_t)(a - off));
                if (off < a) vTaskDelay(pdMS_TO_TICKS(20));
            }
            maybe_start(req, started);
        }
        esp_http_client_cleanup(cl);
        s_stat_reconn++;
        set_state(T(S_RECONNECTING) /* 重新連線… */);
        return 0;
    }
    set_state(T(S_TOO_MANY_REDIRECTS) /* 轉址太多次 */);
    return 0;
}

typedef struct { uint32_t req; bool *started; int *fails; int ret; } sess_t;
static int sess_try(void *c, const char *url)
{
    sess_t *x = c;
    s_got = false;
    x->ret = session_one(x->req, url, x->started, x->fails, false);
    return s_got;
}
static int sess_try_twin(void *c, const char *url)       /* an http address that only exists because the listing is https: short timeout */
{
    sess_t *x = c;
    s_got = false;
    x->ret = session_one(x->req, url, x->started, x->fails, pc_url_is_https(url) ? false : true);
    if (s_got) return 1;
    if (x->ret == 1 || !want(x->req)) return PC_TRY_STOP;   /* I4: a new request, a finished list: no verdict on the twin, and no https try for the old station */
    return 0;
}
/* https listing: its http twin first, then https (outcome cached per station); http addresses go straight through */
static int session(uint32_t req, const char *url, bool *started, int *fails)
{
    sess_t x = { req, started, fails, 0 };
    char used[256];
    if (!pc_url_is_https(url)) { sess_try(&x, url); return x.ret; }
    uint8_t *cache = twin_slot(url);
#ifdef APP_RADIO_TEST_HOOKS
    if (cfg_int("https_notwin", 0)) *cache = PC_TWIN_BAD;     /* TEST ONLY (HTTPS report): measure the TLS path even where the http twin plays; not compiled into the product */
#endif
    pc_https_play(cache, url, used, sizeof used, sess_try_twin, &x);
    if (*cache == PC_TWIN_OK) { char ru[256]; pc_url_redact(used, ru, sizeof ru); printf("APP radio https listing played over http %s\n", ru); }
    return x.ret;
}

static void net_task(void *arg)
{
    (void)arg;
    uint32_t seen = 0;                          /* s_req starts at 0: the request that created this task is new */
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (seen != s_req) {
            uint32_t req = seen = s_req;
            char urls[1 + PC_STATION_ALTS][256];
            int nurl, cand;
            taskENTER_CRITICAL(&s_mux); memcpy(urls, s_urls, sizeof urls); nurl = s_nurl; cand = s_start; taskEXIT_CRITICAL(&s_mux);
            if (cand < 0 || cand >= nurl) cand = 0;
            for (int t = 1; s_audio_on; t++) {              /* the old stream must be released before s_st is reused */
                vTaskDelay(pdMS_TO_TICKS(10));
                if (t % 300 == 0) { printf("APP radio waiting for the audio task to let go\n"); audio_svc_stop(false); }
            }
            pc_ring_init(&s_ring, s_ring.buf, RING_SIZE);
            s_st = (pc_stream_t){ .ring = &s_ring, .prebuffer = 32768 };
            taskENTER_CRITICAL(&s_mux); s_title[0] = 0; taskEXIT_CRITICAL(&s_mux);
            if (!nurl || !urls[0][0]) continue;       /* stop */
            bool started = false;
            int fails = 0, backoff;
            while (want(req)) {
                if (!wifi_svc_connected()) {
                    /* I5: nobody holds the radio (a deny clears every holder): ask again; refused = denied (quick settings / airplane): say why, not an endless wait; allowed again = held again */
                    const bool refused = !(wifi_svc_hold_mask() & PC_WH_RADIO) && wifi_svc_hold(PC_WH_RADIO) < 0;
                    set_state(refused ? T(S_RADIO_WIFI_OFF) /* Wi-Fi 已關閉／飛航模式 */ : T(S_WAITING_FOR_WI_FI) /* 等待 Wi-Fi… */);
                    ui_async(changed_cb, NULL); nap(req, 500); continue;
                }
                s_ci = s_cand_now = cand;
                s_cn = nurl;
                if (session(req, urls[cand], &started, &fails)) break;
                ui_async(changed_cb, NULL);
                fails++;                                         /* consecutive failures without data: never reset to switch address */
                bool rotate = nurl > 1 && (!started || fails % 3 == 0);   /* never got audio, or every third failure: the next backup address */
                if (rotate) cand = (cand + 1) % nurl;
                backoff = pc_radio_backoff_ms(fails, nurl, rotate && cand != 0);   /* mid-round: at once; a full round of failures doubles the wait, up to 30 s */
                printf("APP radio retry in %d ms\n", backoff);
                if (!nap(req, backoff)) break;
            }
        }
    }
}

static void request(const pc_station_t *st, int start)   /* NULL stops */
{
    taskENTER_CRITICAL(&s_mux);
    s_nurl = st ? pc_station_urls(st) : 0;
    for (int k = 0; k < 1 + PC_STATION_ALTS; k++) snprintf(s_urls[k], sizeof s_urls[k], "%s", k < s_nurl ? pc_station_url(st, k) : "");
    s_start = start;
    s_req++;
    taskEXIT_CRITICAL(&s_mux);
    if (s_task) xTaskNotifyGive(s_task);
}

/* ---- playback owner (LVGL) ---- */
static void forget(void) { s_cur = -1; request(NULL, 0); wifi_svc_release(PC_WH_RADIO); }   /* another source took over: Wi-Fi goes 60 s later */
static void on_event(const aud_event_t *e)
{
    if (e->gen != s_gen || s_cur < 0) return;
    printf("APP radio audio event=%d\n", e->type);
    if (e->type == AUD_ENDED && s_finished) { radio_stop(); return; }   /* an HLS list that ended (#EXT-X-ENDLIST) */
    if (e->type == AUD_ERROR || e->type == AUD_ENDED) {   /* undecodable or the decoder gave up: connect again */
        if (esp_timer_get_time() - s_started_us > 30000000) s_errs = 0;   /* it played a while: a fresh failure */
        if (++s_errs >= 3) {
            int cur = s_cur;
            radio_stop();
            ui_push_message(T(S_CANNOT_PLAY_THIS_STATION_FORMAT) /* 這台無法播放（格式不支援） */);
            printf("APP radio give up station=%d\n", cur);
            return;
        }
        set_state(T(S_RECONNECTING) /* 重新連線… */);
        request(&s_list[s_cur], s_cand_now);   /* the audio had started: the same address again (the connection logic moves on if it fails) */
    }
}

void radio_stats(uint32_t *bytes, uint32_t *reconnects, uint32_t *underruns) { *bytes = s_stat_bytes; *reconnects = s_stat_reconn; *underruns = s_st.underruns; }

static void redo_play(int a, int b) { (void)b; radio_play(a); }
int radio_play(int i)
{
    if (!radio_station(i)) return -1;
    if (spk_excl_gate(redo_play, i, 0)) return -5;          /* speaker mode is an exclusive source: asked first (player_ctl.h); nothing started */
    if (bt_svc_mode() == BT_MODE_BROADCAST && !bt_svc_wifi_ok()) { ui_push_message(T(S_NO_RADIO_IN_BROADCAST_MODE) /* 廣播模式不能聽電台 */); return -2; }
    int first = pc_station_pick(&s_list[i], tls_ok());      /* http first; https only when the station has no http address and TLS fits in this mode */
    if (first < 0) {                                         /* M7: two different reasons */
        ui_push_message(pc_station_pick(&s_list[i], true) < 0 ? T(S_RADIO_NO_WEB_ADDRESS) /* 這台電台沒有 http 或 https 網址 */ : T(S_HTTPS_IS_NOT_SUPPORTED) /* 記憶體不足，不能用 https */);
        return -3;
    }
    if (!s_ring.buf) {
        s_ring.buf = heap_caps_malloc(RING_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_ring.buf) return -4;
        xTaskCreatePinnedToCoreWithCaps(net_task, "radio", NET_STACK, NULL, 4, &s_task, 0, MALLOC_CAP_SPIRAM);
    }
    const bool wifi_denied = wifi_svc_hold(PC_WH_RADIO) < 0 && !cfg_int("wifi_allow", 1);   /* kept while the radio is the player's source, also paused or retrying; I5: a denied Wi-Fi is said, not waited for */
    player_ctl_take(&s_ops, forget, on_event);
    if (s_audio_on) audio_svc_stop(false);
    s_cur = i;
    s_gen = 0;
    s_errs = 0;
    s_finished = false;
    cfg_set_int("radio_last", i);
    set_state(wifi_denied ? T(S_RADIO_WIFI_OFF) /* Wi-Fi 已關閉／飛航模式 */ : T(S_CONNECTING) /* 連線中… */);
    request(&s_list[i], first);
    printf("APP radio play %d %s\n", i, s_list[i].name);
    player_ctl_changed();
    return 0;
}

void radio_stop(void)
{
    if (s_cur < 0) return;
    s_cur = -1;
    request(NULL, 0);
    wifi_svc_release(PC_WH_RADIO);
    audio_svc_stop(false);
    player_ctl_release();
    player_ctl_changed();
}

static void title(char *out, size_t cap)
{
    char t[160], st[64];
    taskENTER_CRITICAL(&s_mux); memcpy(t, s_title, sizeof t); memcpy(st, s_state, sizeof st); taskEXIT_CRITICAL(&s_mux);
    const char *name = s_cur >= 0 ? s_list[s_cur].name : "";
    snprintf(out, cap, "%s\n%s", name, st[0] ? st : t);
}
static void next(void) { if (s_n) radio_play((s_cur + 1) % s_n); }
static bool playing(void) { return s_cur >= 0; }
static void open_list(void) { ui_open_once(&scr_radio, NULL); }   /* the station list may be under the cover page already (a list page is on the stack once) */
static const char *list_label(void) { return T(S_STATIONS) /* 電台清單 */; }
static bool fav_id(char *out, size_t cap) { if (s_cur < 0) return false; snprintf(out, cap, "radio:%s", s_list[s_cur].url); return true; }
static const now_ops_t s_ops = { .src = NOW_SRC_RADIO, .title = title, .fav_id = fav_id, .next = next, .stop = radio_stop, .playing = playing,
                                 .open_list = open_list, .list_label = list_label };

/* ---- probe: can the device itself play this address? (settings page, before adding a station) ---- */
typedef struct { char url[384]; int id; void (*done)(int id, bool ok, const char *fmt, const char *url, const char *why); } probe_t;
static const char *fmt_name(pc_fmt_t f) { return f == PC_FMT_MP3 ? "mp3" : f == PC_FMT_AAC ? "aac" : f == PC_FMT_OGG ? "ogg" : f == PC_FMT_TS ? "ts" : "?"; }
static void probe_task(void *arg)
{
    probe_t *p = arg;
    static char text[16384];
    char url[384], why[64] = "";
    const char *fmt = "";
    bool ok = false;
    snprintf(url, sizeof url, "%s", p->url);
    wifi_svc_hold(PC_WH_PROBE);                       /* its own bit: the playing radio keeps its hold */
    for (int t = 0; t < 120 && !wifi_svc_connected(); t++) vTaskDelay(pdMS_TO_TICKS(100));
    if (!wifi_svc_connected()) { snprintf(why, sizeof why, "wifi"); goto out; }
    char orig[384], twin_url[384];
    snprintf(orig, sizeof orig, "%s", url);
    const bool has_twin = pc_url_http_twin(orig, twin_url, sizeof twin_url) > 0;   /* an https address: its http twin first */
    for (int pass = 0; pass < 2 && !ok; pass++) {
    if (pass == 1 && !has_twin) break;
    snprintf(url, sizeof url, "%s", has_twin ? (pass == 0 ? twin_url : orig) : orig);
    why[0] = 0; fmt = "";
    for (int hop = 0; hop < 5 && !ok && !why[0]; hop++) {
        if (!pc_url_is_web(url) || (pc_url_is_https(url) && !tls_ok())) { snprintf(why, sizeof why, "https"); break; }
        hdrs_t h = { 0 };
        esp_http_client_config_t c = { .url = url, .user_agent = UA, .timeout_ms = has_twin && pass == 0 ? 3000 : 6000, .buffer_size = 4096,
                                       .event_handler = on_http, .user_data = &h, .disable_auto_redirect = true, .crt_bundle_attach = esp_crt_bundle_attach };
        esp_http_client_handle_t cl = esp_http_client_init(&c);
        if (!cl) { snprintf(why, sizeof why, "memory"); break; }
        esp_http_client_set_header(cl, "Accept", "*/*");
        int st = esp_http_client_open(cl, 0) == ESP_OK && esp_http_client_fetch_headers(cl) >= -1 ? esp_http_client_get_status_code(cl) : 0;
        int err = esp_http_client_get_errno(cl);
        if (st >= 300 && st < 400) { esp_http_client_set_redirection(cl); esp_http_client_get_url(cl, url, sizeof url); esp_http_client_cleanup(cl); continue; }
        if (st != 200) { esp_http_client_cleanup(cl); snprintf(why, sizeof why, st ? "status %d" : err == ECONNREFUSED ? "refused" : "no answer", st); break; }
        int n = 0, k;
        int64_t t0 = esp_timer_get_time();
        while (n < (int)sizeof text - 1 && esp_timer_get_time() - t0 < 4000000 && (k = esp_http_client_read(cl, text + n, sizeof text - 1 - n)) > 0) n += k;
        esp_http_client_cleanup(cl);
        text[n] = 0;
        if (pc_hls_is_hls(text)) {                        /* follow to the first segment: it must be http too */
            static pc_hls_t hl;
            for (int v = 0; v < 3 && !pc_hls_parse(text, url, 200000, &hl) && hl.master; v++) {
                snprintf(url, sizeof url, "%s", hl.variant);
                if (get_text(url, text, sizeof text) < 0) { hl.nseg = 0; break; }
            }
            if (hl.master || !hl.nseg) snprintf(why, sizeof why, "hls list");
            else if (!pc_url_is_web(hl.seg[0]) || (pc_url_is_https(hl.seg[0]) && !tls_ok())) snprintf(why, sizeof why, "https");
            else { ok = true; fmt = "hls"; }
            break;
        }
        pc_fmt_t f = pc_radio_guess(h.ctype, url);
        if (f == PC_FMT_PLAYLIST) {
            char next[384];
            if (!pc_playlist_first_url(text, next, sizeof next)) { snprintf(why, sizeof why, "empty playlist"); break; }
            snprintf(url, sizeof url, "%s", next);
            continue;
        }
        pc_fmt_t real = pc_radio_sniff((const unsigned char *)text, (size_t)n);
        if (real == PC_FMT_UNKNOWN) { snprintf(why, sizeof why, "format"); break; }
        ok = true;
        fmt = fmt_name(real);
    }
    }
    if (!ok && !why[0]) snprintf(why, sizeof why, "redirects");
out:
    printf("APP radio probe ok=%d fmt=%s why=%s\n", ok, fmt, why);
    p->done(p->id, ok, fmt, url, why);
    free(p);
    wifi_svc_release(PC_WH_PROBE);
    s_probing = false;
    vTaskDeleteWithCaps(NULL);
}

int radio_probe(const char *url, int id, void (*done)(int id, bool ok, const char *fmt, const char *url, const char *why))
{
    if (bt_svc_mode() == BT_MODE_BROADCAST) return -1;
    if (s_probing) return -2;
    probe_t *p = heap_caps_calloc(1, sizeof *p, MALLOC_CAP_SPIRAM);
    if (!p) return -1;
    snprintf(p->url, sizeof p->url, "%s", url);
    p->id = id;
    p->done = done;
    s_probing = true;
    if (xTaskCreatePinnedToCoreWithCaps(probe_task, "probe", 8192, p, 3, NULL, 0, MALLOC_CAP_SPIRAM) != pdPASS) { free(p); s_probing = false; return -1; }
    return 0;
}
