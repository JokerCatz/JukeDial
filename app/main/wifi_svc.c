// SPDX-License-Identifier: LGPL-2.1-or-later
#include "wifi_svc.h"
#include "pc/pc_paths.h"
#include "bt_svc.h"
#include "cfg.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "memlog.h"
#include "pc/pc_kv.h"
#include "pc/pc_wifihold.h"

#define WIFI_CFG PC_CFG_WIFI

static QueueHandle_t s_q;
static volatile bool s_on, s_up, s_want;
static char s_ip[16];
static esp_netif_t *s_netif;
static bool s_inited_net;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        s_up = false;
        s_ip[0] = 0;
        printf("APP wifi disconnected reason=%d\n", d->reason);
        if (s_want) esp_wifi_connect();                 /* the driver paces retries */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&e->ip_info.ip));
        s_up = true;
        printf("APP wifi ip=%s rssi=%d\n", s_ip, wifi_svc_rssi());
        memlog("wifi_up");
    }
}

static int start(void)
{
    pc_kv_t kv[4];
    int n = pc_kv_load(WIFI_CFG, kv, 4);
    const char *ssid = pc_kv_get(kv, n, "ssid"), *pass = pc_kv_get(kv, n, "pass");
    if (!ssid || !*ssid) { printf("APP wifi no %s\n", WIFI_CFG); return -1; }
    if (!s_inited_net) {
        esp_netif_init();
        esp_event_loop_create_default();                /* ESP_ERR_INVALID_STATE if it exists: fine */
        s_netif = esp_netif_create_default_wifi_sta();
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
        s_inited_net = true;
    }
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&ic);
    if (e != ESP_OK) { printf("APP wifi init %s\n", esp_err_to_name(e)); return -1; }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);             /* no NVS writes: credentials live on the TF card */
    wifi_config_t wc = { 0 };
    snprintf((char *)wc.sta.ssid, sizeof wc.sta.ssid, "%s", ssid);
    snprintf((char *)wc.sta.password, sizeof wc.sta.password, "%s", pass ? pass : "");
    wc.sta.threshold.authmode = (pass && *pass) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    s_want = true;
    e = esp_wifi_start();
    printf("APP wifi start rc=%s\n", esp_err_to_name(e));   /* no SSID in the log (the project rules: Wi-Fi settings never reach a log) */
    memlog("wifi_start");
    return e == ESP_OK ? 0 : -1;
}

static void stop(void)
{
    s_want = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    s_up = false;
    s_ip[0] = 0;
    memlog("wifi_stop");
}

/* Holders (pc_wifihold) are kept by the wifi_ctl task only: every caller posts a message, so no lock is needed. */
typedef struct { uint8_t op, who; } wmsg_t;
enum { OP_HOLD, OP_RELEASE, OP_FORCE_OFF, OP_RECONNECT };
static pc_wifihold_t s_h;
static volatile uint8_t s_hold_mask;                    /* mirror of s_h.holders for wifi st */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void wifi_task(void *arg)
{
    (void)arg;
    uint8_t logged_mask = 0;
    bool logged_on = false;
    for (;;) {
        wmsg_t m;
        TickType_t wait = pc_wh_pending(&s_h) ? pdMS_TO_TICKS(1000) : portMAX_DELAY;     /* the grace timer: no timer object */
        bool got = xQueueReceive(s_q, &m, wait) == pdTRUE;     /* start() is tried only on a pass that got a message: a failed start is not retried every second of the grace */
        if (got) {
            if (m.op == OP_HOLD) pc_wh_hold(&s_h, (pc_wh_who_t)m.who);
            else if (m.op == OP_RELEASE) pc_wh_release(&s_h, (pc_wh_who_t)m.who, now_ms());
            else if (m.op == OP_FORCE_OFF) pc_wh_force_off(&s_h);
            else if (m.op == OP_RECONNECT && s_on) { stop(); s_on = start() == 0; }   /* new network: holders unchanged */
        }
        pc_wh_tick(&s_h, now_ms());
        s_hold_mask = s_h.holders;
        if (s_h.holders != logged_mask || s_h.on != logged_on) {
            logged_mask = s_h.holders; logged_on = s_h.on;
            printf("APP wifi hold=%d on=%d\n", logged_mask, logged_on);
        }
        if (s_h.on && !s_on && got) s_on = start() == 0;
        else if (!s_h.on && s_on) { stop(); s_on = false; }
    }
}

void wifi_svc_init(void)
{
    if (s_q) return;
    s_q = xQueueCreate(8, sizeof(wmsg_t));
    xTaskCreatePinnedToCore(wifi_task, "wifi_ctl", 4096, NULL, 5, NULL, 0);   /* internal stack: NVS/flash safe */
}

static int post(int op, int who)
{
    wmsg_t m = { (uint8_t)op, (uint8_t)who };
    return (s_q && xQueueSend(s_q, &m, pdMS_TO_TICKS(50)) == pdTRUE) ? 0 : -1;
}

static bool broadcast_refused(void)
{
    if (bt_svc_wifi_ok()) return false;                    /* the broadcast-radio boot: Wi-Fi first, the broadcast second (cast_mode.c) */
    if (bt_svc_mode() == BT_MODE_BROADCAST || cfg_int("bt_mode", 0) == BT_MODE_BROADCAST) {   /* at boot the mode is still starting */
        printf("APP wifi refused: broadcast mode\n");   /* not enough internal RAM */
        return true;
    }
    return false;
}

int wifi_svc_hold(int who) { return (!cfg_int("wifi_allow", 1) && !bt_svc_lean()) || broadcast_refused() ? -1 : post(OP_HOLD, who); }   /* the broadcast radio boot (bt_svc_lean) asked for Wi-Fi by switching to it: the airplane setting stands aside there */   /* GS: quick settings' Wi-Fi switch = allow / deny (airplane denies) */
void wifi_svc_release(int who) { post(OP_RELEASE, who); }
int wifi_svc_set(bool on) { return on ? wifi_svc_hold(PC_WH_MANUAL) : post(OP_FORCE_OFF, 0); }
void wifi_svc_reconnect(void) { post(OP_RECONNECT, 0); }
int wifi_svc_hold_mask(void) { return s_hold_mask; }

bool wifi_svc_on(void) { return s_on; }
bool wifi_svc_connected(void) { return s_up; }
const char *wifi_svc_ip(void) { return s_ip; }

int wifi_svc_rssi(void)
{
    wifi_ap_record_t ap;
    return (s_up && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
}

void wifi_svc_bench(const char *url, int seconds)
{
    if (!s_up) { printf("APP wget not connected\n"); return; }
    esp_http_client_config_t c = { .url = url, .timeout_ms = 5000, .buffer_size = 4096 };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h || esp_http_client_open(h, 0) != ESP_OK) { printf("APP wget open failed\n"); if (h) esp_http_client_cleanup(h); return; }
    esp_http_client_fetch_headers(h);
    static char buf[4096];
    int64_t t0 = esp_timer_get_time(), tl = t0, end = t0 + (int64_t)seconds * 1000000;
    long total = 0, sec = 0;
    while (esp_timer_get_time() < end) {
        int r = esp_http_client_read(h, buf, sizeof buf);
        if (r <= 0) break;
        total += r; sec += r;
        int64_t now = esp_timer_get_time();
        if (now - tl >= 1000000) {
            printf("APP wget kBps=%ld total=%ld rssi=%d\n", (long)((int64_t)sec * 1000000 / (now - tl) / 1024), total, wifi_svc_rssi());
            sec = 0; tl = now;
        }
    }
    printf("APP wget done total=%ld secs=%.1f\n", total, (esp_timer_get_time() - t0) / 1e6);
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
}
