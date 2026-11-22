/*
 * JC player test peer "JC Peer AVRC" (2026-11-14): an A2DP sink + AVRCP target with absolute volume, driven from a UART
 * console, to bench the player's headphones-mode headset keys and absolute volume (bt-remote).
 * Based on ESP-IDF v6.1 examples/bluetooth/bluedroid/classic_bt/avrcp_absolute_volume (Unlicense OR CC0-1.0); the
 * example's 10 s volume-change simulation is gone: the peer sends CHANGED only when told to (`vol`, `echo`).
 * The answer options (ansdelay, ansk, ansfix, bit7, hold) need the patched Bluedroid (make_bt.sh, avrc_bench.patch):
 * btc_avrc_bench_answer() below replaces the patch's weak default.
 * Every line this file prints starts with "PEER " and carries t=<ms since boot>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_console.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "bt_app_core_utils.h"
#include "bredr_app_common_utils.h"
#include "a2dp_utils_tags.h"
#include "a2dp_sink_common_utils.h"
#include "a2dp_sink_int_codec_utils.h"

static const char local_device_name[] = CONFIG_EXAMPLE_LOCAL_DEVICE_NAME;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
#define P(fmt, ...) printf("PEER " fmt " t=%" PRIu32 "\n", ##__VA_ARGS__, now_ms())

/* ---------- state (console task, BTC task, bench task: small words, a mutex around the event list) ---------- */
static volatile bool s_conn;            /* AVRCP target connected */
static volatile bool s_ct_conn;         /* AVRCP controller connected (passthrough keys) */
static volatile bool s_reg;             /* the player registered VOLUME_CHANGE (one-shot: cleared by our CHANGED) */
static volatile int s_vol = 0;          /* the peer's own volume (0..127) */
static volatile int s_last_set = -1, s_last_ans = -1;
static volatile uint32_t s_n_set, s_n_ans, s_n_chg;
/* options */
static volatile int s_echo_ms;          /* 0 off */
static volatile bool s_echo_cmd;        /* echo timed from the command (true) or from the answer (false, default) */
static volatile int s_ansdelay_ms;      /* delay of the answer */
static volatile int s_ansdelay_n;       /* how many answers the delay applies to (0 = all) */
static volatile int s_ansk;             /* answer = request + k (clamped 0..127) */
static volatile int s_ansfix = -1;      /* answer always this (-1 off) */
static volatile bool s_bit7;            /* OR 0x80 into the answer and into CHANGED */
static volatile bool s_hold;            /* never answer */
static volatile bool s_nocap;           /* do not offer VOLUME_CHANGE (applied at the next connection) */
static volatile bool s_unordered;       /* answer at once even while a delayed answer is still out (default: in order, as a headset) */
static volatile int s_ansgap = 1;        /* in order: ms after the delayed answer still out */
static volatile int s_pend_ans;         /* deferred answers not sent yet */
static volatile uint32_t s_last_due;    /* when the last deferred answer goes out */

/* timed events: a deferred answer or an echo */
typedef struct { bool used; uint32_t due, t0; bool ans; uint8_t label, ctype, vol; } ev_t;
static ev_t s_ev[16];
static SemaphoreHandle_t s_mx;
static TaskHandle_t s_bench_task;

extern void btc_avrc_bench_abs_vol_rsp(uint8_t label, uint8_t ctype, uint8_t volume);   /* avrc_bench.patch */

static void ev_add(uint32_t delay_ms, bool ans, uint8_t label, uint8_t ctype, uint8_t vol)
{
    xSemaphoreTake(s_mx, portMAX_DELAY);
    int i;
    for (i = 0; i < 16 && s_ev[i].used; i++) {}
    if (i < 16) {
        s_ev[i] = (ev_t){ true, now_ms() + delay_ms, now_ms(), ans, label, ctype, vol };
        if (ans) { s_pend_ans++; s_last_due = s_ev[i].due; }
    }
    xSemaphoreGive(s_mx);
    if (i == 16) P("event list full, dropped %s %u", ans ? "answer" : "echo", vol);
    xTaskNotifyGive(s_bench_task);
}

static void send_changed(int v, const char *why)
{
    uint8_t w = (uint8_t)(v & 0x7F) | (s_bit7 ? 0x80 : 0);
    if (!s_conn) { P("changed %u not sent (%s): not connected", w, why); return; }
    if (!s_reg) { P("changed %u not sent (%s): not registered", w, why); return; }
    s_reg = false;                      /* the stack deregisters on CHANGED; the player registers again */
    esp_avrc_rn_param_t rn = { .volume = w };
    esp_err_t e = esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &rn);
    s_n_chg++;
    P("changed %u (%s) rc=%s", w, why, esp_err_to_name(e));
}

static void answered(uint8_t v, bool deferred, uint32_t waited)
{
    s_last_ans = v;
    s_vol = v & 0x7F;
    s_n_ans++;
    P("ans %u%s waited=%" PRIu32, v, deferred ? " deferred" : "", waited);
    if (s_echo_ms > 0 && !s_echo_cmd) ev_add((uint32_t)s_echo_ms, false, 0, 0, v & 0x7F);
}

static void bench_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t wait = 1000;
        ev_t fire[16];
        int nf = 0;
        xSemaphoreTake(s_mx, portMAX_DELAY);
        uint32_t t = now_ms();
        for (int i = 0; i < 16; i++) {
            if (!s_ev[i].used) continue;
            int32_t d = (int32_t)(s_ev[i].due - t);
            if (d <= 0) { fire[nf++] = s_ev[i]; s_ev[i].used = false; }
            else if ((uint32_t)d < wait) wait = (uint32_t)d;
        }
        xSemaphoreGive(s_mx);
        for (int i = 0; i < nf; i++) {
            if (fire[i].ans) {
                s_pend_ans--;
                if (!s_conn) { P("deferred answer %u dropped: not connected", fire[i].vol); continue; }
                btc_avrc_bench_abs_vol_rsp(fire[i].label, fire[i].ctype, fire[i].vol);
                answered(fire[i].vol, true, now_ms() - fire[i].t0);
            } else {
                send_changed(fire[i].vol, "echo");
            }
        }
        if (nf) continue;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait ? wait : 1));
    }
}

/* the patch's hook: BTC task, before the stack answers SetAbsoluteVolume. true = answer now with *volume */
bool btc_avrc_bench_answer(uint8_t label, uint8_t ctype, uint8_t *volume)
{
    int req = *volume, v = req;
    s_last_set = req;
    s_n_set++;
    P("set %d label=%u", req, label);
    if (s_echo_ms > 0 && s_echo_cmd) ev_add((uint32_t)s_echo_ms, false, 0, 0, (uint8_t)(req & 0x7F));
    if (s_hold) { P("hold: no answer to %d", req); return false; }
    if (s_ansfix >= 0) v = s_ansfix;
    else if (s_ansk) { v = req + s_ansk; v = v < 0 ? 0 : v > 127 ? 127 : v; }
    if (s_bit7) v |= 0x80;
    int d = s_ansdelay_ms > 0 && s_ansdelay_n >= 0 ? s_ansdelay_ms : 0;
    if (d && s_ansdelay_n > 0 && --s_ansdelay_n == 0) s_ansdelay_n = -1;   /* last counted one: later answers at once */
    if (!s_unordered && s_pend_ans > 0) {   /* a headset answers in order: never overtake a delayed answer still out */
        int32_t behind = (int32_t)(s_last_due - now_ms()) + s_ansgap;
        if (behind > d) d = behind;
    }
    if (d > 0) {
        ev_add((uint32_t)d, true, label, ctype, (uint8_t)v);
        P("answer %d in %d ms", v, d);
        return false;
    }
    *volume = (uint8_t)v;
    answered((uint8_t)v, false, 0);
    return true;
}

/* ---------- Bluetooth callbacks ---------- */
static void bt_app_dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param) { bredr_app_dev_evt_def_hdl(event, param); }
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) { bredr_app_gap_evt_def_hdl(event, param); }

static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_PROF_STATE_EVT:
    case ESP_A2D_SNK_PSC_CFG_EVT:
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
        bt_app_work_dispatch(bt_a2d_evt_def_hdl, event, param, sizeof(esp_a2d_cb_param_t), NULL, NULL);
        break;
    case ESP_A2D_CONNECTION_STATE_EVT:
        P("a2dp state=%d", param->conn_stat.state);
        bt_app_work_dispatch(bt_a2d_evt_int_codec_hdl, event, param, sizeof(esp_a2d_cb_param_t), NULL, NULL);
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_SEP_REG_STATE_EVT:
        bt_app_work_dispatch(bt_a2d_evt_int_codec_hdl, event, param, sizeof(esp_a2d_cb_param_t), NULL, NULL);
        break;
    default:
        break;
    }
}

static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len) { bt_a2d_data_hdl(data, len); }

static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        s_ct_conn = param->conn_stat.connected;
        P("ct link=%d", s_ct_conn);
        break;
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
        P("pt rsp key=0x%x state=%d code=%d", param->psth_rsp.key_code, param->psth_rsp.key_state, param->psth_rsp.rsp_code);
        break;
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        P("ct remote feat=0x%" PRIx32 " tg=0x%x", param->rmt_feats.feat_mask, param->rmt_feats.tg_feat_flag);
        break;
    default:
        break;
    }
}

static void set_caps(void)
{
    esp_avrc_rn_evt_cap_mask_t evt_set = { 0 };
    if (!s_nocap) esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    esp_err_t e = esp_avrc_tg_set_rn_evt_cap(&evt_set);
    P("caps volume_change=%d rc=%s", !s_nocap, esp_err_to_name(e));
}

static void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
        s_conn = param->conn_stat.connected;
        s_reg = false;
        P("tg link=%d", s_conn);
        if (!s_conn) set_caps();        /* nocap: applied for the next connection */
        break;
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT:
        P("tg remote feat=0x%" PRIx32 " ct=0x%x", param->rmt_feats.feat_mask, param->rmt_feats.ct_feat_flag);
        break;
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
        break;                          /* already printed by the hook (`set`) */
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        if (param->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
            s_reg = true;
            esp_avrc_rn_param_t rn = { .volume = (uint8_t)s_vol };
            esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &rn);
            P("reg volume_change (interim %d)", s_vol);
        } else {
            P("reg event %d", param->reg_ntf.event_id);
        }
        break;
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
        P("tg pt key=0x%x state=%d", param->psth_cmd.key_code, param->psth_cmd.key_state);
        break;
    default:
        break;
    }
}

enum { BT_APP_EVT_STACK_UP = 0 };
static void bt_av_hdl_stack_evt(uint16_t event, void *p_param)
{
    (void)p_param;
    if (event != BT_APP_EVT_STACK_UP) return;
    esp_bt_gap_set_device_name(local_device_name);
    esp_bt_dev_register_callback(bt_app_dev_cb);
    esp_bt_gap_register_callback(bt_app_gap_cb);
    esp_avrc_ct_register_callback(bt_app_rc_ct_cb);
    assert(esp_avrc_ct_init() == ESP_OK);
    esp_avrc_tg_register_callback(bt_app_rc_tg_cb);
    assert(esp_avrc_tg_init() == ESP_OK);
    set_caps();
    esp_a2d_register_callback(&bt_app_a2d_cb);
    assert(esp_a2d_sink_init() == ESP_OK);
    esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);
    esp_a2d_sink_get_delay_value();
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    const uint8_t *a = esp_bt_dev_get_address();
    P("up name=\"%s\" bda=%02x:%02x:%02x:%02x:%02x:%02x", local_device_name, a[0], a[1], a[2], a[3], a[4], a[5]);
}

/* ---------- console ---------- */
static int cmd_pt(int argc, char **argv)
{
    static const char *const nm[] = { "play", "pause", "stop", "forward", "backward" };
    static const uint8_t kc[] = { ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STOP, ESP_AVRC_PT_CMD_FORWARD,
                                  ESP_AVRC_PT_CMD_BACKWARD };
    static uint8_t tl;
    int k = -1;
    for (int i = 0; argc > 1 && i < 5; i++) if (!strcmp(argv[1], nm[i])) k = i;
    if (k < 0) { P("pt ? (play|pause|stop|forward|backward)"); return 1; }
    esp_err_t e1 = esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0F, kc[k], ESP_AVRC_PT_CMD_STATE_PRESSED);
    vTaskDelay(pdMS_TO_TICKS(60));
    esp_err_t e2 = esp_avrc_ct_send_passthrough_cmd(tl++ & 0x0F, kc[k], ESP_AVRC_PT_CMD_STATE_RELEASED);
    P("pt %s press=%s release=%s", nm[k], esp_err_to_name(e1), esp_err_to_name(e2));
    return 0;
}

static int cmd_vol(int argc, char **argv)
{
    if (argc < 2) { P("vol ?"); return 1; }
    int v = atoi(argv[1]);
    if (v < 0 || v > 127) { P("vol out of range"); return 1; }
    s_vol = v;
    send_changed(v, "vol");
    return 0;
}

static int cmd_echo(int argc, char **argv)
{
    if (argc < 2) { P("echo ?"); return 1; }
    s_echo_ms = atoi(argv[1]);
    s_echo_cmd = argc > 2 && !strcmp(argv[2], "cmd");
    P("echo %d ms from %s", s_echo_ms, s_echo_cmd ? "command" : "answer");
    return 0;
}

static int cmd_ansdelay(int argc, char **argv)
{
    if (argc < 2) { P("ansdelay ?"); return 1; }
    s_ansdelay_ms = atoi(argv[1]);
    s_ansdelay_n = argc > 2 ? atoi(argv[2]) : 0;
    P("ansdelay %d ms count=%d", s_ansdelay_ms, s_ansdelay_n);
    return 0;
}

static int cmd_ansk(int argc, char **argv) { s_ansk = argc > 1 ? atoi(argv[1]) : 0; P("ansk %d", s_ansk); return 0; }
static int cmd_ansfix(int argc, char **argv) { s_ansfix = argc > 1 ? atoi(argv[1]) : -1; P("ansfix %d", s_ansfix); return 0; }
static int cmd_bit7(int argc, char **argv) { (void)argc; (void)argv; s_bit7 = !s_bit7; P("bit7 %d", s_bit7); return 0; }
static int cmd_hold(int argc, char **argv) { (void)argc; (void)argv; s_hold = !s_hold; P("hold %d", s_hold); return 0; }
static int cmd_nocap(int argc, char **argv)
{
    (void)argc; (void)argv;
    s_nocap = !s_nocap;
    P("nocap %d (next connection)", s_nocap);
    if (!s_conn) set_caps();
    return 0;
}
static int cmd_ansgap(int argc, char **argv) { s_ansgap = argc > 1 ? atoi(argv[1]) : 1; if (s_ansgap < 1) s_ansgap = 1; P("ansgap %d", s_ansgap); return 0; }
static int cmd_unordered(int argc, char **argv) { (void)argc; (void)argv; s_unordered = !s_unordered; P("unordered %d", s_unordered); return 0; }
static int cmd_reset(int argc, char **argv)
{
    (void)argc; (void)argv;
    s_echo_ms = 0; s_echo_cmd = false; s_ansdelay_ms = 0; s_ansdelay_n = 0; s_ansk = 0; s_ansfix = -1; s_bit7 = false; s_hold = false; s_unordered = false; s_ansgap = 1;
    P("options reset (nocap %d kept)", s_nocap);
    return 0;
}
static int cmd_st(int argc, char **argv)
{
    (void)argc; (void)argv;
    P("st conn=%d ct=%d reg=%d vol=%d last_set=%d last_ans=%d n_set=%" PRIu32 " n_ans=%" PRIu32 " n_chg=%" PRIu32
      " echo=%d%s ansdelay=%d/%d ansk=%d ansfix=%d bit7=%d hold=%d nocap=%d unordered=%d pend=%d",
      s_conn, s_ct_conn, s_reg, s_vol, s_last_set, s_last_ans, s_n_set, s_n_ans, s_n_chg, s_echo_ms, s_echo_cmd ? "cmd" : "",
      s_ansdelay_ms, s_ansdelay_n, s_ansk, s_ansfix, s_bit7, s_hold, s_nocap, s_unordered, s_pend_ans);
    return 0;
}

static void console_start(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "pt", .help = "pt <play|pause|stop|forward|backward>: passthrough press + release", .func = cmd_pt },
        { .command = "vol", .help = "vol <0..127>: send CHANGED(n) now", .func = cmd_vol },
        { .command = "echo", .help = "echo <ms> [cmd]: CHANGED with the set value <ms> after each answer (cmd: after each command); 0 off", .func = cmd_echo },
        { .command = "ansdelay", .help = "ansdelay <ms> [n]: delay the SetAbsoluteVolume answer (n: only the next n answers)", .func = cmd_ansdelay },
        { .command = "ansk", .help = "ansk <k>: answer request + k (clamped 0..127); 0 off", .func = cmd_ansk },
        { .command = "ansfix", .help = "ansfix <n>: always answer n; no argument / -1 off", .func = cmd_ansfix },
        { .command = "bit7", .help = "bit7: toggle OR 0x80 into the answer and CHANGED", .func = cmd_bit7 },
        { .command = "hold", .help = "hold: toggle never answering SetAbsoluteVolume", .func = cmd_hold },
        { .command = "nocap", .help = "nocap: toggle not offering VOLUME_CHANGE (next connection)", .func = cmd_nocap },
        { .command = "ansgap", .help = "ansgap <ms>: an answer queued behind a delayed one goes this long after it (default 1)", .func = cmd_ansgap },
        { .command = "unordered", .help = "unordered: toggle answering at once while a delayed answer is still out (default: in order)", .func = cmd_unordered },
        { .command = "reset", .help = "reset: every answer/echo option off", .func = cmd_reset },
        { .command = "st", .help = "st: state and options", .func = cmd_st },
    };
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "peer>";
    rc.max_cmdline_length = 128;
    esp_console_dev_uart_config_t uc = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uc, &rc, &repl));
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void app_main(void)
{
    s_mx = xSemaphoreCreateMutex();
    xTaskCreate(bench_task, "bench", 4096, NULL, 6, &s_bench_task);
    ESP_ERROR_CHECK(bredr_app_common_init());
    bt_app_task_start_up();
    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_EVT_STACK_UP, NULL, 0, NULL, NULL);
    console_start();
}
