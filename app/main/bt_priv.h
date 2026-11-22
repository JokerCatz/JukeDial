// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Private to the Bluetooth service: the state and the internal functions that bt_svc.c, bt_svc_a2dp.c, bt_svc_pair.c and bt_svc_cast_tx.c share. Everything here was a
 * static in the one bt_svc.c; the public API stays in bt_svc.h. Nothing outside these four files includes this. */

#include "bt_svc.h"
#include "esp_attr.h"
#include "pc/pc_thumbbg.h"
#include <stdio.h>
#include <string.h>
#include "cfg.h"
#include "sdkconfig.h"
#include "ui/font.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_CLASSIC_ENABLED
#include "audio_svc.h"
#include "vol_out.h"
#include "bt_meta.h"
#include "bt_meta_avrc.h"
#include "bt_remote.h"
#include "esp_a2dp_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_heap_caps.h"
#include "esp_audio_dec.h"
#include "esp_aac_dec.h"
#include "esp_sbc_dec.h"
#include "esp_audio_enc.h"
#include "esp_aac_enc.h"
#include "esp_sbc_enc.h"
#include "esp_timer.h"
#include "pc/pc_castq.h"
#include "pc/pc_btcodec.h"
#include "wifi_svc.h"
#include "pc/pc_mix.h"
#include "pc/pc_rate.h"
#include "pc/pc_pipe_rules.h"
#include "pc/pc_rsq.h"
#include "pc/pc_pair.h"
#include "ui/screens.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "memlog.h"
#include "nvs_flash.h"
#include "player_ctl.h"
#include "ui/ui.h"

#define DEV_NAME "JukeDial"
#define RING_FRAMES (48000 / 2)          /* 0.5 s of stereo int16 at 48 kHz (PSRAM) */
#define TARGET_MS 120                    /* fill the mixer keeps the ring at: absorbs radio jitter */
#define HP_MAX 16
#define HP_RING (48000 / 2)                    /* 0.5 s at the link rate, stereo int16, PSRAM */
#define HP_FILL (48000 / 5)                    /* the player waits above 0.2 s: latency, not the ring's size */
#if CONFIG_BT_BAP_BROADCAST_SOURCE
#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_bap_lc3_preset_defs.h"
#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_audio_lc3_defs.h"
#include "esp_ble_audio_pbp_api.h"
#include "esp_gap_ble_api.h"
#include "esp_lc3_enc.h"
#include "freertos/semphr.h"
#include "fx_state.h"

#define CAST_NAME "JukeDial"
#define CAST_ID 0x4A4350                        /* "JCP" */
#define CAST_ADV 0
#endif
#if CONFIG_BT_BAP_BROADCAST_SINK
#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_audio_lc3_defs.h"
#include "esp_ble_audio_pacs_api.h"
#include "esp_gap_ble_api.h"
#include "esp_lc3_dec.h"
#include "freertos/semphr.h"

#define LS_MAX 12
#endif
#if CONFIG_BT_BAP_BROADCAST_SINK
typedef struct { uint8_t idx, len; uint16_t seq; uint8_t data[160]; } ls_sdu_t;
#endif

typedef struct { pc_rsq_swap_t sw; pc_rsq_pair_t waited; uint32_t dropped, dropped_total; TaskHandle_t task; volatile bool on; } bt_hp_rs_t;
extern bt_hp_rs_t s_hp_rs;
#if CONFIG_BT_BAP_BROADCAST_SOURCE
typedef struct { esp_ble_audio_bap_stream_t stream; uint16_t seq; volatile bool ready; } bt_cs_t;
extern bt_cs_t s_cs[2];
#endif


/* defined in bt_svc.c */
extern bt_mode_t s_mode;
extern char s_status[128];
extern char s_peer[64];
extern volatile bool s_spk_connected, s_spk_leaving;   /* speaker mode: the phone's link (bt_svc.c speaker_off) */
extern uint8_t s_spk_bda[6];
extern volatile bool s_hp_connected, s_hp_active;
void ui_stream(void *arg);
void ui_status(void *arg);
extern SemaphoreHandle_t s_a2d_down;
extern SemaphoreHandle_t s_a2d_up;
extern bool s_stack_up;
#if JC_LDAC
void hp_codec_failed(pc_btc_t c, const char *why_zh, const char *why_en);
#endif
void ui_remember(void *arg);
int broadcast_on(void);
#if CONFIG_BT_BAP_BROADCAST_SINK
extern int s_nfound;
extern esp_audio_dec_handle_t s_lc3[2];
extern int s_ls_ch[2], s_ls_nstreams;
extern QueueHandle_t s_ls_q;
extern volatile uint32_t s_ls_qdrop, s_ls_ok[2], s_ls_inval[2], s_ls_gap[2];
#endif
/* defined in bt_svc_a2dp.c */
extern volatile uint32_t s_sink_sess;
void sink_block(bool dry);
extern int16_t *s_ring;
extern volatile uint32_t s_w, s_r;
extern QueueHandle_t s_raw_q;
extern uint32_t s_rate;
void decode_task(void *arg);
extern bool s_primed;
int mix_render(float *out, int frames, void *u);
void mix_release(void *u);
void pf_take(void);
bool pf_reconnect_needed(void);
void a2d_cb(esp_a2d_cb_event_t ev, esp_a2d_cb_param_t *p);
void data_cb(esp_a2d_conn_hdl_t h, esp_a2d_audio_buff_t *b);
extern bt_dev_t s_devs[HP_MAX];
extern int s_ndevs;
extern portMUX_TYPE s_dev_mux;
extern uint8_t s_hp_bda[6];
extern volatile bool s_hp_streaming;
extern volatile uint32_t s_hp_gen;
extern volatile unsigned s_hp_skip;
extern char s_hp_note[64];
extern uint16_t s_hp_mtu;
extern uint32_t s_link_rate;
extern int16_t *s_hp_ring;
extern volatile uint32_t s_hw, s_hr;
void hp_rsq_alloc(void);
void hp_sink(const float *lr, int frames, uint32_t rate);
#if JC_LDAC
void hp_venc_task(void *arg);
#endif
void hp_encode_task(void *arg);
void hp_rsq_sink(bool on);
void ui_hp_stream(void *arg);
extern esp_timer_handle_t s_rc_t;
extern int s_rc_n;
extern volatile bool s_hp_leaving;
#if CONFIG_BT_BAP_BROADCAST_SINK
void ls_decode_task(void *arg);
#endif
/* defined in bt_svc_pair.c */
void gap_cb(esp_bt_gap_cb_event_t ev, esp_bt_gap_cb_param_t *p);
/* defined in bt_svc_cast_tx.c */
#if CONFIG_BT_BAP_BROADCAST_SOURCE
extern int s_cast_q, s_cast_rate, s_cast_nbyte;
extern esp_ble_audio_bap_broadcast_source_t *s_cast_src;
extern bool s_cast_inited;
extern volatile bool s_cast_on;
extern SemaphoreHandle_t s_adv_sem;
extern esp_timer_handle_t s_cast_tick;
extern TaskHandle_t s_cast_task;
void cast_gap_cb(esp_gap_ble_cb_event_t ev, esp_ble_gap_cb_param_t *p);
int cast_adv_start(void);
void cast_tick_cb(void *arg);
void cast_task(void *arg);
int cast_create(void);
void ui_cast_sink(void *arg);
#endif
#endif
