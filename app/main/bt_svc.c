// SPDX-License-Identifier: LGPL-2.1-or-later
/* Bluetooth modes (spec 2026-11-09-bluetooth-design.md). BT2: speaker - a phone streams A2DP (AAC or SBC, external
 * codec, decoded with esp_audio_codec) into a ring buffer that plays as a mixer source through audio_svc, so
 * volume, effects and the spectrum apply as for any other source. */
/* The code is split by what it does: bt_svc.c (modes, stack up/down, Auracast listening, status), bt_svc_a2dp.c (speaker and headphones paths), bt_svc_pair.c (pairing, GAP),
 * bt_svc_cast_tx.c (Auracast broadcast); bt_priv.h declares what they share. */
#include "bt_priv.h"
#include "vol_out.h"
bt_mode_t s_mode = BT_MODE_OFF;
char s_status[128];

const char *bt_svc_mode_label(bt_mode_t m)
{
    static const char *const zh[] = { "關閉", "藍牙喇叭", "藍牙耳機", "Auracast 廣播", "Auracast 收聽" };
    static const char *const en[] = { "Off", "Speaker", "Headphones", "Auracast", "Listen" };
    return m < BT_MODE_COUNT ? T_RAW(zh[m], en[m]) : "";
}
bt_mode_t bt_svc_mode(void) { return s_mode; }
EXT_RAM_BSS_ATTR static bool s_lean;                                       /* the broadcast-radio boot (cast_mode.c): the Auracast source alone, no player: no fx hook, the sink is set up by hand */
void bt_svc_lean_begin(void) { s_lean = true; }
bool bt_svc_lean(void) { return s_lean; }
EXT_RAM_BSS_ATTR static bool s_bypass;                                     /* TEST ONLY (console bcastbypass): the full player may run Wi-Fi and the radio while it broadcasts (BR report "Full player instead of the mode") */
void bt_svc_test_bypass(bool on) { s_bypass = on; }
bool bt_svc_wifi_ok(void) { return s_lean || s_bypass; }
const char *bt_svc_status(void) { return s_status; }

#if CONFIG_BT_ENABLED && CONFIG_BT_CLASSIC_ENABLED

bool bt_svc_available(void) { return true; }

/* ---- player ownership while the phone streams ---- */
char s_peer[64];
volatile bool s_hp_connected, s_hp_active;   /* headphones mode (below) */
/* speaker mode: the phone's link, so speaker_off() can take it down BEFORE the A2DP deinit (as headphones_off() does). Board run
 * 2026-11-19: a deinit while the link was being lost made Bluedroid panic when the supervision timeout fired (btc_av.c, sm_handle NULL). */
EXT_RAM_BSS_ATTR volatile bool s_spk_connected, s_spk_leaving;
EXT_RAM_BSS_ATTR uint8_t s_spk_bda[6];
static void hp_restart_request(void);
static uint32_t s_gen;
static bool s_streaming;
static void title(char *out, size_t cap)                 /* the phone's track (AVRCP, bt_meta.h) once a title is known, else its name */
{
    if (!bt_meta_title(out, cap)) snprintf(out, cap, "%s %s", T(S_BLUETOOTH) /* 藍牙： */, s_peer[0] ? s_peer : T(S_PHONE) /* 手機 */);
}
static bool playing(void) { return s_streaming; }
static void stop_stream(void) { audio_svc_stop(false); s_streaming = false; player_ctl_release(); }
static now_ops_t s_ops;                                  /* filled when the stream starts (zero-initialised: PSRAM, not internal .data); no_art, cover_jpeg, open_info follow the metadata (bt_meta_bind) */
static void forget(void) { s_streaming = false; }
static void on_event(const aud_event_t *e) { (void)e; }

void ui_stream(void *arg)                         /* LVGL: the phone started / stopped streaming */
{
    bool start = (bool)(intptr_t)arg;
    if (start && !s_streaming) {
        s_w = s_r = 0;
        s_primed = false;
        s_streaming = true;
        s_ops.title = title;
        s_ops.stop = stop_stream;
        s_ops.playing = playing;
        s_ops.src = NOW_SRC_BT;                           /* the main menu's Bluetooth cell reopens this stream's playing page */
        bt_meta_bind(&s_ops);
        player_ctl_take(&s_ops, forget, on_event);
        s_gen = audio_svc_play_mixer(mix_render, NULL, s_rate, mix_release, NULL);
        player_ctl_changed();
        printf("APP bt stream start rate=%lu gen=%lu\n", (unsigned long)s_rate, (unsigned long)s_gen);
    } else if (!start && s_streaming) {
        stop_stream();
        printf("APP bt stream stop\n");
    }
}
void ui_status(void *arg) { (void)arg; player_ctl_changed(); }

/* deinit is asynchronous: the stack must not go down before it finishes, or the next init in this boot fails */
SemaphoreHandle_t s_a2d_down;
/* Bluedroid finishes a deinit that starts while the link is being lost only after the link's disconnection resolves: the
 * BR/EDR supervision timeout is BTA_DM_LINK_TIMEOUT = 8000 slots x 0.625 ms = 5 s (bta_dm_cfg.c, set by bta_dm_act.c via
 * BTM_SetDefaultLinkSuperTout), so the bound is twice that. Waits run on the bt_mode task (core 0, prio 3), never LVGL or audio.
 * Returns false when the deinit has not reported done: the caller must then leave the stack up (see s_a2d_pending). */
#define A2D_DEINIT_MS 10000
static bool a2d_deinit_wait(int ms)
{
    if (!s_a2d_down) return true;
    if (xSemaphoreTake(s_a2d_down, pdMS_TO_TICKS(ms < 1000 ? ms : 1000)) == pdTRUE) return true;
    if (ms == 0) return false;                           /* a poll: no message */
    if (ms > 1000) {
        printf("APP bt a2dp deinit waiting (link loss: up to %d s)\n", ms / 1000);
        if (xSemaphoreTake(s_a2d_down, pdMS_TO_TICKS(ms - 1000)) == pdTRUE) return true;
    }
    printf("APP bt a2dp deinit timeout\n");
    return false;
}
/* an A2DP deinit we started has not reported done: the stack stays up until it does (mode task only: checked at the next
 * mode request and polled once a second while pending; the Bluedroid callback only gives the semaphore) */
static bool s_a2d_pending EXT_RAM_BSS_ATTR;
static bool a2d_off_done(bool done)
{
    if (!done) {
        s_a2d_pending = true;
        printf("APP bt a2dp deinit pending: stack left up\n");
    }
    return done;
}
static bool a2d_settled(int ms)                          /* no deinit outstanding (or it just finished) */
{
    if (s_a2d_pending && a2d_deinit_wait(ms)) s_a2d_pending = false;
    return !s_a2d_pending;
}
/* the endpoints are registered once the profile reports it is up (ESP_A2D_PROF_STATE_EVT): registering right
 * after esp_a2d_*_init() can fail with ESP_ERR_INVALID_STATE (seen on the test peer), and then nothing connects */
SemaphoreHandle_t s_a2d_up;
static void a2d_up_wait(const char *role)
{
    if (s_a2d_up && xSemaphoreTake(s_a2d_up, pdMS_TO_TICKS(3000)) != pdTRUE) printf("APP bt a2dp %s init timeout: no endpoints\n", role);
}
bool s_stack_up;

static int stack_up(void)
{
    if (s_stack_up) return 0;
    esp_err_t e;                                          /* NVS was initialised at boot (app_main) */
    esp_bt_controller_config_t bc = BT_CONTROLLER_INIT_CONFIG_DEFAULT();   /* dual mode: Classic + LE (Auracast) */
    if ((e = esp_bt_controller_init(&bc)) != ESP_OK) return printf("APP bt ctrl init %s\n", esp_err_to_name(e)), -1;
    if ((e = esp_bt_controller_enable(ESP_BT_MODE_BTDM)) != ESP_OK) return printf("APP bt ctrl enable %s\n", esp_err_to_name(e)), -1;
    printf("APP bt step=controller ok\n");
    esp_bluedroid_config_t c = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    if ((e = esp_bluedroid_init_with_cfg(&c)) != ESP_OK) return printf("APP bt bluedroid init %s\n", esp_err_to_name(e)), -1;
    printf("APP bt step=bluedroid init ok\n");
    if ((e = esp_bluedroid_enable()) != ESP_OK) return printf("APP bt bluedroid enable %s\n", esp_err_to_name(e)), -1;
    printf("APP bt step=bluedroid enable ok\n");
    esp_bt_gap_register_callback(gap_cb);
    esp_bt_gap_set_device_name(DEV_NAME);
    esp_bt_sp_param_t pt = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t io = ESP_BT_IO_CAP_IO;   /* display + yes/no: phones get numeric comparison */
    esp_bt_gap_set_security_param(pt, &io, sizeof io);
    s_stack_up = true;
    memlog("bt_stack");
    return 0;
}

/* 'off' gives the ~170 KB of internal RAM back - unless LE Audio was started: it has no deinit, so then the stack
 * stays until the next boot (cfg bt_mode=0 by then, so it does not come back) */
static bool cast_used(void);
static void stack_down(void)
{
    if (!s_stack_up || cast_used()) return;
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    s_stack_up = false;
    memlog("bt_down");
}

static int speaker_on(void)
{
    if (stack_up()) return -1;
    if (!s_ring) s_ring = heap_caps_malloc(RING_FRAMES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_raw_q) {
        s_raw_q = xQueueCreate(32, sizeof(esp_a2d_audio_buff_t *));
        xTaskCreatePinnedToCoreWithCaps(decode_task, "bt_dec", 16 * 1024, NULL, configMAX_PRIORITIES - 4, NULL, 1, MALLOC_CAP_SPIRAM);
    }
    if (!s_a2d_down) s_a2d_down = xSemaphoreCreateBinary();
    xSemaphoreTake(s_a2d_down, 0);
    esp_a2d_register_callback(a2d_cb);
    printf("APP bt step=a2dp\n");
    if (!s_a2d_up) s_a2d_up = xSemaphoreCreateBinary();
    xSemaphoreTake(s_a2d_up, 0);
    bt_meta_avrc_start();                                /* AVRCP controller (the phone's track and cover): before the A2DP init; a failure leaves the stream without it */
    if (esp_a2d_sink_init() != ESP_OK) { bt_meta_avrc_stop(); return printf("APP bt a2dp sink init failed\n"), -1; }
    a2d_up_wait("sink");                                 /* the endpoints are registered in ESP_A2D_PROF_STATE_EVT */
    printf("APP bt step=a2dp ok\n");
    esp_a2d_sink_register_audio_data_callback(data_cb);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    snprintf(s_status, sizeof s_status, "%s", T(S_WAITING_FOR_A_PHONE_JC_PLAYER) /* 等待手機連線（JukeDial） */);
    memlog("bt_speaker");
    return 0;
}

static bool speaker_off(void)
{
    if (s_streaming) stop_stream();
    s_spk_leaving = true;                                /* the disconnect event below must not make us connectable again */
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    /* the link down first, the deinit after it: up to 7 s covers a link that is being lost (supervision timeout 5 s, 3.1-4.0 s measured) */
    if (s_spk_connected) esp_a2d_sink_disconnect(s_spk_bda);
    for (int i = 0; i < 70 && s_spk_connected; i++) vTaskDelay(pdMS_TO_TICKS(100));
    if (s_spk_connected) printf("APP bt speaker off: link still up after 7 s\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    bt_meta_avrc_stop();                                 /* AVRCP before the A2DP deinit */
    if (s_a2d_down) xSemaphoreTake(s_a2d_down, 0);       /* a stale give must not satisfy this wait */
    bool done = esp_a2d_sink_deinit() != ESP_OK || a2d_deinit_wait(A2D_DEINIT_MS);
    s_spk_connected = false;
    s_spk_leaving = false;
    return done;
}


#if JC_LDAC
/* a negotiated codec that cannot start (LDAC with a media MTU under 679, an encoder that does not come up): it is
 * left out for the rest of this boot and headphones mode restarts, so the next codec both sides have takes over
 * (instead of silence under an "LDAC" status). Encoder tasks only. */
void ui_hp_stream(void *arg);
void hp_codec_failed(pc_btc_t c, const char *why_zh, const char *why_en)
{
    printf("APP bt hp %s cannot start (%s, mtu=%u): left out until reboot, reconnecting\n", pc_btc_name(c), why_en, s_hp_mtu);
    s_hp_skip |= 1u << c;
    snprintf(s_hp_note, sizeof s_hp_note, T(S_S_FAILED_S) /* %s 無法啟動（%s） */, pc_btc_name(c), T_RAW(why_zh, why_en));
    s_hp_streaming = false;
    s_hp_gen++;
    ui_async(ui_hp_stream, (void *)0);
    snprintf(s_status, sizeof s_status, T(S_S_TRYING_ANOTHER_CODEC) /* %s，改用其他編碼… */, s_hp_note);
    ui_async(ui_status, NULL);
    hp_restart_request();
}
#endif

static void bda_str(const uint8_t *b, char *out) { sprintf(out, "%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]); }
static bool bda_parse(const char *s, uint8_t *b)
{
    unsigned v[6];
    if (strlen(s) != 12 || sscanf(s, "%2x%2x%2x%2x%2x%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; i++) b[i] = (uint8_t)v[i];
    return true;
}
void ui_remember(void *arg) { (void)arg; char h[13]; bda_str(s_hp_bda, h); cfg_set_str("bt_hp", h); }

#if CONFIG_BT_BAP_BROADCAST_SINK
static void listen_pick(const uint8_t bda[6]);
#endif
void bt_svc_connect(const uint8_t bda[6])
{
#if CONFIG_BT_BAP_BROADCAST_SINK
    if (s_mode == BT_MODE_LISTEN) { listen_pick(bda); return; }
#endif
    if (s_mode != BT_MODE_HEADPHONES) return;
    esp_bt_gap_cancel_discovery();
    memcpy(s_hp_bda, bda, 6);
    esp_a2d_source_connect(s_hp_bda);
    snprintf(s_status, sizeof s_status, "%s", T(S_CONNECTING) /* 連線中… */);
}

static int headphones_on(void)
{
    if (stack_up()) return -1;
    if (!s_hp_ring) s_hp_ring = heap_caps_malloc(HP_RING * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    hp_rsq_alloc();
    static bool task;
    if (!task) { task = xTaskCreatePinnedToCoreWithCaps(hp_encode_task, "bt_enc", 16 * 1024, NULL, configMAX_PRIORITIES - 4, NULL, 0, MALLOC_CAP_SPIRAM) == pdPASS; }
#if JC_LDAC
    static bool vtask;                                  /* vendor codecs: the core comes from the measurements (cfg venc_core) */
    if (!vtask) vtask = xTaskCreatePinnedToCoreWithCaps(hp_venc_task, "bt_venc", 16 * 1024, NULL, configMAX_PRIORITIES - 4, NULL,
                                                        cfg_int("venc_core", 1) ? 1 : 0, MALLOC_CAP_SPIRAM) == pdPASS;
#endif
    s_hp_active = true;
    s_hp_leaving = false;
    s_rc_n = 0;
    if (!s_a2d_down) s_a2d_down = xSemaphoreCreateBinary();
    xSemaphoreTake(s_a2d_down, 0);
    bt_remote_start();                                   /* AVRCP (headset keys, absolute volume): before the A2DP init; a failure leaves the mode without it */
    esp_a2d_register_callback(a2d_cb);
    if (!s_a2d_up) s_a2d_up = xSemaphoreCreateBinary();
    xSemaphoreTake(s_a2d_up, 0);
    if (esp_a2d_source_init() != ESP_OK) return printf("APP bt a2dp source init failed\n"), -1;
    a2d_up_wait("source");                               /* endpoints registered (ESP_A2D_PROF_STATE_EVT) before connecting */
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
    const char *saved = cfg_str("bt_hp", "");
    uint8_t b[6];
    if (bda_parse(saved, b)) { bt_svc_connect(b); }
    else { s_mode = BT_MODE_HEADPHONES; bt_svc_scan(); }
    memlog("bt_headphones");
    return 0;
}

static bool headphones_off(void)
{
    s_hp_leaving = true;
    if (s_rc_t) esp_timer_stop(s_rc_t);
    s_hp_streaming = false;
    ui_async(ui_hp_stream, (void *)0);
    esp_bt_gap_cancel_discovery();
    if (s_hp_connected) esp_a2d_source_disconnect(s_hp_bda);
    /* deinit only once the link is down: a deinit during a slow disconnect (3.4 s seen) times out, and the next
     * esp_a2d_source_init() in this boot then fails */
    for (int i = 0; i < 50 && s_hp_connected; i++) vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(300));
    bt_remote_stop();                                    /* AVRCP before the A2DP deinit */
    if (s_a2d_down) xSemaphoreTake(s_a2d_down, 0);       /* a stale give must not satisfy this wait */
    bool done = esp_a2d_source_deinit() != ESP_OK || a2d_deinit_wait(A2D_DEINIT_MS);
    s_hp_active = false;
    s_hp_connected = false;
    return done;
}
#if CONFIG_BT_BAP_BROADCAST_SOURCE
int broadcast_on(void)
{
    if (stack_up()) return -1;
    if (!s_hp_ring) s_hp_ring = heap_caps_malloc(HP_RING * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    hp_rsq_alloc();
    if (!s_cast_inited) {
        s_adv_sem = xSemaphoreCreateBinary();
        esp_ble_gap_register_callback(cast_gap_cb);
        esp_err_t e = esp_ble_audio_common_init(NULL);
        if (e) return printf("APP bt cast common init %d\n", e), -1;
        static esp_ble_audio_bap_broadcast_source_cb_t cb = { 0 };
        esp_ble_audio_bap_broadcast_source_register_cb(&cb);
        if ((e = cast_create())) return printf("APP bt cast create %d\n", e), -1;
        if ((e = esp_ble_audio_common_start(NULL))) return printf("APP bt cast common start %d\n", e), -1;
        xTaskCreatePinnedToCore(cast_task, "bt_cast", 10 * 1024, NULL, configMAX_PRIORITIES - 4, &s_cast_task, 0);   /* internal stack: stays on core 0 */
        printf("APP bt cast task core=%d (asked 0)\n", s_cast_task ? (int)xTaskGetCoreID(s_cast_task) : -9);
        const esp_timer_create_args_t ta = { .callback = cast_tick_cb, .name = "cast" };
        esp_timer_create(&ta, &s_cast_tick);
        s_cast_inited = true;
    } else if (!s_cast_src && cast_create()) return -1;
    if (cast_adv_start()) return -1;
    esp_ble_audio_bap_broadcast_adv_info_t info = { .adv_handle = CAST_ADV };
    esp_err_t e = esp_ble_audio_bap_broadcast_adv_add(&info);
    if (!e) e = esp_ble_audio_bap_broadcast_source_start(s_cast_src, CAST_ADV);
    if (e) return printf("APP bt cast start %d\n", e), -1;
    s_link_rate = (uint32_t)s_cast_rate;              /* the player's blocks are resampled to the broadcast rate */
    if (!s_lean) fx_mode_set(PC_FXM_CAST, true);       /* core 1 also runs LC3 (PIE): the HEAVY fx apps are suspended meanwhile */
    s_cast_on = true;
    esp_timer_start_periodic(s_cast_tick, 10000);
    if (s_lean) {                                      /* no player in this boot: the same sink set-up by hand (ui_cast_sink without player_ctl_changed) */
        s_hw = s_hr = 0; s_sink_sess++; hp_rsq_sink(true);
        audio_svc_set_remote_volume(NULL);
        audio_svc_set_sink(hp_sink);
    } else ui_async(ui_cast_sink, (void *)1);
    snprintf(s_status, sizeof s_status, TZ("%s「%s」"), T(S_BROADCASTING) /* 廣播中： */, CAST_NAME);
    memlog("bt_broadcast");
    return 0;
}

static void broadcast_off(void)
{
    s_cast_on = false;
    fx_mode_set(PC_FXM_CAST, false);
    if (s_cast_tick) esp_timer_stop(s_cast_tick);
    ui_async(ui_cast_sink, (void *)0);
    if (s_cast_src) {
        esp_ble_audio_bap_broadcast_source_stop(s_cast_src);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_ble_audio_bap_broadcast_source_delete(s_cast_src);
        s_cast_src = NULL;
    }
    esp_ble_audio_bap_broadcast_adv_info_t info = { .adv_handle = CAST_ADV };
    esp_ble_audio_bap_broadcast_adv_delete(&info);       /* or the next start's adv_add fails (found 2026-11-09) */
    /* stop and remove the advertising set, waiting for each step, so the next start can set it up again */
    xSemaphoreTake(s_adv_sem, 0);
    if (esp_ble_gap_periodic_adv_stop(CAST_ADV) == ESP_OK) xSemaphoreTake(s_adv_sem, pdMS_TO_TICKS(1000));
    uint8_t h = CAST_ADV;
    if (esp_ble_gap_ext_adv_stop(1, &h) == ESP_OK) xSemaphoreTake(s_adv_sem, pdMS_TO_TICKS(1000));
    if (esp_ble_gap_ext_adv_set_remove(CAST_ADV) == ESP_OK) xSemaphoreTake(s_adv_sem, pdMS_TO_TICKS(1000));
    for (int i = 0; i < 2; i++) s_cs[i].ready = false;
}
static bool cast_used(void) { return s_cast_inited; }
#else
static bool cast_used(void) { return false; }
int broadcast_on(void) { snprintf(s_status, sizeof s_status, "%s", T(S_BUILT_WITHOUT_AURACAST) /* 這個韌體沒有 Auracast */); return -1; }
static void broadcast_off(void) {}
#endif
/* ===================== Auracast listening (BAP broadcast sink): someone's broadcast -> our 3.5 mm ===================== */
#if CONFIG_BT_BAP_BROADCAST_SINK
typedef struct { char name[64]; uint8_t addr[6]; uint8_t addr_type, sid; uint32_t bid; } ls_found_t;
static ls_found_t s_found[LS_MAX];
int s_nfound;
static uint8_t s_want[6];                             /* the broadcaster the user picked (or remembered) */
static bool s_want_set;
static SemaphoreHandle_t s_ls_sem;
static esp_bt_status_t s_ls_st;
static esp_ble_audio_bap_broadcast_sink_t *s_sink;
static uint16_t s_ls_sync = 0xFFFF;
static volatile bool s_ls_syncing, s_ls_base;
static uint32_t s_ls_bits;
static esp_ble_audio_bap_stream_t s_ls_stream[2], *s_ls_p[2];
esp_audio_dec_handle_t s_lc3[2];
int s_ls_ch[2], s_ls_nstreams;
QueueHandle_t s_ls_q;
static char s_ls_name[64];

static void ls_gap_cb(esp_gap_ble_cb_event_t ev, esp_ble_gap_cb_param_t *p)   /* scan start/stop completions */
{
    if (ev == ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT) { s_ls_st = p->set_ext_scan_params.status; xSemaphoreGive(s_ls_sem); }
    else if (ev == ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT) { s_ls_st = p->ext_scan_start.status; xSemaphoreGive(s_ls_sem); }
    else if (ev == ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT) { s_ls_st = p->ext_scan_stop.status; xSemaphoreGive(s_ls_sem); }
}
static int ls_wait(esp_err_t e) { return e == ESP_OK && xSemaphoreTake(s_ls_sem, pdMS_TO_TICKS(2000)) && s_ls_st == ESP_BT_STATUS_SUCCESS ? 0 : -1; }
static int ls_scan(bool on)
{
    if (!on) return ls_wait(esp_ble_gap_stop_ext_scan());
    static esp_ble_ext_scan_params_t sp = { .own_addr_type = BLE_ADDR_TYPE_PUBLIC, .filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE, .cfg_mask = ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK,
        .uncoded_cfg = { .scan_type = BLE_SCAN_TYPE_PASSIVE, .scan_interval = 160, .scan_window = 160 } };
    if (ls_wait(esp_ble_gap_set_ext_scan_params(&sp))) return -1;
    return ls_wait(esp_ble_gap_start_ext_scan(0, 0));
}

/* one advertisement: broadcast name and Broadcast Audio Announcement (UUID 0x1852 + 24-bit id) */
typedef struct { char name[64]; uint32_t bid; bool has_bid; } ls_adv_t;
static bool ls_ad(uint8_t type, const uint8_t *d, uint8_t n, void *u)
{
    ls_adv_t *a = u;
    if ((type == 0x30 || type == 0x09 || (type == 0x08 && !a->name[0])) && n) {   /* Broadcast Name, Complete / Short Name */
        size_t k = n < sizeof a->name - 1 ? n : sizeof a->name - 1;
        memcpy(a->name, d, k);
        a->name[k] = 0;
    } else if (type == 0x16 && n >= 5 && d[0] == 0x52 && d[1] == 0x18) {        /* service data: Broadcast Audio Announcement */
        a->bid = (uint32_t)d[2] | (uint32_t)d[3] << 8 | (uint32_t)d[4] << 16;
        a->has_bid = true;
    }
    return true;
}
static void ls_try_sync(const ls_found_t *f)
{
    if (s_ls_syncing || s_sink) return;
    esp_ble_gap_periodic_adv_sync_params_t sp = { .filter_policy = 0, .sid = f->sid, .addr_type = f->addr_type, .skip = 0, .sync_timeout = 1000 };
    memcpy(sp.addr, f->addr, 6);
    if (esp_ble_gap_periodic_adv_create_sync(&sp) == ESP_OK) {
        s_ls_syncing = true;
        snprintf(s_ls_name, sizeof s_ls_name, "%s", f->name);
        snprintf(s_status, sizeof s_status, "%s%s", T(S_JOINING) /* 連上中： */, f->name);
        printf("APP bt listen pa sync -> %s\n", f->name);
    }
}
static void ls_iso_gap(esp_ble_audio_gap_app_event_t *e)   /* host task */
{
    if (e->type == ESP_BLE_AUDIO_GAP_EVENT_EXT_SCAN_RECV) {
        if (!e->ext_scan_recv.per_adv_itvl) return;             /* no periodic advertising: not a broadcast */
        ls_adv_t a = { 0 };
        esp_ble_audio_data_parse(e->ext_scan_recv.data, e->ext_scan_recv.data_len, ls_ad, &a);
        if (!a.has_bid) return;
        if (!a.name[0]) snprintf(a.name, sizeof a.name, "Broadcast %06lX", (unsigned long)a.bid);
        int at = -1;
        portENTER_CRITICAL(&s_dev_mux);
        for (int i = 0; i < s_nfound; i++) if (!memcmp(s_found[i].addr, e->ext_scan_recv.addr.val, 6)) at = i;
        if (at < 0 && s_nfound < LS_MAX) at = s_nfound++;
        if (at >= 0) {
            ls_found_t *f = &s_found[at];
            snprintf(f->name, sizeof f->name, "%s", a.name);
            memcpy(f->addr, e->ext_scan_recv.addr.val, 6);
            f->addr_type = e->ext_scan_recv.addr.type;
            f->sid = e->ext_scan_recv.sid;
            f->bid = a.bid;
            /* bt_dev_t view for the UI list */
            snprintf(s_devs[at].name, sizeof s_devs[at].name, "%s", a.name);
            memcpy(s_devs[at].bda, f->addr, 6);
            if (at >= s_ndevs) s_ndevs = at + 1;
        }
        ls_found_t f = at >= 0 ? s_found[at] : (ls_found_t){ 0 };
        portEXIT_CRITICAL(&s_dev_mux);
        if (at >= 0 && s_want_set && !memcmp(s_want, f.addr, 6)) ls_try_sync(&f);
    } else if (e->type == ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC) {
        s_ls_syncing = false;
        if (e->pa_sync.status) { snprintf(s_status, sizeof s_status, "%s", T(S_COULD_NOT_JOIN) /* 連不上這個廣播 */); return; }
        s_ls_sync = e->pa_sync.sync_handle;
        ls_scan(false);
        uint32_t bid = 0;
        for (int i = 0; i < s_nfound; i++) if (!memcmp(s_found[i].addr, s_want, 6)) bid = s_found[i].bid;
        esp_err_t er = esp_ble_audio_bap_broadcast_sink_create(s_ls_sync, bid, &s_sink);
        printf("APP bt listen pa synced, sink create %d\n", er);
    } else if (e->type == ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC_LOST) {
        if (e->pa_sync_lost.sync_handle != s_ls_sync) return;
        printf("APP bt listen lost reason=%d\n", e->pa_sync_lost.reason);
        s_ls_sync = 0xFFFF;
        s_ls_syncing = s_ls_base = false;
        if (s_sink) { esp_ble_audio_bap_broadcast_sink_delete(s_sink); s_sink = NULL; }
        ui_async(ui_stream, (void *)0);
        snprintf(s_status, sizeof s_status, "%s", T(S_LOST_SEARCHING) /* 訊號中斷，重新尋找… */);
        ls_scan(true);
    }
}
static void ls_base(esp_ble_audio_bap_broadcast_sink_t *sink, const esp_ble_audio_bap_base_t *base, size_t size)
{
    (void)sink; (void)size;
    uint32_t bits = 0;
    if (s_ls_base || esp_ble_audio_bap_base_get_bis_indexes(base, &bits)) return;
    s_ls_bits = 0;
    for (int i = 0, n = 0; i < 31 && n < 2; i++) if (bits & (1u << i)) { s_ls_bits |= 1u << i; n++; }   /* up to two BIS (left / right) */
    s_ls_base = true;
    printf("APP bt listen base bis=0x%lx use=0x%lx\n", (unsigned long)bits, (unsigned long)s_ls_bits);
}
static void ls_syncable(esp_ble_audio_bap_broadcast_sink_t *sink, const esp_ble_iso_biginfo_t *bi)
{
    if (!s_ls_bits) return;
    if (bi->encryption) { snprintf(s_status, sizeof s_status, "%s", T(S_ENCRYPTED_BROADCAST_NOT_SUPPORTED) /* 加密的廣播，需要密碼（不支援） */); return; }
    static uint8_t code[ESP_BLE_ISO_BROADCAST_CODE_SIZE];
    esp_err_t e = esp_ble_audio_bap_broadcast_sink_sync(sink, s_ls_bits, s_ls_p, code);
    printf("APP bt listen big sync %d\n", e);
}
static int ls_index(const esp_ble_audio_bap_stream_t *st) { return st == &s_ls_stream[1] ? 1 : 0; }
static void ls_started(esp_ble_audio_bap_stream_t *st)
{
    int i = ls_index(st);
    const esp_ble_audio_codec_cfg_t *cc = st->codec_cfg;
    esp_ble_audio_codec_cfg_freq_t fq;
    esp_ble_audio_codec_cfg_frame_dur_t fd;
    uint32_t hz = 48000, us = 10000;
    uint16_t oct = 100;
    esp_ble_audio_location_t loc = 0;
    if (cc) {
        if (!esp_ble_audio_codec_cfg_get_freq(cc, &fq)) esp_ble_audio_codec_cfg_freq_to_freq_hz(fq, &hz);
        if (!esp_ble_audio_codec_cfg_get_frame_dur(cc, &fd)) esp_ble_audio_codec_cfg_frame_dur_to_frame_dur_us(fd, &us);
        esp_ble_audio_codec_cfg_get_octets_per_frame(cc, &oct);
        esp_ble_audio_codec_cfg_get_chan_allocation(cc, &loc, true);
    }
    int ch = __builtin_popcount((unsigned)loc);
    s_ls_ch[i] = ch == 2 ? 2 : 1;                         /* 0 = mono (default location) */
    if (s_lc3[i]) { esp_audio_dec_close(s_lc3[i]); s_lc3[i] = NULL; }
    esp_lc3_dec_cfg_t lc = ESP_LC3_DEC_CONFIG_DEFAULT();
    lc.sample_rate = hz; lc.channel = (uint8_t)s_ls_ch[i]; lc.frame_dms = us == 7500 ? 75 : 100; lc.nbyte = oct;
    esp_audio_dec_cfg_t dc = { .type = ESP_AUDIO_TYPE_LC3, .cfg = &lc, .cfg_sz = sizeof lc };
    esp_lc3_dec_register();
    esp_audio_err_t r = esp_audio_dec_open(&dc, &s_lc3[i]);
    s_ls_nstreams = __builtin_popcount(s_ls_bits);
    s_rate = hz;
    printf("APP bt listen stream %d started rate=%lu frame_us=%lu octets=%u ch=%d dec=%d\n", i, (unsigned long)hz, (unsigned long)us, oct, s_ls_ch[i], r);
    snprintf(s_peer, sizeof s_peer, "%s", s_ls_name);
    snprintf(s_status, sizeof s_status, "%s%s", T(S_LISTENING) /* 收聽中： */, s_ls_name);
    if (i == 0) ui_async(ui_stream, (void *)1);
}
static void ls_stopped(esp_ble_audio_bap_stream_t *st, uint8_t reason) { printf("APP bt listen stream %d stopped %d\n", ls_index(st), reason); }
volatile uint32_t s_ls_qdrop, s_ls_ok[2], s_ls_inval[2], s_ls_gap[2];
static void ls_recv(esp_ble_audio_bap_stream_t *st, const esp_ble_iso_recv_info_t *info, const uint8_t *data, uint16_t len)   /* host task: hand over */
{
    static uint16_t last[2];
    static bool seen[2];
    int i = ls_index(st);
    ls_sdu_t sdu = { .idx = (uint8_t)i, .seq = info->seq_num };
    /* a missing or broken packet still takes its 10 ms: the decoder conceals it (PLC), the timeline stays whole */
    if (seen[i]) {
        uint16_t gap = (uint16_t)(info->seq_num - last[i]);
        if (gap > 1) s_ls_gap[i] += gap - 1;
        for (uint16_t k = 1; k < gap && k <= 5; k++) {
            ls_sdu_t lost = { .idx = (uint8_t)i, .len = 0, .seq = (uint16_t)(last[i] + k) };
            if (xQueueSend(s_ls_q, &lost, 0) != pdTRUE) s_ls_qdrop++;
        }
    }
    seen[i] = true;
    last[i] = info->seq_num;
    if ((info->flags & ESP_BLE_ISO_FLAGS_VALID) && len && len <= sizeof sdu.data) { sdu.len = (uint8_t)len; memcpy(sdu.data, data, len); s_ls_ok[i]++; }
    else s_ls_inval[i]++;
    if (xQueueSend(s_ls_q, &sdu, 0) != pdTRUE) s_ls_qdrop++;
}
/* LC3 uses the PIE unit, which only core 1 has: decode on a task of our own, not on the host task.
 * Two BIS (left / right) are paired by arrival, not by sequence number: the two streams' numbers need not match
 * (a source may start them apart). Each stream queues a few decoded frames; a pair goes out when both have one; if
 * one side falls 3 frames behind, the other plays on both channels so the sound never stops. */
static int listen_on(void)
{
    if (stack_up()) return -1;
    if (!s_ring) s_ring = heap_caps_malloc(RING_FRAMES * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    s_ls_sem = xSemaphoreCreateBinary();
    s_ls_q = xQueueCreateWithCaps(48, sizeof(ls_sdu_t), MALLOC_CAP_SPIRAM);
    if (!s_ring || !s_ls_sem || !s_ls_q) return -1;
    esp_ble_gap_register_callback(ls_gap_cb);
    esp_ble_audio_init_info_t info = { .gap_cb = ls_iso_gap };
    esp_err_t e = esp_ble_audio_common_init(&info);
    if (e) return printf("APP bt listen init %d\n", e), -1;
    static const esp_ble_audio_pacs_register_param_t pp = { .snk_pac = true, .snk_loc = true };
    esp_ble_audio_pacs_register(&pp);
    static uint8_t cd[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_DATA(ESP_BLE_AUDIO_CODEC_CAP_FREQ_16KHZ | ESP_BLE_AUDIO_CODEC_CAP_FREQ_24KHZ |
        ESP_BLE_AUDIO_CODEC_CAP_FREQ_32KHZ | ESP_BLE_AUDIO_CODEC_CAP_FREQ_48KHZ, ESP_BLE_AUDIO_CODEC_CAP_DURATION_10 | ESP_BLE_AUDIO_CODEC_CAP_DURATION_7_5,
        ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1, 2), 26, 155, 1);
    static uint8_t cm[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_META(ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA | ESP_BLE_AUDIO_CONTEXT_TYPE_CONVERSATIONAL);
    static const esp_ble_audio_codec_cap_t cap = ESP_BLE_AUDIO_CODEC_CAP_LC3(cd, cm);
    static esp_ble_audio_pacs_cap_t pc = { .codec_cap = &cap };
    esp_ble_audio_pacs_cap_register(ESP_BLE_AUDIO_DIR_SINK, &pc);
    static esp_ble_audio_bap_scan_delegator_cb_t sd = { 0 };
    esp_ble_audio_bap_scan_delegator_register(&sd);
    static esp_ble_audio_bap_broadcast_sink_cb_t sc = { .base_recv = ls_base, .syncable = ls_syncable };
    esp_ble_audio_bap_broadcast_sink_register_cb(&sc);
    static esp_ble_audio_bap_stream_ops_t ops = { .started = ls_started, .stopped = ls_stopped, .recv = ls_recv };
    for (int i = 0; i < 2; i++) { s_ls_stream[i].ops = &ops; s_ls_p[i] = &s_ls_stream[i]; }
    if ((e = esp_ble_audio_common_start(NULL))) return printf("APP bt listen start %d\n", e), -1;
    xTaskCreatePinnedToCoreWithCaps(ls_decode_task, "bt_lc3dec", 8192, NULL, configMAX_PRIORITIES - 5, NULL, 1, MALLOC_CAP_SPIRAM);
    s_want_set = bda_parse(cfg_str("bt_listen", ""), s_want);   /* the remembered broadcaster joins by itself */
    s_ndevs = 0;
    ls_scan(true);
    snprintf(s_status, sizeof s_status, "%s", T(S_LOOKING_FOR_BROADCASTS) /* 尋找廣播中… */);
    memlog("bt_listen");
    return 0;
}
static void ui_listen_saved(void *arg) { cfg_set_str("bt_listen", arg); free(arg); }   /* LVGL: remember it */
static void listen_pick(const uint8_t bda[6])
{
    memcpy(s_want, bda, 6);
    s_want_set = true;
    for (int i = 0; i < s_nfound; i++) if (!memcmp(s_found[i].addr, bda, 6)) { ls_found_t f = s_found[i]; ls_try_sync(&f); }
    char b[16];
    bda_str(bda, b);
    ui_async(ui_listen_saved, strdup(b));
}
#endif

/* Mode changes run on their own task with an internal-RAM stack: the Bluetooth bring-up writes flash (NVS),
 * which freezes the caches - a PSRAM stack (the LVGL task's) then asserts (found on the board 2026-11-09). */
static QueueHandle_t s_mode_q;
static int set_mode_now(bt_mode_t m)
{
    if (m == s_mode) return 0;
    if (!a2d_settled(2000)) return printf("APP bt mode=%d busy: earlier a2dp deinit still pending\n", m), -1;
    bool done = true;
    if (s_mode == BT_MODE_SPEAKER) done = a2d_off_done(speaker_off());
    if (s_mode == BT_MODE_HEADPHONES) done = a2d_off_done(headphones_off());
    if (s_mode == BT_MODE_BROADCAST) broadcast_off();
    s_mode = BT_MODE_OFF;
    s_status[0] = 0;
    int rc = 0;
    if (!done) rc = m == BT_MODE_OFF ? 0 : -1;           /* the stack is still up with a deinit in flight: no new mode now */
    else if (m == BT_MODE_SPEAKER) rc = speaker_on();
    else if (m == BT_MODE_HEADPHONES) { s_mode = BT_MODE_HEADPHONES; rc = headphones_on(); }
    else if (m == BT_MODE_BROADCAST) rc = broadcast_on();
#if CONFIG_BT_BAP_BROADCAST_SINK
    else if (m == BT_MODE_LISTEN) rc = listen_on();       /* no off: modes change by reboot */
#endif
    if (rc == 0) s_mode = m;
    if (s_mode == BT_MODE_OFF && !s_a2d_pending) stack_down();
    printf("APP bt mode=%d rc=%d\n", m, rc);
    return rc;
}

static void ui_saved(void *arg) { cfg_set_int("bt_mode", (int)(intptr_t)arg); vol_out_mode_changed(); player_ctl_changed(); }   /* the mode is set (the mode task set s_mode before this): the jack takes its value */
static void mode_task(void *arg)
{
    (void)arg;
    bt_mode_t m;
    for (;;)
        if (xQueueReceive(s_mode_q, &m, s_a2d_pending ? pdMS_TO_TICKS(1000) : portMAX_DELAY) != pdTRUE) {
            if (s_a2d_pending && a2d_settled(0)) {         /* the late deinit-done event: finish the teardown here, not in the callback */
                printf("APP bt a2dp deinit done late\n");
                if (s_mode == BT_MODE_OFF) stack_down();
            }
        } else {
            if (m == BT_MODE_COUNT) {                      /* hp_codec_failed: headphones again, without that codec */
                if (s_mode == BT_MODE_HEADPHONES) {
                    int rc = -1;
                    if (a2d_off_done(headphones_off())) rc = headphones_on();
                    if (rc && s_mode == BT_MODE_HEADPHONES && !s_a2d_pending) {   /* once more, after the stack settles */
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        if (a2d_off_done(headphones_off())) rc = headphones_on();
                    }
                    if (s_a2d_pending) { s_mode = BT_MODE_OFF; ui_async(ui_saved, (void *)(intptr_t)0); }
                    printf("APP bt hp restart rc=%d\n", rc);
                }
                continue;
            }
            int rc = set_mode_now(m);
            ui_async(ui_saved, (void *)(intptr_t)(rc == 0 ? (int)m : 0));   /* cfg is LVGL-side */
        }
}

static void hp_restart_request(void)
{
    bt_mode_t m = BT_MODE_COUNT;                         /* internal: restart headphones mode */
    if (!s_mode_q || xQueueSend(s_mode_q, &m, 0) != pdTRUE) printf("APP bt hp restart not queued\n");
}

/* BTAUDIO: a stored preference (bt_codec / bt_rate / bt_bits / bt_chan / ldac_kbps) takes effect by one reconnect of the sink (the endpoints are registered again), never a reboot */
void bt_svc_apply_prefs(void)                            /* LVGL: one reconnect only when what the endpoints are made of changed (FIX4 M7: not for ldac_kbps, bt_bits, 48k vs 96k) */
{
    const bool need = pf_reconnect_needed();
    pf_take();                                           /* the snapshot the endpoints are registered with (M6: taken here on LVGL, read by the Bluetooth task) */
    if (need && s_mode == BT_MODE_HEADPHONES) hp_restart_request();
}

int bt_svc_set_mode(bt_mode_t m)                         /* LVGL: queued; bt_svc_mode() / status follow */
{
    if (m >= BT_MODE_COUNT) return -1;
    pf_take();                                           /* the endpoints a mode entry registers are made from the stored preferences of now */
    if (!s_mode_q) {
        s_mode_q = xQueueCreate(4, sizeof(bt_mode_t));
        xTaskCreatePinnedToCore(mode_task, "bt_mode", 6144, NULL, 3, NULL, 0);   /* internal RAM stack */
    }
    snprintf(s_status, sizeof s_status, "%s", T(S_SWITCHING) /* 切換中… */);
    return xQueueSend(s_mode_q, &m, 0) == pdTRUE ? 0 : -1;
}

void bt_svc_start(void)
{
    int m = cfg_int("bt_mode", 0);
    if (m > 0 && m < BT_MODE_COUNT) bt_svc_set_mode((bt_mode_t)m);
}

#else
bool bt_svc_pair_pending(char digits[7]) { (void)digits; return false; }
void bt_svc_pair_reply(bool accept) { (void)accept; }
bool bt_svc_pair_check_timeout(void) { return false; }
int bt_svc_paired_count(void) { return -1; }
int bt_svc_forget_all(void) { return -1; }
void bt_svc_scan(void) {}
int bt_svc_devices(bt_dev_t *out, int max) { (void)out; (void)max; return 0; }
void bt_svc_connect(const uint8_t bda[6]) { (void)bda; }
int bt_svc_ldac_kbps(int kbps) { (void)kbps; return -1; }
const char *bt_svc_codec(void) { return ""; }
void bt_svc_out_in(pc_os_bt_in_t *in) { (void)in; }
void bt_svc_apply_prefs(void) {}
void bt_svc_applied_prefs(pc_os_bt_in_t *in) { (void)in; }
bool bt_svc_prefs_pending(void) { return false; }
uint32_t bt_svc_sink_underflows(void) { return 0; }   /* hires T8e fix 0: no sink */
bool bt_svc_available(void) { return false; }
int bt_svc_set_mode(bt_mode_t m) { (void)m; return -1; }
void bt_svc_start(void) {}
#endif
