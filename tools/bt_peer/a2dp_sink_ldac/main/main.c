/* JC Peer LDAC: an A2DP sink for testing the player's vendor codecs. It offers LDAC (44.1/48 kHz stereo), aptX HD
 * and aptX (44.1/48 kHz stereo), chosen at build time with PEER_CODECS, and SBC as the fallback.
 * - LDAC: not decoded (no licensed decoder here). Every packet is checked instead: each frame starts with the
 *   sync byte 0xAA, has the negotiated rate and channel config, the frames fill the payload exactly and their
 *   number matches the media header (pc_ldac_check, host-tested against libldac's own frames).
 * - aptX / aptX HD: decoded with libfreeaptx on core 1; the decoded left channel is checked as a test tone
 *   (dominant frequency, level, SNR of a sine fit) - play `tone 1000` on the player.
 * Every 5 s it prints codec, packets/s, payload kbps, timestamp gaps and the checks; a pause of more than 100 ms
 * between packets (the player's codec steps) is printed when it ends.
 * Needs the patched Bluedroid from tools/bt_ldac (see ../README.md). */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "bt_app_core_utils.h"
#include "bredr_app_common_utils.h"
#include "freeaptx.h"
#include "pc/pc_btcodec.h"

#ifndef PEER_CODECS
#define PEER_CODECS 7
#endif
#ifndef PEER_44K_ONLY
#define PEER_44K_ONLY 0
#endif
#define LDAC_SF (PEER_44K_ONLY ? ESP_A2D_LDAC_SF_44K : ESP_A2D_LDAC_SF_44K | ESP_A2D_LDAC_SF_48K)
#define APTX_SF (PEER_44K_ONLY ? ESP_A2D_APTX_SF_44K : ESP_A2D_APTX_SF_44K | ESP_A2D_APTX_SF_48K)
#define NAME "JC Peer LDAC"

static esp_a2d_mcc_t s_mcc;
static pc_btc_t s_codec = PC_BTC_NONE;
static uint32_t s_rate = 48000;
/* counters, written by the BTC task (data callback) or the decoder, read by the stats task */
static volatile uint32_t s_pkts, s_bytes, s_frames, s_gaps, s_gap_samples, s_bad_hdr, s_minlen = 0xFFFF, s_maxlen;
static volatile uint32_t s_ldac_bad_pkts, s_ldac_bad_frames, s_ldac_fbytes;
static uint32_t s_last_ts, s_last_step, s_run_step, s_run;
static bool s_have_ts;
/* aptX decoding */
static QueueHandle_t s_q;
static volatile uint32_t s_dec_samples, s_dec_dropped, s_dec_unsync, s_q_full;
static volatile int64_t s_dec_us;
static volatile bool s_dec_reset;
static float s_tone_buf[8192];
static volatile int s_tone_fill;           /* the decoder fills it, the stats task analyses it and starts it over */

static pc_btc_t codec_of(const esp_a2d_mcc_t *m)
{
    if (m->type == ESP_A2D_MCT_SBC) return PC_BTC_SBC;
    if (m->type == ESP_A2D_MCT_M24) return PC_BTC_AAC;
    if (m->type != ESP_A2D_MCT_NON_A2DP) return PC_BTC_NONE;
    uint32_t v = m->cie.vendor_info.vendor_id;
    uint16_t c = m->cie.vendor_info.codec_id;
    if (v == ESP_A2D_LDAC_VENDOR_ID && c == ESP_A2D_LDAC_CODEC_ID) return PC_BTC_LDAC;
    if (v == ESP_A2D_APTX_HD_VENDOR_ID && c == ESP_A2D_APTX_HD_CODEC_ID) return PC_BTC_APTX_HD;
    if (v == ESP_A2D_APTX_VENDOR_ID && c == ESP_A2D_APTX_CODEC_ID) return PC_BTC_APTX;
    return PC_BTC_NONE;
}

static void gaps(uint32_t ts)
{
    if (s_have_ts) {
        /* The RTP timestamp step between packets is constant for a CBR stream (samples per packet). A step of 2x or
         * more means packets were lost. A new constant step (rate changed, the source restarted) becomes the new
         * reference after 4 packets in a row, and those are not counted as gaps. */
        uint32_t step = ts - s_last_ts;
        if (!s_last_step || step > 1000000) { s_last_step = step; s_run = 0; }   /* first, or timestamp restarted */
        else if (step >= 2 * s_last_step) {
            if (step == s_run_step && ++s_run >= 3) { s_gaps -= s_run - 1; s_gap_samples -= (s_run - 1) * (step - s_last_step); s_last_step = step; s_run = 0; }
            else { if (step != s_run_step) { s_run_step = step; s_run = 1; } s_gaps++; s_gap_samples += step - s_last_step; }
        } else { s_last_step = step; s_run = 0; }
    }
    s_last_ts = ts;
    s_have_ts = true;
}

static int64_t s_last_pkt_us;
static volatile uint32_t s_silences, s_silence_ms;     /* pauses of more than 100 ms between packets (codec steps) */

static void data_cb(esp_a2d_conn_hdl_t h, esp_a2d_audio_buff_t *b)   /* BTC task */
{
    (void)h;
    if (!b) return;
    uint32_t len = b->data_len;
    int64_t now = esp_timer_get_time();
    if (s_last_pkt_us && now - s_last_pkt_us > 100000) {
        s_silences++;
        s_silence_ms += (uint32_t)((now - s_last_pkt_us) / 1000);
        printf("PEER silence %lld ms before this %s packet (total %" PRIu32 " pauses, %" PRIu32 " ms)\n", (now - s_last_pkt_us) / 1000,
               pc_btc_name(s_codec), (uint32_t)s_silences, (uint32_t)s_silence_ms);
    }
    s_last_pkt_us = now;
    s_pkts++;
    s_bytes += len + (s_codec == PC_BTC_LDAC || s_codec == PC_BTC_SBC);   /* + the one-octet media header the stack strips */
    s_frames += b->number_frame;
    if (!b->number_frame) s_bad_hdr++;
    if (len < s_minlen) s_minlen = len;
    if (len > s_maxlen) s_maxlen = len;
    if (s_codec != PC_BTC_APTX) gaps(b->timestamp);                        /* classic aptX has no RTP timestamp */
    if (s_codec == PC_BTC_LDAC) {
        pc_ldac_check_t c;
        pc_ldac_check(b->data, (int)len, b->number_frame, pc_ldac_rate_id(s_rate), 2, &c);
        if (!c.ok) {
            if (s_ldac_bad_pkts++ < 5)
                printf("PEER LDAC bad packet len=%" PRIu32 " hdr_frames=%u found=%d bad=%d first=%02x %02x %02x\n", len,
                       b->number_frame, c.frames, c.bad_frames, b->data[0], len > 1 ? b->data[1] : 0, len > 2 ? b->data[2] : 0);
            s_ldac_bad_frames += (uint32_t)c.bad_frames;
        }
        s_ldac_fbytes = (uint32_t)c.frame_bytes;
    }
    if ((s_codec == PC_BTC_APTX || s_codec == PC_BTC_APTX_HD) && s_q) {
        if (xQueueSend(s_q, &b, 0) == pdTRUE) return;                    /* the decoder frees it */
        s_q_full++;
    }
    esp_a2d_audio_buff_free(b);
}

static void decode_task(void *arg)                                       /* core 1 */
{
    (void)arg;
    struct aptx_context *ctx = NULL;
    int ctx_hd = -1;
    static uint8_t out[8192];
    for (;;) {
        esp_a2d_audio_buff_t *b;
        if (xQueueReceive(s_q, &b, portMAX_DELAY) != pdTRUE) continue;
        int hd = s_codec == PC_BTC_APTX_HD;
        if (!ctx || hd != ctx_hd || s_dec_reset) {
            if (ctx) aptx_finish(ctx);
            ctx = aptx_init(hd);
            ctx_hd = hd;
            s_dec_reset = false;
        }
        int64_t t = esp_timer_get_time();
        size_t written = 0, dropped = 0;
        int synced = 0;
        aptx_decode_sync(ctx, b->data, b->data_len, out, sizeof out, &written, &synced, &dropped);
        s_dec_us += esp_timer_get_time() - t;
        if (!synced) s_dec_unsync++;
        s_dec_dropped += (uint32_t)dropped;
        int n = (int)(written / 6);                                        /* 24-bit stereo */
        s_dec_samples += (uint32_t)n;
        for (int i = 0; i < n && s_tone_fill < (int)(sizeof s_tone_buf / sizeof s_tone_buf[0]); i++) {
            const uint8_t *p = out + 6 * i;                                /* left channel */
            int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24);
            s_tone_buf[s_tone_fill++] = v / 2147483648.0f;
        }
        esp_a2d_audio_buff_free(b);
    }
}

static void stats_task(void *arg)
{
    (void)arg;
    uint32_t p0 = 0, by0 = 0, f0 = 0, g0 = 0, ds0 = 0;
    int64_t t0 = esp_timer_get_time(), du0 = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        int64_t t = esp_timer_get_time();
        float dt = (t - t0) / 1e6f;
        uint32_t p = s_pkts, by = s_bytes, f = s_frames, g = s_gaps, ds = s_dec_samples;
        int64_t du = s_dec_us;
        if (p != p0) {
            printf("PEER stats codec=%s rate=%" PRIu32 " pkts/s=%.1f kbps=%.1f frames/pkt=%.2f len=%" PRIu32 "..%" PRIu32
                   " gaps=%" PRIu32 " (total %" PRIu32 ", %" PRIu32 " samples) badhdr=%" PRIu32 " heap=%u\n",
                   pc_btc_name(s_codec), s_rate, (p - p0) / dt, (by - by0) * 8 / dt / 1000.0f, (float)(f - f0) / (float)(p - p0),
                   (uint32_t)s_minlen, (uint32_t)s_maxlen, g - g0, g, (uint32_t)s_gap_samples, (uint32_t)s_bad_hdr,
                   (unsigned)esp_get_free_heap_size());
            if (s_codec == PC_BTC_LDAC)
                printf("PEER ldac check frame=%" PRIu32 "B (%d kbps) bad_packets=%" PRIu32 " bad_frames=%" PRIu32 " (totals)\n",
                       (uint32_t)s_ldac_fbytes, pc_ldac_kbps((int)s_ldac_fbytes, s_rate), (uint32_t)s_ldac_bad_pkts, (uint32_t)s_ldac_bad_frames);
            if (s_codec == PC_BTC_APTX || s_codec == PC_BTC_APTX_HD) {
                pc_tone_t tone = { 0 };
                bool ok = false;
                if (s_tone_fill >= 8192) ok = pc_tone_analyse(s_tone_buf, 8192, (float)s_rate, &tone);
                printf("PEER aptx decoded=%.0f samples/s (%.2f%% of %" PRIu32 ") dec=%.1f%% unsynced=%" PRIu32 " dropped=%" PRIu32
                       " qfull=%" PRIu32 " tone=%s %.2f Hz %.2f dBFS snr=%.1f dB\n",
                       (ds - ds0) / dt, (ds - ds0) / dt * 100.0f / s_rate, s_rate, (du - du0) / 1e4f / dt, (uint32_t)s_dec_unsync,
                       (uint32_t)s_dec_dropped, (uint32_t)s_q_full, ok ? "yes" : "no", tone.freq_hz, tone.level_db, tone.snr_db);
                s_tone_fill = 0;                                           /* the next block */
            }
            s_minlen = 0xFFFF; s_maxlen = 0;
        }
        p0 = p; by0 = by; f0 = f; g0 = g; t0 = t; ds0 = ds; du0 = du;
    }
}

static void register_seps(void);
static void a2d_cb(esp_a2d_cb_event_t ev, esp_a2d_cb_param_t *p)
{
    switch (ev) {
    case ESP_A2D_PROF_STATE_EVT:
        /* endpoints only once the profile is up: right after esp_a2d_sink_init() they can fail with
         * ESP_ERR_INVALID_STATE (seen 2026-11-10, a race) */
        if (p->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS) register_seps();
        break;
    case ESP_A2D_CONNECTION_STATE_EVT:
        printf("PEER conn state=%d\n", p->conn_stat.state);
        if (p->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) s_have_ts = false;   /* a codec step closes and reopens
                                                                                     the media channel: keep timing the pause */
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        printf("PEER audio state=%d\n", p->audio_stat.state);
        s_have_ts = false;
        s_last_step = 0;
        s_dec_reset = true;
        break;
    case ESP_A2D_AUDIO_CFG_EVT: {
        s_mcc = p->audio_cfg.mcc;
        s_codec = codec_of(&s_mcc);
        const uint8_t *c = (const uint8_t *)&s_mcc.cie;
        if (s_codec == PC_BTC_LDAC) {
            uint8_t sf = s_mcc.cie.vendor_info.value[0];
            s_rate = sf & ESP_A2D_LDAC_SF_48K ? 48000 : sf & ESP_A2D_LDAC_SF_44K ? 44100 : sf & ESP_A2D_LDAC_SF_96K ? 96000 : 88200;
        } else if (s_codec == PC_BTC_APTX || s_codec == PC_BTC_APTX_HD) {
            s_rate = s_mcc.cie.vendor_info.value[0] & ESP_A2D_APTX_SF_48K ? 48000 : 44100;
        } else if (s_mcc.type == ESP_A2D_MCT_SBC) {
            uint8_t sf = s_mcc.cie.sbc_info.samp_freq;
            s_rate = sf & ESP_A2D_SBC_CIE_SF_48K ? 48000 : sf & ESP_A2D_SBC_CIE_SF_44K ? 44100 : 32000;
        }
        printf("PEER codec=%s rate=%" PRIu32 " cie=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", pc_btc_name(s_codec), s_rate,
               c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10]);
        break;
    }
    case ESP_A2D_SEP_REG_STATE_EVT:
        printf("PEER sep=%d reg=%d\n", p->a2d_sep_reg_stat.seid, p->a2d_sep_reg_stat.reg_state);
        break;
    default:
        break;
    }
}

static void register_seps(void)
{
    int seid = 0;
    if (PEER_CODECS & 1) {
        esp_a2d_mcc_t ldac = { .type = ESP_A2D_MCT_NON_A2DP };
        ldac.cie.vendor_info.vendor_id = ESP_A2D_LDAC_VENDOR_ID;
        ldac.cie.vendor_info.codec_id = ESP_A2D_LDAC_CODEC_ID;
        ldac.cie.vendor_info.value[0] = LDAC_SF;
        ldac.cie.vendor_info.value[1] = ESP_A2D_LDAC_CH_STEREO | ESP_A2D_LDAC_CH_DUAL;
        printf("PEER reg LDAC seid=%d rc=%d\n", seid, esp_a2d_sink_register_stream_endpoint(seid, &ldac));
        seid++;
    }
    for (int hd = 1; hd >= 0; hd--) {
        if (!(PEER_CODECS & (hd ? 2 : 4))) continue;
        esp_a2d_mcc_t a = { .type = ESP_A2D_MCT_NON_A2DP };
        a.cie.vendor_info.vendor_id = hd ? ESP_A2D_APTX_HD_VENDOR_ID : ESP_A2D_APTX_VENDOR_ID;
        a.cie.vendor_info.codec_id = hd ? ESP_A2D_APTX_HD_CODEC_ID : ESP_A2D_APTX_CODEC_ID;
        a.cie.vendor_info.value[0] = APTX_SF | ESP_A2D_APTX_CH_STEREO;
        printf("PEER reg %s seid=%d rc=%d\n", hd ? "aptX HD" : "aptX", seid, esp_a2d_sink_register_stream_endpoint(seid, &a));
        seid++;
    }
    esp_a2d_mcc_t sbc = { .type = ESP_A2D_MCT_SBC };
    sbc.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_44K | ESP_A2D_SBC_CIE_SF_48K;
    sbc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_MONO | ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL | ESP_A2D_SBC_CIE_CH_MODE_STEREO | ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
    sbc.cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_4 | ESP_A2D_SBC_CIE_BLOCK_LEN_8 | ESP_A2D_SBC_CIE_BLOCK_LEN_12 | ESP_A2D_SBC_CIE_BLOCK_LEN_16;
    sbc.cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_4 | ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
    sbc.cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR | ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
    sbc.cie.sbc_info.min_bitpool = 2;
    sbc.cie.sbc_info.max_bitpool = 53;
    printf("PEER reg SBC seid=%d rc=%d\n", seid, esp_a2d_sink_register_stream_endpoint(seid, &sbc));
}

static void stack_up(uint16_t ev, void *arg)
{
    (void)ev; (void)arg;
    esp_bt_gap_set_device_name(NAME);
    esp_bt_gap_register_callback(bredr_app_gap_evt_def_hdl);
    esp_a2d_register_callback(a2d_cb);
    ESP_ERROR_CHECK(esp_a2d_sink_init());              /* the endpoints follow in ESP_A2D_PROF_STATE_EVT */
    esp_a2d_sink_register_audio_data_callback(data_cb);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    printf("PEER up name=\"%s\" codecs:%s%s%s SBC%s (LDAC checked, aptX decoded)\n", NAME, PEER_CODECS & 1 ? " LDAC" : "",
           PEER_CODECS & 2 ? " aptX-HD" : "", PEER_CODECS & 4 ? " aptX" : "", PEER_44K_ONLY ? ", vendor codecs 44.1 kHz only" : "");
}

void app_main(void)
{
    ESP_ERROR_CHECK(bredr_app_common_init());
    s_q = xQueueCreate(32, sizeof(esp_a2d_audio_buff_t *));
    xTaskCreatePinnedToCore(decode_task, "aptx_dec", 6144, NULL, 4, NULL, 1);
    bt_app_task_start_up();
    bt_app_work_dispatch(stack_up, 0, NULL, 0, NULL, NULL);
    xTaskCreate(stats_task, "stats", 4096, NULL, 3, NULL);
}
