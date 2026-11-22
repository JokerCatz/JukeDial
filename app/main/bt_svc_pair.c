// SPDX-License-Identifier: LGPL-2.1-or-later
/* Bluetooth, GAP callback: the pairing confirmation (a phone asks to pair: the six digits, Pair / Reject) and the inquiry results of headphones mode.
 * gap_cb inlines pair_request and hp_disc_result, so the three live together. Split out of bt_svc.c; shared state in bt_priv.h. */
#include "bt_priv.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_CLASSIC_ENABLED

/* ---- pairing confirmation (requirements notes: show the six digits, Pair / Reject; no auto-accept for phones) ---- */
static pc_pair_t s_pair;
static portMUX_TYPE s_pair_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void ui_pair_show(void *arg)
{
    (void)arg;
    ui_wake();
    if (ui_top() != &scr_pair) ui_push(&scr_pair, NULL);
}
static void pair_request(const uint8_t *bda, uint32_t num)   /* BTC task */
{
    bool ours = s_mode == BT_MODE_HEADPHONES || s_hp_active;   /* we connect to headphones: nothing to compare */
    portENTER_CRITICAL(&s_pair_mux);
    pc_pair_decision_t d = pc_pair_request(&s_pair, bda, num, ours, now_ms());
    portEXIT_CRITICAL(&s_pair_mux);
    printf("APP bt pair request %s\n", d == PC_PAIR_PROMPT ? "ask" : d == PC_PAIR_ACCEPT ? "accept" : "reject (busy)");
    if (d == PC_PAIR_PROMPT) ui_async(ui_pair_show, NULL);
    else esp_bt_gap_ssp_confirm_reply((uint8_t *)bda, d == PC_PAIR_ACCEPT);
}
bool bt_svc_pair_pending(char digits[7])
{
    portENTER_CRITICAL(&s_pair_mux);
    bool a = s_pair.asking;
    uint32_t n = s_pair.num;
    portEXIT_CRITICAL(&s_pair_mux);
    if (a && digits) pc_pair_digits(n, digits);
    return a;
}
void bt_svc_pair_reply(bool accept)
{
    uint8_t bda[6];
    portENTER_CRITICAL(&s_pair_mux);
    bool open = pc_pair_answer(&s_pair, bda);
    portEXIT_CRITICAL(&s_pair_mux);
    if (!open) return;
    printf("APP bt pair %s\n", accept ? "accepted" : "rejected");
    esp_bt_gap_ssp_confirm_reply(bda, accept);
}
bool bt_svc_pair_check_timeout(void)
{
    uint8_t bda[6];
    portENTER_CRITICAL(&s_pair_mux);
    bool gone = pc_pair_expired(&s_pair, now_ms());
    if (gone) memcpy(bda, s_pair.bda, 6);
    portEXIT_CRITICAL(&s_pair_mux);
    if (gone) { printf("APP bt pair timeout: rejected\n"); esp_bt_gap_ssp_confirm_reply(bda, false); }
    return gone;
}

int bt_svc_paired_count(void) { return s_stack_up ? esp_bt_gap_get_bond_device_num() : -1; }

int bt_svc_forget_all(void)
{
    if (!s_stack_up) return -1;
    int n = esp_bt_gap_get_bond_device_num();
    if (n <= 0) return 0;
    esp_bd_addr_t list[16];
    if (n > 16) n = 16;
    if (esp_bt_gap_get_bond_device_list(&n, list) != ESP_OK) return -1;
    for (int i = 0; i < n; i++) { esp_bt_gap_remove_bond_device(list[i]); vol_out_forget(list[i]); }   /* BTVOL: a forgotten headset starts at the default again */
    printf("APP bt forgot %d paired device(s)\n", n);
    return n;
}

static void hp_disc_result(esp_bt_gap_cb_param_t *p);
void gap_cb(esp_bt_gap_cb_event_t ev, esp_bt_gap_cb_param_t *p)
{
    switch (ev) {
    case ESP_BT_GAP_CFM_REQ_EVT: pair_request(p->cfm_req.bda, p->cfm_req.num_val); break;
    case ESP_BT_GAP_PIN_REQ_EVT: { esp_bt_pin_code_t pin = { '0', '0', '0', '0' }; esp_bt_gap_pin_reply(p->pin_req.bda, true, 4, pin); break; }
    case ESP_BT_GAP_AUTH_CMPL_EVT: printf("APP bt auth=%d name=%s\n", p->auth_cmpl.stat, (const char *)p->auth_cmpl.device_name); break;
    case ESP_BT_GAP_DISC_RES_EVT: hp_disc_result(p); break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        printf("APP bt discovery state=%d\n", p->disc_st_chg.state);
        if (s_mode == BT_MODE_HEADPHONES && p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED && !s_hp_connected)
            snprintf(s_status, sizeof s_status, "%s", T(S_SCAN_DONE_PICK_HEADPHONES) /* 掃描完成：點選耳機 */);
        break;
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        if (p->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
            snprintf(s_peer, sizeof s_peer, "%s", (const char *)p->read_rmt_name.rmt_name);
            snprintf(s_status, sizeof s_status, "%s %s", T(S_CONNECTED_2) /* 已連線： */, s_peer);
            ui_async(ui_status, NULL);
        }
        break;
    default: break;
    }
}
static void hp_disc_result(esp_bt_gap_cb_param_t *p)       /* BTC: one inquiry result */
{
    char name[64] = "";
    uint32_t cod = 0;
    for (int i = 0; i < p->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *q = &p->disc_res.prop[i];
        if (q->type == ESP_BT_GAP_DEV_PROP_COD) cod = *(uint32_t *)q->val;
        else if (q->type == ESP_BT_GAP_DEV_PROP_BDNAME) snprintf(name, sizeof name, "%.*s", q->len, (const char *)q->val);
        else if (q->type == ESP_BT_GAP_DEV_PROP_EIR && !name[0]) {
            uint8_t len = 0;
            uint8_t *n = esp_bt_gap_resolve_eir_data((uint8_t *)q->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
            if (!n) n = esp_bt_gap_resolve_eir_data((uint8_t *)q->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
            if (n) snprintf(name, sizeof name, "%.*s", len, (const char *)n);
        }
    }
    printf("APP bt seen cod=0x%06lx name=%s\n", (unsigned long)cod, name);
    if (!esp_bt_gap_is_valid_cod(cod) || !(esp_bt_gap_get_cod_srvc(cod) & ESP_BT_COD_SRVC_RENDERING)) return;   /* not an audio sink */
    portENTER_CRITICAL(&s_dev_mux);
    int i = 0;
    while (i < s_ndevs && memcmp(s_devs[i].bda, p->disc_res.bda, 6)) i++;
    if (i == s_ndevs && s_ndevs < HP_MAX) s_ndevs++;
    if (i < s_ndevs) {
        memcpy(s_devs[i].bda, p->disc_res.bda, 6);
        if (name[0] || !s_devs[i].name[0])
            snprintf(s_devs[i].name, sizeof s_devs[i].name, "%s", name[0] ? name : "?");
    }
    portEXIT_CRITICAL(&s_dev_mux);
}

#endif
