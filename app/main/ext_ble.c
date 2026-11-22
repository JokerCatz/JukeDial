// SPDX-License-Identifier: LGPL-2.1-or-later
/* 外接模式 T3: BLE HID of the external mode (ext_ble.h). Bluedroid with the 4.2 API compiled out in this build: advertising uses the extended-advertising calls
 * with a legacy connectable PDU (as inst_ble.c). Statics are small (this file is built with -msmall-data-limit=0: PSRAM). */
#include "esp_attr.h"
#include "ext_ble.h"
#include "pc/pc_exthid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_bt.h"
#include "esp_mac.h"
#include "bsp/bsp_pins.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hidd.h"
#include "esp_hidd_gatts.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "pc/pc_bleadv.h"

/* the same report map as the USB HID (pc_exthid_map: consumer 1, keyboard 2, mouse 3) */
const char *ext_serial(void)
{
    static EXT_RAM_BSS_ATTR char s[12];   /* PSRAM: 0 new internal statics */
    if (!s[0]) {
        uint8_t m[6] = {0};
        esp_efuse_mac_get_default(m);       /* read only */
#ifdef BSP_DAC_CS43131
        snprintf(s, sizeof s, "JD-S-%02X%02X%02X", m[3], m[4], m[5]);
#else
        snprintf(s, sizeof s, "JD-R-%02X%02X%02X", m[3], m[4], m[5]);
#endif
    }
    return s;
}
static const esp_hid_raw_report_map_t s_maps[] = { { pc_exthid_map, PC_EXTHID_MAP_LEN } };
static const esp_hid_device_config_t s_cfg = { .vendor_id = 0x303A, .product_id = 0x4003, .version = 0x0100, .device_name = "JukeDial Keys", .manufacturer_name = "JukeDial",
                                         .serial_number = "", .report_maps = (esp_hid_raw_report_map_t *)s_maps, .report_maps_len = 1 };
static EXT_RAM_BSS_ATTR esp_hid_device_config_t s_run;   /* s_cfg + the per-device serial (PSRAM: no internal .data) */
/* flags, appearance keyboard (0x03C1), complete 16-bit list: HID service 0x1812, complete name */
static const uint8_t s_adv[] = { 0x02, 0x01, 0x06, 0x03, 0x19, 0xC1, 0x03, 0x03, 0x03, 0x12, 0x18, 0x0E, 0x09, 'J', 'u', 'k', 'e', 'D', 'i', 'a', 'l', ' ', 'K', 'e', 'y', 's' };
static const esp_ble_gap_ext_adv_params_t s_advp = {
    .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND, .interval_min = 0x30, .interval_max = 0x50, .channel_map = ADV_CHNL_ALL,
    .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY, .primary_phy = ESP_BLE_GAP_PHY_1M, .max_skip = 0, .secondary_phy = ESP_BLE_GAP_PHY_1M,
    .sid = 0, .scan_req_notif = false, .own_addr_type = BLE_ADDR_TYPE_RANDOM, .tx_power = EXT_ADV_TX_PWR_NO_PREFERENCE,
};
static esp_ble_gap_ext_adv_t s_ext = { 0, 0, 0 };
static esp_hidd_dev_t *s_dev;
static volatile bool s_ready, s_conn, s_adv_cfg, s_advertising;
static pc_bleadv_t s_av;                      /* the start / stop bookkeeping (pc_bleadv): one command outstanding at a time */
static esp_bd_addr_t s_own;                   /* this mode's own static random address (NVS ext/bleaddr, made once): not the public address the player boot uses */
static esp_bd_addr_t s_bda;
static char s_peer[12];

bool ext_ble_ready(void) { return s_ready; }
bool ext_ble_advertising(void) { return s_advertising; }
bool ext_ble_connected(void) { return s_conn; }
const char *ext_ble_peer(void) { return s_conn ? s_peer : ""; }
int ext_ble_bonded(void) { return s_ready ? esp_ble_get_bond_device_num() : 0; }

static void adv_apply(void)
{
    if (!s_ready || !s_adv_cfg) return;
    int a = pc_bleadv_next(&s_av);
    if (a == PC_ADV_START) esp_ble_gap_ext_adv_start(1, &s_ext);
    else if (a == PC_ADV_STOP) esp_ble_gap_ext_adv_stop(1, &s_ext.instance);
    s_advertising = s_av.adv;
}
void ext_ble_advertise(bool on)
{
    pc_bleadv_want(&s_av, on);
    adv_apply();
}
void ext_ble_disconnect(void) { if (s_conn) esp_ble_gap_disconnect(s_bda); }
void ext_ble_unpair(void)
{
    if (!s_ready) return;
    int n = esp_ble_get_bond_device_num();
    if (n > 0) {
        esp_ble_bond_dev_t *l = malloc((size_t)n * sizeof *l);
        if (l) {
            if (esp_ble_get_bond_device_list(&n, l) == ESP_OK)
                for (int i = 0; i < n; i++) esp_ble_remove_bond_device(l[i].bd_addr);
            free(l);
        }
    }
    ext_ble_disconnect();
    printf("APP ext ble unpaired\n");
}

bool ext_ble_sink(const pc_extsend_ev *e)
{
    if (e->kind == PC_XEV_WAIT) return true;
    if (!s_dev || !s_conn) return false;
    if (e->kind == PC_XEV_WHEEL) {
        uint8_t r[PC_EXTHID_LEN_MOUSE] = { 0, 0, 0, (uint8_t)(int8_t)(int16_t)e->code, 0 };   /* buttons, x, y, wheel, pan */
        return esp_hidd_dev_input_set(s_dev, 0, PC_EXTHID_RID_MOUSE, r, sizeof r) == ESP_OK;
    }
    if (e->kind == PC_XEV_CONS) {
        uint8_t r[2] = { (uint8_t)(e->code & 0xFF), (uint8_t)(e->code >> 8) };
        return esp_hidd_dev_input_set(s_dev, 0, 1, r, sizeof r) == ESP_OK;
    }
    uint8_t r[8] = { e->mod, 0, (uint8_t)e->code, 0, 0, 0, 0, 0 };
    return esp_hidd_dev_input_set(s_dev, 0, 2, r, sizeof r) == ESP_OK;
}

static void gap_cb(esp_gap_ble_cb_event_t ev, esp_ble_gap_cb_param_t *p)
{
    switch (ev) {
    case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT: esp_ble_gap_ext_adv_set_rand_addr(0, s_own); break;
    case ESP_GAP_BLE_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT: esp_ble_gap_config_ext_adv_data_raw(0, sizeof s_adv, s_adv); break;
    case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT: s_adv_cfg = true; adv_apply(); break;
    case ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT: pc_bleadv_started(&s_av, p->ext_adv_start.status == 0); printf("APP ext ble adv start status=%d\n", p->ext_adv_start.status); adv_apply(); break;   /* an error never clears the flag; then what is wanted now */
    case ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT: pc_bleadv_stopped(&s_av); printf("APP ext ble adv stopped\n"); adv_apply(); break;
    case ESP_GAP_BLE_SEC_REQ_EVT: esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, true); break;   /* Just Works: accept */
    case ESP_GAP_BLE_AUTH_CMPL_EVT: printf("APP ext ble auth success=%d\n", p->ble_security.auth_cmpl.success); break;
    default: break;
    }
}
static void gatts_cb(esp_gatts_cb_event_t ev, esp_gatt_if_t gif, esp_ble_gatts_cb_param_t *p)
{
    if (ev == ESP_GATTS_CONNECT_EVT) {
        memcpy(s_bda, p->connect.remote_bda, sizeof s_bda);
        snprintf(s_peer, sizeof s_peer, "%02X:%02X:%02X", s_bda[3], s_bda[4], s_bda[5]);
        pc_bleadv_conn(&s_av, true);                            /* a connection ends the advertising set */
        s_advertising = false;
    }
    esp_hidd_gatts_event_handler(ev, gif, p);
}
static void hidd_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == ESP_HIDD_CONNECT_EVENT) { s_conn = true; pc_bleadv_conn(&s_av, true); printf("APP ext ble connected %s\n", s_peer); }
    else if (id == ESP_HIDD_DISCONNECT_EVENT) { s_conn = false; pc_bleadv_conn(&s_av, false); printf("APP ext ble disconnected\n"); adv_apply(); }
}

/* a static random address (two top bits 11), made once and kept in NVS so the bonds stay valid across restarts */
static void own_addr_load(void)
{
    nvs_handle_t h;
    size_t n = sizeof s_own;
    bool have = false;
    if (nvs_open("ext", NVS_READWRITE, &h) == ESP_OK) {
        have = nvs_get_blob(h, "bleaddr", s_own, &n) == ESP_OK && n == sizeof s_own && (s_own[0] & 0xC0) == 0xC0;
        if (!have) {
            esp_ble_gap_addr_create_static(s_own);
            nvs_set_blob(h, "bleaddr", s_own, sizeof s_own);
            nvs_commit(h);
        }
        nvs_close(h);
    } else esp_ble_gap_addr_create_static(s_own);
}

int ext_ble_init(void)
{
    esp_err_t e;
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);     /* the classic half is never used in this boot (a restart brings it back); a refusal is not fatal */
    esp_bt_controller_config_t bc = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((e = esp_bt_controller_init(&bc))) return printf("APP ext ble ctrl init %s\n", esp_err_to_name(e)), -1;
    if ((e = esp_bt_controller_enable(ESP_BT_MODE_BLE))) return printf("APP ext ble ctrl enable %s\n", esp_err_to_name(e)), -1;
    esp_bluedroid_config_t c = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if ((e = esp_bluedroid_init_with_cfg(&c))) return printf("APP ext ble bluedroid init %s\n", esp_err_to_name(e)), -1;
    if ((e = esp_bluedroid_enable())) return printf("APP ext ble bluedroid enable %s\n", esp_err_to_name(e)), -1;
    esp_ble_auth_req_t auth = ESP_LE_AUTH_BOND;                /* Just Works: no input, no output, bonded */
    esp_ble_io_cap_t io = ESP_IO_CAP_NONE;
    uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK, ksize = 16;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &ksize, 1);
    esp_ble_gap_register_callback(gap_cb);
    esp_ble_gatts_register_callback(gatts_cb);
    s_run = s_cfg; s_run.serial_number = ext_serial();   /* the const table stays in flash; the copy with the runtime serial lives in PSRAM */
    if ((e = esp_hidd_dev_init(&s_run, ESP_HID_TRANSPORT_BLE, hidd_cb, &s_dev))) return printf("APP ext ble hidd init %s\n", esp_err_to_name(e)), -2;
    esp_ble_gap_set_device_name(s_run.device_name);
    pc_bleadv_init(&s_av);
    own_addr_load();
    esp_ble_gap_set_rand_addr(s_own);                          /* the host's own address too (pairing and the connection use it) */
    esp_ble_gap_ext_adv_set_params(0, &s_advp);                /* params -> data -> ready; advertising starts only when ext_ble_advertise(true) */
    s_ready = true;
    printf("APP ext ble ready\n");
    return 0;
}
