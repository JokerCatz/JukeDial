// SPDX-License-Identifier: LGPL-2.1-or-later
/* Bluetooth, Auracast broadcast source (BAP broadcast source + PBP): the LC3 encoder task, the advertising set, the sink hook. broadcast_on / broadcast_off
 * stay in bt_svc.c (mode_task inlines broadcast_off). Split out of bt_svc.c; shared state in bt_priv.h. */
#include "bt_priv.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_CLASSIC_ENABLED



/* ===================== Auracast broadcast (BAP broadcast source + PBP): our audio -> anyone listening ===================== */
#if CONFIG_BT_BAP_BROADCAST_SOURCE
/* quality (cfg cast_q): LC3 needs the PIE unit, which only core 1 has - FreeRTOS moves the encoder there, next to
 * the player, so the lighter options exist (measured 2026-11-09: 48 kHz stereo + MP3 + fx overran core 1).
 * 0 = 48 kHz stereo (2 BIS, 48_4: 120 B, the default), 1 = retired (was mono; reads as 0), 2 = 24 kHz stereo (2 BIS, 24_2: 60 B),
 * 3 = 48 kHz stereo high (2 BIS, 48_6: 155 B). 2026-11-13: 48 kHz stereo + MP3 + fx = 44 % of core 1 at 320 MHz, the mono default is gone. */
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_4_1_DEFINE(s_cast_48, ESP_BLE_AUDIO_LOCATION_FRONT_LEFT | ESP_BLE_AUDIO_LOCATION_FRONT_RIGHT,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_24_2_1_DEFINE(s_cast_24, ESP_BLE_AUDIO_LOCATION_FRONT_LEFT | ESP_BLE_AUDIO_LOCATION_FRONT_RIGHT,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_BROADCAST_PRESET_48_6_1_DEFINE(s_cast_48h, ESP_BLE_AUDIO_LOCATION_FRONT_LEFT | ESP_BLE_AUDIO_LOCATION_FRONT_RIGHT,
                                                     ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
int s_cast_q, s_cast_rate, s_cast_nbyte;
bt_cs_t s_cs[2];
esp_ble_audio_bap_broadcast_source_t *s_cast_src;
bool s_cast_inited;
volatile bool s_cast_on;
SemaphoreHandle_t s_adv_sem;
static esp_bt_status_t s_adv_st;
esp_timer_handle_t s_cast_tick;
TaskHandle_t s_cast_task;

static int cs_index(esp_ble_audio_bap_stream_t *st) { return st == &s_cs[0].stream ? 0 : st == &s_cs[1].stream ? 1 : -1; }
static void cs_started(esp_ble_audio_bap_stream_t *st) { int i = cs_index(st); if (i >= 0) s_cs[i].ready = true; printf("APP bt cast stream %d started\n", i); }
static void cs_stopped(esp_ble_audio_bap_stream_t *st, uint8_t r) { int i = cs_index(st); if (i >= 0) s_cs[i].ready = false; printf("APP bt cast stream %d stopped %u\n", i, r); }
static esp_ble_audio_bap_stream_ops_t s_cs_ops = { .started = cs_started, .stopped = cs_stopped };

void cast_gap_cb(esp_gap_ble_cb_event_t ev, esp_ble_gap_cb_param_t *p)
{
    switch (ev) {
    case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT: s_adv_st = p->ext_adv_set_params.status; xSemaphoreGive(s_adv_sem); break;
    case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT: s_adv_st = p->ext_adv_data_set.status; xSemaphoreGive(s_adv_sem); break;
    case ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT: s_adv_st = p->ext_adv_start.status; xSemaphoreGive(s_adv_sem); break;
    case ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT: s_adv_st = p->ext_adv_stop.status; xSemaphoreGive(s_adv_sem); break;
    case ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT: s_adv_st = p->peroid_adv_set_params.status; xSemaphoreGive(s_adv_sem); break;
    case ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT: s_adv_st = p->period_adv_data_set.status; xSemaphoreGive(s_adv_sem); break;
    case ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT: s_adv_st = p->period_adv_start.status; xSemaphoreGive(s_adv_sem); break;
    case ESP_GAP_BLE_PERIODIC_ADV_STOP_COMPLETE_EVT: s_adv_st = p->period_adv_stop.status; xSemaphoreGive(s_adv_sem); break;
    case ESP_GAP_BLE_EXT_ADV_SET_REMOVE_COMPLETE_EVT: s_adv_st = p->ext_adv_remove.status; xSemaphoreGive(s_adv_sem); break;
    default: break;
    }
}
#define ADV_CALL(x) do { esp_err_t _e = (x); if (_e || xSemaphoreTake(s_adv_sem, pdMS_TO_TICKS(2000)) != pdTRUE || s_adv_st) { \
        printf("APP bt cast adv %s failed e=%d st=%d\n", #x, _e, s_adv_st); return -1; } } while (0)

int cast_adv_start(void)
{
    static esp_ble_gap_ext_adv_params_t ep = {
        .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_NONCONN_NONSCANNABLE_UNDIRECTED,
        .interval_min = ESP_BLE_GAP_ADV_ITVL_MS(200), .interval_max = ESP_BLE_GAP_ADV_ITVL_MS(200),
        .channel_map = ADV_CHNL_ALL, .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        .primary_phy = ESP_BLE_GAP_PHY_1M, .max_skip = 0, .secondary_phy = ESP_BLE_GAP_PHY_2M,
        .sid = 0, .scan_req_notif = false, .own_addr_type = BLE_ADDR_TYPE_PUBLIC, .tx_power = 127,
    };
    static esp_ble_gap_periodic_adv_params_t pp = {
        .interval_min = ESP_BLE_GAP_PERIODIC_ADV_ITVL_MS(100), .interval_max = ESP_BLE_GAP_PERIODIC_ADV_ITVL_MS(100), .properties = 0,
    };
    /* extended adv: Broadcast Audio Announcement (UUID + broadcast id), Public Broadcast Announcement (Auracast,
     * high quality, unencrypted), Broadcast Name */
    uint8_t ext[64];
    int n = 0;
    ext[n++] = 6; ext[n++] = 0x16;
    ext[n++] = ESP_BLE_AUDIO_UUID_BROADCAST_AUDIO_VAL & 0xFF; ext[n++] = ESP_BLE_AUDIO_UUID_BROADCAST_AUDIO_VAL >> 8;
    ext[n++] = CAST_ID & 0xFF; ext[n++] = (CAST_ID >> 8) & 0xFF; ext[n++] = (CAST_ID >> 16) & 0xFF;
    NET_BUF_SIMPLE_DEFINE(pba, ESP_BLE_AUDIO_PBP_MIN_PBA_SIZE + 8);
    if (esp_ble_audio_pbp_get_announcement(NULL, 0, ESP_BLE_AUDIO_PBP_ANNOUNCEMENT_FEATURE_HIGH_QUALITY, &pba) == ESP_OK) {
        ext[n++] = (uint8_t)(1 + pba.len); ext[n++] = 0x16;            /* pba holds the 0x1856 UUID + features + metadata */
        memcpy(ext + n, pba.data, pba.len); n += pba.len;
    }
    ext[n++] = sizeof CAST_NAME; ext[n++] = 0x30;                       /* Broadcast Name */
    memcpy(ext + n, CAST_NAME, sizeof CAST_NAME - 1); n += sizeof CAST_NAME - 1;
    /* periodic adv: BASE */
    NET_BUF_SIMPLE_DEFINE(base, 128);
    if (esp_ble_audio_bap_broadcast_source_get_base(s_cast_src, &base)) return -1;
    uint8_t per[140];
    int m = 0;
    per[m++] = (uint8_t)(1 + base.len); per[m++] = 0x16;
    memcpy(per + m, base.data, base.len); m += base.len;
    static esp_ble_gap_ext_adv_t inst[1] = { { CAST_ADV, 0, 0 } };
    ADV_CALL(esp_ble_gap_ext_adv_set_params(CAST_ADV, &ep));
    ADV_CALL(esp_ble_gap_config_ext_adv_data_raw(CAST_ADV, (uint16_t)n, ext));
    ADV_CALL(esp_ble_gap_periodic_adv_set_params(CAST_ADV, &pp));
#if CONFIG_BT_BLE_FEAT_PERIODIC_ADV_ENH
    ADV_CALL(esp_ble_gap_config_periodic_adv_data_raw(CAST_ADV, (uint16_t)m, per, false));
    ADV_CALL(esp_ble_gap_periodic_adv_start(CAST_ADV, true));
#else
    ADV_CALL(esp_ble_gap_config_periodic_adv_data_raw(CAST_ADV, (uint16_t)m, per));
    ADV_CALL(esp_ble_gap_periodic_adv_start(CAST_ADV));
#endif
    ADV_CALL(esp_ble_gap_ext_adv_start(1, inst));
    return 0;
}

void cast_tick_cb(void *arg) { (void)arg; if (s_cast_task) xTaskNotifyGive(s_cast_task); }

/* every 10 ms: 480 frames from the ring -> left / right LC3 (48_4: 120 bytes each, 48_6: 155) -> one BIS each */
void cast_task(void *arg)
{
    (void)arg;
    esp_audio_enc_handle_t enc[2] = { 0 };
    int open_q = -1;
    static int16_t ch[2][480];
    static uint8_t out[2][160];                    /* >= the biggest SDU (155) */
    uint32_t under = 0, n = 0, enc_us = 0, enc_n = 0, send_err = 0, gap_max = 0;   /* send_err, gap_max: since the last report line */
    int64_t last_tick = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_cast_on) { last_tick = 0; continue; }
        int64_t now = esp_timer_get_time();
        if (last_tick && (uint32_t)(now - last_tick) > gap_max) gap_max = (uint32_t)(now - last_tick);
        last_tick = now;
        if (open_q != s_cast_q) {                      /* (re)open the encoders for this quality */
            for (int c = 0; c < 2; c++) if (enc[c]) { esp_audio_enc_close(enc[c]); enc[c] = NULL; }
            for (int c = 0; c < 2; c++) {
                esp_lc3_enc_config_t lc = ESP_LC3_ENC_CONFIG_DEFAULT();
                lc.sample_rate = (uint32_t)s_cast_rate;
                lc.channel = 1;
                lc.frame_dms = 100;
                lc.nbyte = (uint16_t)s_cast_nbyte;
                esp_audio_enc_config_t ec = { .type = ESP_AUDIO_TYPE_LC3, .cfg = &lc, .cfg_sz = sizeof lc };
                esp_lc3_enc_register();
                if (esp_audio_enc_open(&ec, &enc[c]) != ESP_AUDIO_ERR_OK) printf("APP bt cast lc3 open failed\n");
            }
            open_q = s_cast_q;
        }
        int fr = s_cast_rate / 100;                    /* 10 ms */
        bool d0 = s_hw == s_hr;                        /* the block starts dry (one underflow, as before) */
        for (int i = 0; i < fr; i++) {
            int16_t l = 0, r = 0;
            if (s_hw == s_hr) { if (!i) under++; }
            else {
                uint32_t k = s_hr % HP_RING;
                l = s_hp_ring[2 * k];
                r = s_hp_ring[2 * k + 1];
                __atomic_store_n(&s_hr, s_hr + 1, __ATOMIC_RELEASE);
            }
            ch[0][i] = l;
            ch[1][i] = r;
        }
        sink_block(d0);
        for (int c = 0; c < 2; c++) {
            if (!enc[c] || !s_cs[c].ready) continue;
            esp_audio_enc_in_frame_t in = { .buffer = (uint8_t *)ch[c], .len = (uint32_t)(fr * 2) };
            esp_audio_enc_out_frame_t o = { .buffer = out[c], .len = sizeof out[c] };
            int64_t te = esp_timer_get_time();
            esp_audio_err_t er = esp_audio_enc_process(enc[c], &in, &o);
            enc_us += (uint32_t)(esp_timer_get_time() - te);
            enc_n++;
            if (er != ESP_AUDIO_ERR_OK) continue;
            if (esp_ble_audio_bap_stream_send(&s_cs[c].stream, out[c], (uint16_t)o.encoded_bytes, s_cs[c].seq++)) send_err++;
        }
        if (++n % 1000 == 0) {
            printf("APP bt cast frames=%lu underflows=%lu ring=%lu lc3_us_per_ch=%lu send_err=%lu gap_max=%lu rsqdrop=%lu\n", (unsigned long)n, (unsigned long)under,
                   (unsigned long)(s_hw - s_hr), (unsigned long)(enc_n ? enc_us / enc_n : 0), (unsigned long)send_err, (unsigned long)gap_max,
                   (unsigned long)s_hp_rs.dropped_total);
            enc_us = enc_n = 0;
            send_err = gap_max = 0;
        }
    }
}

int cast_create(void)
{
    static uint8_t left[] = { ESP_BLE_AUDIO_CODEC_DATA(ESP_BLE_AUDIO_CODEC_CFG_CHAN_ALLOC, BT_BYTES_LIST_LE32(ESP_BLE_AUDIO_LOCATION_FRONT_LEFT)) };
    static uint8_t right[] = { ESP_BLE_AUDIO_CODEC_DATA(ESP_BLE_AUDIO_CODEC_CFG_CHAN_ALLOC, BT_BYTES_LIST_LE32(ESP_BLE_AUDIO_LOCATION_FRONT_RIGHT)) };
    esp_ble_audio_bap_broadcast_source_stream_param_t sp[2];
    s_cast_q = pc_cast_quality(cfg_int("cast_q", PC_CAST_Q_DEFAULT));
    s_cast_rate = pc_cast_q_rate(s_cast_q);
    s_cast_nbyte = pc_cast_q_nbyte(s_cast_q);
    struct bt_bap_lc3_preset *pr = s_cast_q == 0 ? &s_cast_24 : s_cast_q == 2 ? &s_cast_48h : &s_cast_48;
    esp_ble_audio_bap_broadcast_source_subgroup_param_t sg = { .params_count = 2, .params = sp, .codec_cfg = &pr->codec_cfg };
    for (int i = 0; i < 2; i++) {
        sp[i].stream = &s_cs[i].stream;
        sp[i].data = i ? right : left;
        sp[i].data_len = i ? sizeof right : sizeof left;
        esp_ble_audio_bap_stream_cb_register(sp[i].stream, &s_cs_ops);
    }
    printf("APP bt cast quality=%d rate=%d ch=2 bytes=%d\n", s_cast_q, s_cast_rate, s_cast_nbyte);
    esp_ble_audio_bap_broadcast_source_param_t cp = { .params_count = 1, .params = &sg, .qos = &pr->qos,
                                                      .encryption = false, .packing = ESP_BLE_ISO_PACKING_SEQUENTIAL };
    return esp_ble_audio_bap_broadcast_source_create(&cp, &s_cast_src);
}

void ui_cast_sink(void *arg)
{
    bool on = (bool)(intptr_t)arg;
    s_hw = s_hr = 0;
    s_sink_sess++;                                       /* fix 2 (N2): a new sink session */
    hp_rsq_sink(on);
    if (on) audio_svc_set_remote_volume(NULL);           /* defence in depth: a broadcast is never at the headphones' unity */
    vol_out_switch(on ? PC_VOUT_CAST : PC_VOUT_WIRED, NULL, on ? hp_sink : NULL);   /* BTVOL: the broadcast keeps the wired value (as before the split); the same ordered path */
    player_ctl_changed();
}

int bt_svc_lean_cast(void)                              /* the broadcast-radio boot (cast_mode.c): the stack and the broadcast, after Wi-Fi is up; 0 ok */
{
    return broadcast_on();
}
bool bt_svc_lean_cast_on(void) { return s_cast_on; }
const char *bt_svc_cast_name(void) { return CAST_NAME; }
#else
int bt_svc_lean_cast(void) { return -1; }
bool bt_svc_lean_cast_on(void) { return false; }
const char *bt_svc_cast_name(void) { return ""; }
#endif

#endif
