/* "JC Peer LEA": LE Audio unicast sink for the S31 feasibility spike (based on IDF bap/unicast_server).
 * PACS/ASCS sink only. Every SDU is queued to a decoder task on core 1 (LC3 needs the PIE unit), the left channel
 * goes through pc_tone_analyse. One stats line every 5 s. */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_lc3_defs.h"
#include "esp_ble_audio_pacs_api.h"

#include "ble_audio_example_init.h"
#include "ble_audio_example_utils.h"

#include "decoder/impl/esp_lc3_dec.h"
#include "esp_audio_dec.h"
#include "pc/pc_btcodec.h"
#include "peripheral.h"

#define LOC (ESP_BLE_AUDIO_LOCATION_FRONT_LEFT | ESP_BLE_AUDIO_LOCATION_FRONT_RIGHT)
#define CTX (ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED | ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA | ESP_BLE_AUDIO_CONTEXT_TYPE_CONVERSATIONAL)
#define MAX_OCT 155
#define MAX_SDU (2 * MAX_OCT)
#define TONE_N 8192
static float s_ana[2][TONE_N];
static volatile bool s_ana_ready;
static volatile int s_ana_nch;
static volatile uint32_t s_ana_hz;

static uint8_t codec_data[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_DATA(
    ESP_BLE_AUDIO_CODEC_CAP_FREQ_16KHZ | ESP_BLE_AUDIO_CODEC_CAP_FREQ_24KHZ | ESP_BLE_AUDIO_CODEC_CAP_FREQ_32KHZ |
        ESP_BLE_AUDIO_CODEC_CAP_FREQ_48KHZ,
    ESP_BLE_AUDIO_CODEC_CAP_DURATION_10, ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1, 2), 40, MAX_OCT,
    2); /* 2 frames per SDU: one per channel */
static uint8_t codec_meta[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_META(CTX);
static const esp_ble_audio_codec_cap_t codec_cap = ESP_BLE_AUDIO_CODEC_CAP_LC3(codec_data, codec_meta);
static esp_ble_audio_pacs_cap_t cap_sink = { .codec_cap = &codec_cap };

static esp_ble_audio_bap_unicast_server_register_param_t reg_param = { CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT, 0 };

static const esp_ble_audio_bap_qos_cfg_pref_t qos_pref = ESP_BLE_AUDIO_BAP_QOS_CFG_PREF(
    true, ESP_BLE_ISO_PHY_2M, 2, 10, 20000, 40000, 20000, 40000);

static esp_ble_audio_bap_stream_t s_streams[CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT];
static uint8_t ext_adv_data[3 + 4 + 10 + 2 + LOCAL_DEVICE_NAME_LEN];

/* decoder setup, written on stream start, read by the decoder task */
typedef struct { uint32_t hz, frame_us; uint16_t oct; uint8_t nch, blocks; } dcfg_t;
static volatile dcfg_t s_cfg;
static volatile uint32_t s_cfg_gen;

typedef struct { uint16_t len; uint8_t ok; uint8_t data[MAX_SDU]; } sdu_t;
static QueueHandle_t s_q;

/* stats (recv side: ISO task; decode side: decoder task) */
static volatile uint32_t s_sdus, s_bytes, s_lost, s_lost_tot, s_invalid, s_qdrop, s_gap_max_us;
static volatile uint32_t s_dec_ok, s_dec_n, s_dec_us;
static volatile pc_tone_t s_tone = { 0, -200, 0 }, s_tone_r = { 0, -200, 0 };

static int stream_index(const esp_ble_audio_bap_stream_t *st)
{
    for (int i = 0; i < CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT; i++)
        if (st == &s_streams[i]) return i;
    return -1;
}

static void read_cfg(const esp_ble_audio_codec_cfg_t *cc, dcfg_t *o)
{
    esp_ble_audio_codec_cfg_freq_t fq;
    esp_ble_audio_codec_cfg_frame_dur_t fd;
    esp_ble_audio_location_t loc = 0;
    *o = (dcfg_t){ 0, 0, 0, 1, 1 };
    if (!cc) return;
    if (!esp_ble_audio_codec_cfg_get_freq(cc, &fq)) esp_ble_audio_codec_cfg_freq_to_freq_hz(fq, &o->hz);
    if (!esp_ble_audio_codec_cfg_get_frame_dur(cc, &fd)) esp_ble_audio_codec_cfg_frame_dur_to_frame_dur_us(fd, &o->frame_us);
    esp_ble_audio_codec_cfg_get_octets_per_frame(cc, &o->oct);
    esp_ble_audio_codec_cfg_get_chan_allocation(cc, &loc, true);
    esp_ble_audio_codec_cfg_get_frame_blocks_per_sdu(cc, &o->blocks, true);
    int n = __builtin_popcount((unsigned)loc);
    o->nch = n >= 2 ? 2 : 1; /* 0 (mono default) or 1 location -> 1 channel */
    if (!o->blocks) o->blocks = 1;
    (void)loc;
}

static void print_cfg(const char *what, int idx, const esp_ble_audio_codec_cfg_t *cc)
{
    dcfg_t c;
    esp_ble_audio_location_t loc = 0;
    read_cfg(cc, &c);
    if (cc) esp_ble_audio_codec_cfg_get_chan_allocation(cc, &loc, true);
    printf("PEER lea stream %d %s rate=%lu frame_us=%lu octets=%u ch=%u loc=0x%lx blocks=%u sdu=%u\n", idx, what,
           (unsigned long)c.hz, (unsigned long)c.frame_us, c.oct, c.nch, (unsigned long)loc, c.blocks,
           (unsigned)(c.oct * c.nch * c.blocks));
}

/* ---- ASCS server callbacks ---- */
static int config_cb(esp_ble_conn_t *conn, const esp_ble_audio_bap_ep_t *ep, esp_ble_audio_dir_t dir,
                     const esp_ble_audio_codec_cfg_t *codec_cfg, esp_ble_audio_bap_stream_t **stream,
                     esp_ble_audio_bap_qos_cfg_pref_t *const pref, esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    *stream = NULL;
    for (int i = 0; i < CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT; i++)
        if (s_streams[i].conn == NULL) { *stream = &s_streams[i]; break; }
    if (dir != ESP_BLE_AUDIO_DIR_SINK || *stream == NULL) {
        printf("PEER lea config rejected (dir=%d, no free stream)\n", dir);
        *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_NO_MEM, ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
        return -ENOMEM;
    }
    print_cfg("config", stream_index(*stream), codec_cfg);
    *pref = qos_pref;
    return 0;
}

static int reconfig_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_dir_t dir,
                       const esp_ble_audio_codec_cfg_t *codec_cfg, esp_ble_audio_bap_qos_cfg_pref_t *const pref,
                       esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    print_cfg("reconfig", stream_index(stream), codec_cfg);
    *pref = qos_pref;
    return 0;
}

static int qos_cb(esp_ble_audio_bap_stream_t *stream, const esp_ble_audio_bap_qos_cfg_t *qos, esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    printf("PEER lea stream %d qos interval=%lu framing=%u phy=%u sdu=%u rtn=%u latency=%u pd=%lu\n", stream_index(stream),
           (unsigned long)qos->interval, qos->framing, qos->phy, qos->sdu, qos->rtn, qos->latency, (unsigned long)qos->pd);
    return 0;
}

static int enable_cb(esp_ble_audio_bap_stream_t *stream, const uint8_t meta[], size_t meta_len, esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    *rsp = ESP_BLE_AUDIO_BAP_ASCS_RSP(ESP_BLE_AUDIO_BAP_ASCS_RSP_CODE_SUCCESS, ESP_BLE_AUDIO_BAP_ASCS_REASON_NONE);
    return 0;
}

static int start_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_t *rsp) { return 0; }
static int metadata_cb(esp_ble_audio_bap_stream_t *stream, const uint8_t meta[], size_t meta_len, esp_ble_audio_bap_ascs_rsp_t *rsp) { return 0; }
static int disable_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_t *rsp) { return 0; }
static int stop_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_t *rsp) { return 0; }
static int release_cb(esp_ble_audio_bap_stream_t *stream, esp_ble_audio_bap_ascs_rsp_t *rsp)
{
    printf("PEER lea stream %d released\n", stream_index(stream));
    return 0;
}

static const esp_ble_audio_bap_unicast_server_cb_t server_cb = {
    .config = config_cb, .reconfig = reconfig_cb, .qos = qos_cb, .enable = enable_cb, .start = start_cb,
    .metadata = metadata_cb, .disable = disable_cb, .stop = stop_cb, .release = release_cb,
};

/* ---- stream callbacks ---- */
static void stream_enabled_cb(esp_ble_audio_bap_stream_t *stream)
{
    /* the unicast server starts sink ASEs itself */
    esp_err_t e = esp_ble_audio_bap_stream_start(stream);
    printf("PEER lea stream %d enabled, start=%d\n", stream_index(stream), e);
}

static bool s_seen;
static uint16_t s_last_seq;
static int64_t s_last_t;

static void stream_started_cb(esp_ble_audio_bap_stream_t *stream)
{
    dcfg_t c;
    read_cfg(stream->codec_cfg, &c);
    s_cfg = c;
    s_cfg_gen++;
    s_seen = false;
    print_cfg("started", stream_index(stream), stream->codec_cfg);
}

static void stream_stopped_cb(esp_ble_audio_bap_stream_t *stream, uint8_t reason)
{
    printf("PEER lea stream %d stopped reason=0x%02x\n", stream_index(stream), reason);
}

static void stream_recv_cb(esp_ble_audio_bap_stream_t *stream, const esp_ble_iso_recv_info_t *info, const uint8_t *data, uint16_t len)
{
    static sdu_t s; /* ISO task only */
    int64_t now = esp_timer_get_time();
    if (stream_index(stream) != 0) return; /* spike: stream 0 only (one CIS) */
    if (s_seen) {
        uint16_t gap = (uint16_t)(info->seq_num - s_last_seq);
        if (gap > 1 && gap < 1000) { s_lost += gap - 1; s_lost_tot += gap - 1; }
        uint32_t dt = (uint32_t)(now - s_last_t);
        if (dt > s_gap_max_us) s_gap_max_us = dt;
    }
    s_seen = true;
    s_last_seq = info->seq_num;
    s_last_t = now;
    s_sdus++;
    if (info->flags & ESP_BLE_ISO_FLAGS_LOST) { s_lost++; s_lost_tot++; }
    s.ok = (info->flags & ESP_BLE_ISO_FLAGS_VALID) && !(info->flags & ESP_BLE_ISO_FLAGS_ERROR) && len && len <= MAX_SDU;
    if (!s.ok) s_invalid++;
    s.len = s.ok ? len : 0;
    if (s.ok) { memcpy(s.data, data, len); s_bytes += len; }
    if (xQueueSend(s_q, &s, 0) != pdTRUE) s_qdrop++;
}

static esp_ble_audio_bap_stream_ops_t stream_ops = {
    .enabled = stream_enabled_cb, .started = stream_started_cb, .stopped = stream_stopped_cb, .recv = stream_recv_cb,
};

/* ---- decoder task (core 1) ---- */
static void dec_task(void *arg)
{
    static sdu_t s;
    static int16_t pcm[480];
    static float tone[2][TONE_N];
    int tn[2] = { 0, 0 };
    uint32_t gen = 0;
    dcfg_t c = { 0 };
    esp_audio_dec_handle_t dec[2] = { 0 };
    esp_lc3_dec_register();
    for (;;) {
        if (xQueueReceive(s_q, &s, portMAX_DELAY) != pdTRUE) continue;
        if (gen != s_cfg_gen) { /* (re)open: one mono decoder per channel */
            gen = s_cfg_gen;
            c = s_cfg;
            for (int i = 0; i < 2; i++) if (dec[i]) { esp_audio_dec_close(dec[i]); dec[i] = NULL; }
            for (int i = 0; i < c.nch; i++) {
                esp_lc3_dec_cfg_t lc = ESP_LC3_DEC_CONFIG_DEFAULT();
                lc.sample_rate = c.hz; lc.channel = 1; lc.frame_dms = c.frame_us == 7500 ? 75 : 100; lc.nbyte = c.oct;
                esp_audio_dec_cfg_t dc = { .type = ESP_AUDIO_TYPE_LC3, .cfg = &lc, .cfg_sz = sizeof lc };
                esp_audio_err_t r = esp_audio_dec_open(&dc, &dec[i]);
                if (r != ESP_AUDIO_ERR_OK) { printf("PEER lea lc3 dec open failed %d\n", r); dec[i] = NULL; }
            }
            tn[0] = tn[1] = 0;
        }
        if (!dec[0] || !c.oct) continue;
        /* SDU = blocks x channels x octets, channels in location order (left first) */
        int nfr = s.ok ? s.len / c.oct : c.nch * c.blocks;
        bool good = s.ok && s.len == c.oct * c.nch * c.blocks;
        int64_t t0 = esp_timer_get_time();
        for (int f = 0; f < nfr; f++) {
            int ch = f % c.nch;
            if (!dec[ch]) continue;
            esp_audio_dec_in_raw_t in = { .buffer = s.data + f * c.oct, .len = c.oct };
            if (!good) { in.frame_recover = ESP_AUDIO_DEC_RECOVERY_PLC; in.len = c.oct; }
            esp_audio_dec_out_frame_t o = { .buffer = (uint8_t *)pcm, .len = sizeof pcm };
            if (esp_audio_dec_process(dec[ch], &in, &o) != ESP_AUDIO_ERR_OK) { good = false; continue; }
            int n = (int)(o.decoded_size / 2);
            int ci = c.nch == 1 ? 0 : ch;
            for (int k = 0; k < n && tn[ci] < TONE_N; k++) tone[ci][tn[ci]++] = pcm[k] / 32768.0f;
        }
        s_dec_us += (uint32_t)(esp_timer_get_time() - t0);
        s_dec_n++;
        if (good) s_dec_ok++;
        if (tn[0] >= TONE_N && (c.nch == 1 || tn[1] >= TONE_N)) {
            /* the sine fit takes long: the stats task does it on a copy, so the decoder keeps up with the queue */
            if (!s_ana_ready) {
                memcpy(s_ana[0], tone[0], sizeof tone[0]);
                if (c.nch == 2) memcpy(s_ana[1], tone[1], sizeof tone[1]);
                s_ana_nch = c.nch;
                s_ana_hz = c.hz;
                s_ana_ready = true;
            }
            tn[0] = tn[1] = 0;
        }
    }
}

static void stats_task(void *arg)
{
    int64_t t_prev = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (s_ana_ready) {
            pc_tone_t t;
            pc_tone_analyse(s_ana[0], TONE_N, (float)s_ana_hz, &t);
            s_tone = t;
            if (s_ana_nch == 2) { pc_tone_analyse(s_ana[1], TONE_N, (float)s_ana_hz, &t); s_tone_r = t; }
            s_ana_ready = false;
        }
        int64_t now = esp_timer_get_time();
        double dt = (now - t_prev) / 1e6;
        t_prev = now;
        uint32_t sdus = s_sdus, bytes = s_bytes, lost = s_lost, inval = s_invalid, ok = s_dec_ok, dn = s_dec_n, dus = s_dec_us;
        uint32_t gap = s_gap_max_us, qd = s_qdrop;
        s_sdus = s_bytes = s_lost = s_invalid = s_dec_ok = s_dec_n = s_dec_us = s_gap_max_us = s_qdrop = 0;
        pc_tone_t t = s_tone, tr = s_tone_r;
        printf("PEER lea stats sdus/s=%.1f kbps=%.1f lost=%lu (total %lu) invalid=%lu decoded=%.1f%% tone=%.2f Hz %.2f dBFS "
               "snr=%.1f dB dec=%.1f%% heap=%u gap_max_ms=%.1f qdrop=%lu ch=%u right=%.2f Hz %.2f dBFS snr=%.1f dB\n",
               sdus / dt, bytes * 8 / dt / 1000, (unsigned long)lost, (unsigned long)s_lost_tot, (unsigned long)inval,
               dn ? 100.0 * ok / dn : 0.0, t.freq_hz, t.level_db, t.snr_db, dus / (dt * 1e4),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), gap / 1000.0, (unsigned long)qd, s_cfg.nch,
               tr.freq_hz, tr.level_db, tr.snr_db);
        s_tone = (pc_tone_t){ 0, -200, 0 };
        s_tone_r = (pc_tone_t){ 0, -200, 0 };
    }
}

/* ---- advertising / GAP ---- */
static void build_ext_adv_data(void)
{
    size_t i = 0;
    ext_adv_data[i++] = 0x02;
    ext_adv_data[i++] = EXAMPLE_AD_TYPE_FLAGS;
    ext_adv_data[i++] = EXAMPLE_AD_FLAGS_GENERAL | EXAMPLE_AD_FLAGS_NO_BREDR;
    ext_adv_data[i++] = 0x03;
    ext_adv_data[i++] = EXAMPLE_AD_TYPE_UUID16_SOME;
    ext_adv_data[i++] = ESP_BLE_AUDIO_UUID_ASCS_VAL & 0xFF;
    ext_adv_data[i++] = (ESP_BLE_AUDIO_UUID_ASCS_VAL >> 8) & 0xFF;
    ext_adv_data[i++] = 0x09;
    ext_adv_data[i++] = EXAMPLE_AD_TYPE_SERVICE_DATA16;
    ext_adv_data[i++] = ESP_BLE_AUDIO_UUID_ASCS_VAL & 0xFF;
    ext_adv_data[i++] = (ESP_BLE_AUDIO_UUID_ASCS_VAL >> 8) & 0xFF;
    ext_adv_data[i++] = ESP_BLE_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED;
    ext_adv_data[i++] = CTX & 0xFF;
    ext_adv_data[i++] = (CTX >> 8) & 0xFF;
    ext_adv_data[i++] = 0; /* no source contexts */
    ext_adv_data[i++] = 0;
    ext_adv_data[i++] = 0x00; /* metadata length */
    ext_adv_data[i++] = LOCAL_DEVICE_NAME_LEN + 1;
    ext_adv_data[i++] = EXAMPLE_AD_TYPE_NAME_COMPLETE;
    memcpy(&ext_adv_data[i], LOCAL_DEVICE_NAME, LOCAL_DEVICE_NAME_LEN);
}

static void gap_cb(esp_ble_audio_gap_app_event_t *ev)
{
    switch (ev->type) {
    case ESP_BLE_AUDIO_GAP_EVENT_ACL_CONNECT:
        printf("PEER lea connected status=%d handle=%u\n", ev->acl_connect.status, ev->acl_connect.conn_handle);
        if (ev->acl_connect.status) ext_adv_start(ext_adv_data, sizeof ext_adv_data);
        break;
    case ESP_BLE_AUDIO_GAP_EVENT_ACL_DISCONNECT:
        printf("PEER lea disconnected handle=%u reason=0x%02x\n", ev->acl_disconnect.conn_handle, ev->acl_disconnect.reason);
        ext_adv_start(ext_adv_data, sizeof ext_adv_data);
        break;
    case ESP_BLE_AUDIO_GAP_EVENT_SECURITY_CHANGE:
        printf("PEER lea security status=%d level=%u bonded=%u\n", ev->security_change.status,
               ev->security_change.sec_level, ev->security_change.bonded);
        break;
    default:
        break;
    }
}

static void gatt_cb(esp_ble_audio_gatt_app_event_t *ev)
{
    if (ev->type == ESP_BLE_AUDIO_GATT_EVENT_GATT_MTU_CHANGE) {
        printf("PEER lea mtu=%u\n", ev->gatt_mtu_change.mtu);
        if (ev->gatt_mtu_change.mtu >= ESP_BLE_AUDIO_ATT_MTU_MIN) esp_ble_audio_gattc_disc_start(ev->gatt_mtu_change.conn_handle);
    }
}

#define CHECK(x) do { esp_err_t _e = (x); if (_e) { printf("PEER lea init failed: %s = %d\n", #x, _e); return; } } while (0)

void app_main(void)
{
    const esp_ble_audio_pacs_register_param_t pacs_param = { .snk_pac = true, .snk_loc = true };
    esp_ble_audio_init_info_t info = { .gap_cb = gap_cb, .gatt_cb = gatt_cb };
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_q = xQueueCreate(16, sizeof(sdu_t));
    xTaskCreatePinnedToCore(dec_task, "lea_dec", 8192, NULL, configMAX_PRIORITIES - 5, NULL, 1);
    xTaskCreatePinnedToCore(stats_task, "lea_stats", 4096, NULL, 1, NULL, 1);   /* core 1, low: the sine fit must not delay the BT host */

    CHECK(bluetooth_init());
    CHECK(app_host_init());
    CHECK(esp_ble_audio_common_init(&info));
    CHECK(esp_ble_audio_pacs_register(&pacs_param));
    CHECK(esp_ble_audio_bap_unicast_server_register(&reg_param));
    CHECK(esp_ble_audio_bap_unicast_server_register_cb(&server_cb));
    CHECK(esp_ble_audio_pacs_cap_register(ESP_BLE_AUDIO_DIR_SINK, &cap_sink));
    for (int i = 0; i < CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT; i++) esp_ble_audio_bap_stream_cb_register(&s_streams[i], &stream_ops);
    CHECK(esp_ble_audio_pacs_set_location(ESP_BLE_AUDIO_DIR_SINK, LOC));
    CHECK(esp_ble_audio_pacs_set_supported_contexts(ESP_BLE_AUDIO_DIR_SINK, CTX));
    CHECK(esp_ble_audio_pacs_set_available_contexts(ESP_BLE_AUDIO_DIR_SINK, CTX));
    CHECK(esp_ble_audio_common_start(NULL));
    CHECK(set_device_name());
    build_ext_adv_data();
    CHECK(ext_adv_start(ext_adv_data, sizeof ext_adv_data));
    printf("PEER lea advertising as \"%s\" (sink: LC3 16/24/32/48 kHz, 10 ms, 1-2 ch, 40-%d octets)\n", LOCAL_DEVICE_NAME, MAX_OCT);
}
