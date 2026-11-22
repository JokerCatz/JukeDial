// SPDX-License-Identifier: LGPL-2.1-or-later
/* Speaker mode's AVRCP controller, the IDF glue (AVRCP 1.6 step 1; fix round 1). The phone is the AVRCP target. Every decision is the pure core
 * pc_bma_* (pc_btmeta.h, host-tested), driven by bt_meta.c on the LVGL task; this file only turns Bluedroid callbacks into core events and the
 * core's actions into esp_avrc_ct_* calls (the simulator's fake phone, fake_bt_svc.c, plays the same part against the same core).
 * Tasks: the callback (BTC task) copies an event's bytes into PSRAM (outside any lock: the attribute texts and the cover's pieces must be copied
 * before it returns) and appends the event to a ring under s_mux (a few words: interrupts are off only for that); bt_meta's 50 ms tick drains the
 * ring in order on the LVGL task (poll) and runs the actions there. A full ring drops the event and says so (PC_BMA_LOST: the core re-asks and
 * drops a cover in progress). The cover-art client's buffers come from osi_malloc, PSRAM first (CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST). */
#include "bt_meta_avrc.h"
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_CLASSIC_ENABLED && CONFIG_BT_AVRCP_ENABLED
#include "bt_meta.h"
#include "esp_avrc_api.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"

#define TL_CAPS 0                       /* transaction labels: one per kind (bt_remote.c's are another mode's) */
#define TL_NOTE 1
#define TL_META 2
#define RING 128                        /* events between two ticks (50 ms): a thumbnail comes in ~1 KB pieces */

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static struct { pc_bma_ev_t ev; uint8_t *copy; } s_ring[RING];   /* PSRAM (.bss) */
static uint32_t s_head, s_tail;         /* under s_mux */
static bool s_lost;
static volatile bool s_ct;              /* CT is up (speaker mode): the core's actions reach the stack only then (headphones mode has its own CT) */

/* ---------- any task (BTC, mode): one event in ---------- */
static void push(uint8_t kind, bool on, uint8_t id, const uint8_t *data, int len, bool final, int status)
{
    uint8_t *c = NULL;
    if (data && len > 0) {
        c = heap_caps_malloc((size_t)len, MALLOC_CAP_SPIRAM);
        if (!c) { kind = PC_BMA_LOST; len = 0; }          /* the bytes are gone: say so */
        else memcpy(c, data, (size_t)len);
    } else len = 0;
    bool full, first;
    portENTER_CRITICAL(&s_mux);
    first = s_head == s_tail;
    if (s_lost && RING - (s_head - s_tail) >= 2) {      /* the gap is marked where it happened: the core sees LOST before anything after it */
        s_ring[s_head % RING].ev = (pc_bma_ev_t){ .kind = PC_BMA_LOST };
        s_ring[s_head % RING].copy = NULL;
        s_head++;
        s_lost = false;
    }
    full = s_lost || s_head - s_tail >= RING;           /* still a gap pending (no room for its mark): this one is lost too */
    if (full) s_lost = true;
    else {
        s_ring[s_head % RING].ev = (pc_bma_ev_t){ .kind = kind, .on = on, .id = id, .data = c, .len = len, .final = final, .status = status };
        s_ring[s_head % RING].copy = c;
        s_head++;
    }
    portEXIT_CRITICAL(&s_mux);
    if (full) heap_caps_free(c);
    if (first || bt_meta_asleep()) bt_meta_wake();      /* the tick sleeps while no request is open; a wake that could not be queued is tried again */
}

/* ---------- LVGL task: the ring into the core, the core's actions out ---------- */
static void poll(void)
{
    for (;;) {
        pc_bma_ev_t ev;
        uint8_t *c = NULL;
        bool got, lost;
        portENTER_CRITICAL(&s_mux);
        got = s_tail != s_head;
        if (got) { ev = s_ring[s_tail % RING].ev; c = s_ring[s_tail % RING].copy; s_tail++; }
        lost = !got && s_lost;
        if (lost) s_lost = false;
        portEXIT_CRITICAL(&s_mux);
        if (lost) { ev = (pc_bma_ev_t){ .kind = PC_BMA_LOST }; bt_meta_event(&ev); }   /* a gap at the very end (no push since) */
        if (!got) return;
        if (ev.kind == PC_BMA_LOST) printf("APP bt meta events lost (ring full)\n");
        bt_meta_event(&ev);
        heap_caps_free(c);
    }
}
static void ask_caps(void)
{
    if (!s_ct) return;
    esp_err_t e = esp_avrc_ct_send_get_rn_capabilities_cmd(TL_CAPS);
    if (e != ESP_OK) printf("APP bt meta caps rc=%s\n", esp_err_to_name(e));
}
static void reg_track(void) { if (s_ct) esp_avrc_ct_send_register_notification_cmd(TL_NOTE, ESP_AVRC_RN_TRACK_CHANGE, 0); }
static void ask_meta(uint8_t mask)      /* PC_BTMETA_* are the bits of esp_avrc_md_attr_mask_t */
{
    if (!s_ct) return;
    esp_err_t e = esp_avrc_ct_send_metadata_cmd(TL_META, mask);
    if (e != ESP_OK) printf("APP bt meta ask rc=%s\n", esp_err_to_name(e));
}
static void ca_connect(void)
{
    if (!s_ct) return;
    esp_err_t e = esp_avrc_ct_cover_art_connect(ESP_AVRC_CA_MTU_MAX);
    if (e != ESP_OK) { printf("APP bt art connect rc=%s\n", esp_err_to_name(e)); push(PC_BMA_CA, false, 0, NULL, 0, false, -1); }
}
static void ca_drop(void)
{
    if (!s_ct) return;
    esp_err_t e = esp_avrc_ct_cover_art_disconnect();
    if (e != ESP_OK) { printf("APP bt art drop rc=%s\n", esp_err_to_name(e)); push(PC_BMA_CA, false, 0, NULL, 0, false, -1); }
}
static void get(const char *handle)
{
    if (!s_ct) return;
    uint8_t h[ESP_AVRC_CA_IMAGE_HANDLE_LEN];
    memcpy(h, handle, sizeof h);
    esp_err_t e = esp_avrc_ct_cover_art_get_linked_thumbnail(h);
    if (e != ESP_OK) { printf("APP bt art send rc=%s\n", esp_err_to_name(e)); push(PC_BMA_DATA, false, 0, NULL, 0, true, -1); }
}
static const bt_meta_phone_t k_phone = { .poll = poll, .ask_caps = ask_caps, .reg_track = reg_track, .ask_meta = ask_meta, .ca_connect = ca_connect,
                                         .ca_drop = ca_drop, .get = get };

/* ---------- BTC task ---------- */
static void ct_cb(esp_avrc_ct_cb_event_t ev, esp_avrc_ct_cb_param_t *p)
{
    switch (ev) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        printf("APP bt meta avrc link=%d\n", p->conn_stat.connected);
        push(PC_BMA_LINK, p->conn_stat.connected, 0, NULL, 0, false, 0);   /* down: the core frees a cover in progress */
        break;
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        printf("APP bt meta avrc feat=%lx tg=%x\n", (unsigned long)p->rmt_feats.feat_mask, (unsigned)p->rmt_feats.tg_feat_flag);
        push(PC_BMA_FEAT, (p->rmt_feats.tg_feat_flag & ESP_AVRC_FEAT_FLAG_TG_COVER_ART) != 0, 0, NULL, 0, false, 0);
        break;
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        esp_avrc_rn_evt_cap_mask_t m = p->get_rn_caps_rsp.evt_set;
        bool tc = esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &m, ESP_AVRC_RN_TRACK_CHANGE);
        printf("APP bt meta avrc up track_changed=%d\n", tc);
        push(PC_BMA_CAPS, tc, 0, NULL, 0, false, 0);
        break;
    }
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:                /* CHANGED only (the stack drops the INTERIM one) */
        if (p->change_ntf.event_id == ESP_AVRC_RN_TRACK_CHANGE) push(PC_BMA_TRACK, false, 0, NULL, 0, false, 0);
        break;
    case ESP_AVRC_CT_METADATA_RSP_EVT:                 /* the text is copied here, cleaned on the LVGL task */
        push(PC_BMA_ATTR, false, p->meta_rsp.attr_id, p->meta_rsp.attr_text, p->meta_rsp.attr_length, false, 0);
        break;
    case ESP_AVRC_CT_COVER_ART_STATE_EVT: {
        bool on = p->cover_art_state.state == ESP_AVRC_COVER_ART_CONNECTED;
        printf("APP bt art link=%d reason=%d\n", on, p->cover_art_state.reason);
        push(PC_BMA_CA, on, 0, NULL, 0, false, p->cover_art_state.reason);
        break;
    }
    case ESP_AVRC_CT_COVER_ART_DATA_EVT: {             /* p_data is valid only during this call: copied by push */
        bool ok = p->cover_art_data.status == ESP_BT_STATUS_SUCCESS;
        if (!ok) printf("APP bt art status=%d\n", p->cover_art_data.status);
        push(PC_BMA_DATA, false, 0, ok ? p->cover_art_data.p_data : NULL, ok ? p->cover_art_data.data_len : 0, ok ? p->cover_art_data.final : true,
             ok ? 0 : p->cover_art_data.status ? p->cover_art_data.status : -1);
        break;
    }
    case ESP_AVRC_CT_PROF_STATE_EVT:
        if (p->avrc_ct_init_stat.state != ESP_AVRC_INIT_SUCCESS && p->avrc_ct_init_stat.state != ESP_AVRC_DEINIT_SUCCESS)
            printf("APP bt meta avrc init failed ct_state=%d\n", p->avrc_ct_init_stat.state);
        break;
    default:
        break;
    }
}

/* ---------- mode task ---------- */
void bt_meta_avrc_start(void)
{
    esp_err_t e;
    if ((e = esp_avrc_ct_register_callback(ct_cb)) != ESP_OK || (e = esp_avrc_ct_init()) != ESP_OK) {
        printf("APP bt meta avrc init failed %s\n", esp_err_to_name(e));   /* the stream plays without the track's name */
        return;
    }
    s_ct = true;
    bt_meta_attach(&k_phone);
}

void bt_meta_avrc_stop(void)
{
    if (!s_ct) return;
    s_ct = false;                       /* first: no action of the core reaches the stack after this (the next mode's CT may be up) */
    push(PC_BMA_LINK, false, 0, NULL, 0, false, 0);   /* the core forgets the track and frees a cover in progress (in order, after what is queued) */
    esp_avrc_ct_deinit();               /* before the A2DP deinit; queued in order on the BTC task (the cover-art client closes with it) */
}

#else
void bt_meta_avrc_start(void) {}
void bt_meta_avrc_stop(void) {}
#endif
