// SPDX-License-Identifier: LGPL-2.1-or-later
#include "inst_ble.h"
#include <stdio.h>
#include <string.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inst_engine.h"
#include "pc/pc_midi.h"

static const uint8_t SVC_UUID[16] = { 0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03 };
static const uint8_t CHR_UUID[16] = { 0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77 };
static const uint8_t PRB_SVC[16] = { 0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x04 };
static const uint8_t PRB_UUID[16] = { 0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x78 };
static const uint16_t PRI_SVC = ESP_GATT_UUID_PRI_SERVICE, CHR_DECL = ESP_GATT_UUID_CHAR_DECLARE, CCCD = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t PROPS = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static uint8_t s_val[1], s_ccc[2], s_pval[1], s_pccc[2];
enum { I_SVC, I_DECL, I_VAL, I_CCC, I_PSVC, I_PDECL, I_PVAL, I_PCCC, I_NB };
static const esp_gatts_attr_db_t s_db[I_NB] = {
    [I_SVC] = { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_16, (uint8_t *)&PRI_SVC, ESP_GATT_PERM_READ, 16, 16, (uint8_t *)SVC_UUID } },
    [I_DECL] = { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_16, (uint8_t *)&CHR_DECL, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&PROPS } },
    [I_VAL] = { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_128, (uint8_t *)CHR_UUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 128, 0, s_val } },
    [I_CCC] = { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_16, (uint8_t *)&CCCD, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, s_ccc } },
    [I_PSVC] = { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_16, (uint8_t *)&PRI_SVC, ESP_GATT_PERM_READ, 16, 16, (uint8_t *)PRB_SVC } },
    [I_PDECL] = { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_16, (uint8_t *)&CHR_DECL, ESP_GATT_PERM_READ, 1, 1, (uint8_t *)&PROPS } },
    [I_PVAL] = { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_128, (uint8_t *)PRB_UUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 128, 0, s_pval } },
    [I_PCCC] = { { ESP_GATT_AUTO_RSP }, { ESP_UUID_LEN_16, (uint8_t *)&CCCD, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, s_pccc } },
};
static uint16_t s_h[I_NB], s_conn = 0xFFFF;
static esp_gatt_if_t s_if;
static volatile bool s_pnotify, s_notify;
static volatile uint32_t s_sent;
static volatile int s_adv_ok, s_interval;
static volatile uint32_t s_packets;
static pc_midi_parser_t s_parser;
static esp_bd_addr_t s_peer;
static bool s_retried;

/* flags + complete 128-bit service list + name: 3 + 18 + 9 = 30 bytes (legacy PDU limit 31) */
static const uint8_t s_adv[] = {   /* flash: the raw advertising bytes are copied by the stack */
    0x02, 0x01, 0x06, 0x11, 0x07, 0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03,
                           0x0E, 0x09, 'J', 'u', 'k', 'e', 'D', 'i', 'a', 'l', ' ', 'M', 'I', 'D', 'I' };
static esp_ble_gap_ext_adv_params_t s_advp = {
    .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND, .interval_min = 0x40, .interval_max = 0x60, .channel_map = ADV_CHNL_ALL,
    .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY, .primary_phy = ESP_BLE_GAP_PHY_1M, .max_skip = 0,
    .secondary_phy = ESP_BLE_GAP_PHY_1M, .sid = 0, .scan_req_notif = false, .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .tx_power = EXT_ADV_TX_PWR_NO_PREFERENCE,
};
static esp_ble_gap_ext_adv_t s_ext = { 0, 0, 0 };

bool inst_ble_connected(void) { return s_conn != 0xFFFF; }
int inst_ble_interval_x100(void) { return inst_ble_connected() ? s_interval * 125 : 0; }
uint32_t inst_ble_packets(void) { return s_packets; }
bool inst_ble_subscribed(void) { return s_conn != 0xFFFF && (s_notify || s_pnotify); }
uint32_t inst_ble_sent(void) { return s_sent; }

/* one message as a notification on every MIDI characteristic the central listens on (the standard one; the
 * probe twin too, for the test PC whose PipeWire holds the standard one) */
bool inst_ble_send(const uint8_t *msg, int n, uint16_t ts13)
{
    if (s_conn == 0xFFFF) return false;
    uint8_t pkt[8];
    int len = pc_blemidi_encode(ts13, msg, n, pkt, sizeof pkt);
    bool ok = false;
    if (len && s_notify) ok |= esp_ble_gatts_send_indicate(s_if, s_conn, s_h[I_VAL], (uint16_t)len, pkt, false) == ESP_OK;
    if (len && s_pnotify) ok |= esp_ble_gatts_send_indicate(s_if, s_conn, s_h[I_PVAL], (uint16_t)len, pkt, false) == ESP_OK;
    if (ok) s_sent++;
    return ok;
}

static void gap_cb(esp_gap_ble_cb_event_t e, esp_ble_gap_cb_param_t *p)
{
    switch (e) {
    case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT: esp_ble_gap_config_ext_adv_data_raw(0, sizeof s_adv, s_adv); break;
    case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT: esp_ble_gap_ext_adv_start(1, &s_ext); break;
    case ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT:
        printf("INST ble adv status=%d\n", p->ext_adv_start.status);
        s_adv_ok = p->ext_adv_start.status == 0 ? 1 : -1;
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        if (p->update_conn_params.status && !s_retried && s_conn != 0xFFFF) {   /* 7.5 ms refused: 7.5-15 ms */
            s_retried = true;
            esp_ble_conn_update_params_t cp = { .min_int = 6, .max_int = 12, .latency = 0, .timeout = 400 };
            memcpy(cp.bda, s_peer, sizeof cp.bda);
            esp_ble_gap_update_conn_params(&cp);
        }
        s_interval = p->update_conn_params.conn_int;
        printf("INST ble conn params status=%d interval=%.2f ms latency=%d\n", p->update_conn_params.status,
               p->update_conn_params.conn_int * 1.25, p->update_conn_params.latency);
        break;
    default: break;
    }
}

static void gatts_cb(esp_gatts_cb_event_t e, esp_gatt_if_t gif, esp_ble_gatts_cb_param_t *p)
{
    switch (e) {
    case ESP_GATTS_REG_EVT:
        s_if = gif;
        esp_ble_gap_set_device_name("JukeDial MIDI");
        esp_ble_gatts_create_attr_tab(s_db, gif, I_PSVC, 0);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (p->add_attr_tab.status) { printf("INST ble attr tab failed %d\n", p->add_attr_tab.status); break; }
        if (p->add_attr_tab.svc_inst_id == 0) {               /* the standard MIDI service, then the probe twin */
            memcpy(s_h, p->add_attr_tab.handles, I_PSVC * sizeof s_h[0]);
            esp_ble_gatts_start_service(s_h[I_SVC]);
            esp_ble_gatts_create_attr_tab(&s_db[I_PSVC], s_if, I_NB - I_PSVC, 1);
        } else {
            memcpy(&s_h[I_PSVC], p->add_attr_tab.handles, (I_NB - I_PSVC) * sizeof s_h[0]);
            esp_ble_gatts_start_service(s_h[I_PSVC]);
            esp_ble_gap_ext_adv_set_params(0, &s_advp);
        }
        break;
    case ESP_GATTS_CONNECT_EVT: {
        s_conn = p->connect.conn_id;
        s_interval = p->connect.conn_params.interval;
        pc_midi_reset(&s_parser);
        inst_engine_ble_connected(true);
        printf("INST ble connected interval=%.2f ms\n", p->connect.conn_params.interval * 1.25);
        esp_ble_conn_update_params_t cp = { .min_int = 6, .max_int = 6, .latency = 0, .timeout = 400 };   /* 7.5 ms */
        memcpy(cp.bda, p->connect.remote_bda, sizeof cp.bda);
        memcpy(s_peer, p->connect.remote_bda, sizeof s_peer);
        s_retried = false;
        esp_ble_gap_update_conn_params(&cp);
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
        printf("INST ble disconnected reason=0x%x\n", p->disconnect.reason);
        s_conn = 0xFFFF;
        s_pnotify = s_notify = false;
        inst_engine_ble_connected(false);                     /* no stuck notes */
        esp_ble_gap_ext_adv_start(1, &s_ext);
        break;
    case ESP_GATTS_MTU_EVT: printf("INST ble mtu=%d\n", p->mtu.mtu); break;
    case ESP_GATTS_WRITE_EVT:
        if (p->write.handle == s_h[I_PCCC] && p->write.len == 2) s_pnotify = p->write.value[0] & 1;
        else if (p->write.handle == s_h[I_CCC] && p->write.len == 2) { s_notify = p->write.value[0] & 1; printf("INST ble midi notify=%d\n", s_notify); }
        else if (p->write.handle == s_h[I_VAL] || p->write.handle == s_h[I_PVAL]) {
            int64_t t = esp_timer_get_time();
            s_packets++;
            if (p->write.handle == s_h[I_PVAL] && s_pnotify)   /* probe twin: echo for round-trip timing */
                esp_ble_gatts_send_indicate(s_if, s_conn, p->write.handle, p->write.len, p->write.value, false);
            pc_midi_ev_t ev[32];                              /* in chunks: an MTU-128 packet holds ~60 events */
            pc_blemidi_cur_t cur;
            pc_blemidi_begin(&cur);
            for (int n; (n = pc_blemidi_next(&s_parser, &cur, p->write.value, p->write.len, ev, 32)) > 0;)
                inst_engine_post_ble(ev, n, t);
        }
        break;
    default: break;
    }
}

int inst_ble_start(void)
{
    esp_err_t e;
    esp_bt_controller_config_t bc = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((e = esp_bt_controller_init(&bc))) return printf("INST ble ctrl init %s\n", esp_err_to_name(e)), -1;
    if ((e = esp_bt_controller_enable(ESP_BT_MODE_BLE))) return printf("INST ble ctrl enable %s\n", esp_err_to_name(e)), -1;
    esp_bluedroid_config_t c = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if ((e = esp_bluedroid_init_with_cfg(&c))) return printf("INST ble bluedroid init %s\n", esp_err_to_name(e)), -1;
    if ((e = esp_bluedroid_enable())) return printf("INST ble bluedroid enable %s\n", esp_err_to_name(e)), -1;
    esp_ble_gap_register_callback(gap_cb);
    esp_ble_gatts_register_callback(gatts_cb);
    esp_ble_gatt_set_local_mtu(128);
    esp_ble_gatts_app_register(0x4D);
    for (int i = 0; i < 50 && !s_adv_ok; i++) vTaskDelay(pdMS_TO_TICKS(100));
    return s_adv_ok == 1 ? 0 : -2;
}
