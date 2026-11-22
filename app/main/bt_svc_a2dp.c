// SPDX-License-Identifier: LGPL-2.1-or-later
/* Bluetooth, A2DP: the speaker path (a phone streams AAC or SBC, decoded on a task of its own into a ring that plays as a mixer source) and the
 * headphones path (our audio encoded for the chosen headphones: SBC / AAC, LDAC / aptX in the patched build, the codec steps, the resampler), with the A2DP
 * callback that serves both. One file on purpose: the codec helpers (mcc_*), the ring owner and the callbacks are static functions inlined into each other,
 * and a file boundary through them changes the compiled code (the bluetooth design notes, "bt_svc.c split"). Split out of bt_svc.c; shared state in bt_priv.h. */
#include "bt_priv.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_CLASSIC_ENABLED

/* hires T8e: the dry blocks of the sink paths (A2DP SBC/AAC, the vendor encoders, Auracast) that count as underruns for the cover pass's hold
 * (bt_svc_sink_underflows). Fix round 1 (review I1, M4): only a dry block in a steady stream (pc_tbg_sink_block: a ring that fills at a start or
 * after a pause is not one) while the audio really plays (not paused). PSRAM .bss (no internal RAM); one writer at a time (one sink path runs). */
EXT_RAM_BSS_ATTR static volatile uint32_t s_sink_under;
EXT_RAM_BSS_ATTR static pc_tbg_sink_t s_sink_q;           /* the consumer's (sink path task's) */
EXT_RAM_BSS_ATTR volatile uint32_t s_sink_sess;     /* fix 2 (re-review N2): bumped when a sink starts (LVGL); the consumer starts unfed */
uint32_t bt_svc_sink_underflows(void) { return s_sink_under; }
void sink_block(bool dry)                       /* every block a sink path pulls, dry or not */
{
    if (!pc_tbg_sink_block(&s_sink_q, s_sink_sess, dry, esp_timer_get_time() / 1000)) return;
    aud_status_t st;
    audio_svc_status(&st);
    if ((st.state == AUD_PLAYING || st.state == AUD_NEAR || st.state == AUD_NEXT) && !audio_svc_paused()) s_sink_under++;
}

/* ---- ring of stereo int16 frames: the decode task writes, the audio task reads ---- */
int16_t *s_ring;
volatile uint32_t s_w, s_r;       /* frame counters */
static uint32_t ring_fill(void) { return s_w - s_r; }

/* ---- decoder (decode task) ---- */
QueueHandle_t s_raw_q;
static esp_audio_dec_handle_t s_dec;
static volatile bool s_dec_ok;
uint32_t s_rate = 44100;
static int s_ch = 2;
static esp_a2d_mcc_t s_mcc;
static volatile bool s_mcc_new;

/* the negotiated codec (pc_btcodec order); vendor codecs only with the patched stack (JC_LDAC builds) */
static pc_btc_t mcc_codec(const esp_a2d_mcc_t *m)
{
#if JC_LDAC
    if (m->type == ESP_A2D_MCT_NON_A2DP) {
        uint32_t v = m->cie.vendor_info.vendor_id;
        uint16_t c = m->cie.vendor_info.codec_id;
        if (v == ESP_A2D_LDAC_VENDOR_ID && c == ESP_A2D_LDAC_CODEC_ID) return PC_BTC_LDAC;
        if (v == ESP_A2D_APTX_HD_VENDOR_ID && c == ESP_A2D_APTX_HD_CODEC_ID) return PC_BTC_APTX_HD;
        if (v == ESP_A2D_APTX_VENDOR_ID && c == ESP_A2D_APTX_CODEC_ID) return PC_BTC_APTX;
        return PC_BTC_NONE;
    }
#endif
    return m->type == ESP_A2D_MCT_SBC ? PC_BTC_SBC : m->type == ESP_A2D_MCT_M24 ? PC_BTC_AAC : PC_BTC_NONE;
}
#if JC_LDAC
static bool mcc_is_ldac(const esp_a2d_mcc_t *m) { return mcc_codec(m) == PC_BTC_LDAC; }
#endif
static const char *mcc_name(const esp_a2d_mcc_t *m) { return pc_btc_name(mcc_codec(m)); }

static uint32_t mcc_rate(const esp_a2d_mcc_t *m, int *ch)
{
#if JC_LDAC
    if (mcc_is_ldac(m)) {
        uint8_t f = m->cie.vendor_info.value[0];
        *ch = m->cie.vendor_info.value[1] & ESP_A2D_LDAC_CH_MONO ? 1 : 2;
        return f & ESP_A2D_LDAC_SF_48K ? 48000 : f & ESP_A2D_LDAC_SF_44K ? 44100 : f & ESP_A2D_LDAC_SF_96K ? 96000 : 88200;
    }
    if (mcc_codec(m) == PC_BTC_APTX || mcc_codec(m) == PC_BTC_APTX_HD) {
        *ch = 2;
        return m->cie.vendor_info.value[0] & ESP_A2D_APTX_SF_48K ? 48000 : 44100;
    }
#endif
    if (m->type == ESP_A2D_MCT_SBC) {
        *ch = (m->cie.sbc_info.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO) ? 1 : 2;
        uint8_t f = m->cie.sbc_info.samp_freq;
        return f & ESP_A2D_SBC_CIE_SF_48K ? 48000 : f & ESP_A2D_SBC_CIE_SF_44K ? 44100 : f & ESP_A2D_SBC_CIE_SF_32K ? 32000 : 16000;
    }
    *ch = (m->cie.m24_info.ch & ESP_A2D_M24_CIE_CH_1) && !(m->cie.m24_info.ch & ESP_A2D_M24_CIE_CH_2) ? 1 : 2;
    if (m->cie.m24_info.samp_freq2 & ESP_A2D_M24_CIE_SF2_48K) return 48000;
    return m->cie.m24_info.samp_freq1 & ESP_A2D_M24_CIE_SF1_44K ? 44100 : 32000;
}

static void dec_open(void)
{
    if (s_dec) { esp_audio_dec_close(s_dec); s_dec = NULL; }
    s_dec_ok = false;
    s_rate = mcc_rate(&s_mcc, &s_ch);
    esp_audio_err_t r;
    if (s_mcc.type == ESP_A2D_MCT_SBC) {
        esp_sbc_dec_cfg_t c = ESP_SBC_DEC_CONFIG_DEFAULT();
        c.sbc_mode = ESP_SBC_MODE_STD;
        c.ch_num = (uint8_t)s_ch;
        c.enable_plc = true;
        esp_audio_dec_cfg_t d = { .type = ESP_AUDIO_TYPE_SBC, .cfg = &c, .cfg_sz = sizeof c };
        esp_sbc_dec_register();
        r = esp_audio_dec_open(&d, &s_dec);
    } else {
        esp_aac_dec_cfg_t c = ESP_AAC_DEC_CONFIG_DEFAULT();
        c.sample_rate = s_rate;
        c.channel = (uint8_t)s_ch;
        c.bits_per_sample = ESP_AUDIO_BIT16;
        c.no_adts_header = true;
        c.aac_plus_enable = (s_mcc.cie.m24_info.obj_type & (ESP_A2D_M24_CIE_OBJ_TYPE_4_HE_AAC | ESP_A2D_M24_CIE_OBJ_TYPE_4_HE_AAC_V2)) != 0;
        esp_audio_dec_cfg_t d = { .type = ESP_AUDIO_TYPE_AAC, .cfg = &c, .cfg_sz = sizeof c };
        esp_aac_dec_register();
        r = esp_audio_dec_open(&d, &s_dec);
    }
    s_dec_ok = r == ESP_AUDIO_ERR_OK;
    const uint8_t *ci = (const uint8_t *)&s_mcc.cie;
    printf("APP bt codec=%s rate=%lu ch=%d open=%d cie=%02x %02x %02x %02x\n", s_mcc.type == ESP_A2D_MCT_SBC ? "SBC" : "AAC",
           (unsigned long)s_rate, s_ch, r, ci[0], ci[1], ci[2], ci[3]);
}

void decode_task(void *arg)
{
    (void)arg;
    uint32_t cap = 8192;
    uint8_t *out = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    for (;;) {
        esp_a2d_audio_buff_t *b;
        if (xQueueReceive(s_raw_q, &b, portMAX_DELAY) != pdTRUE) continue;
        if (s_mcc_new) { s_mcc_new = false; dec_open(); }
        if (!s_dec_ok || !out) { esp_a2d_audio_buff_free(b); continue; }
        esp_audio_dec_in_raw_t in = { .buffer = b->data, .len = b->data_len };
        while (in.len > 0) {
            esp_audio_dec_out_frame_t o = { .buffer = out, .len = cap };
            esp_audio_err_t r = esp_audio_dec_process(s_dec, &in, &o);
            if (r == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                uint8_t *n = heap_caps_realloc(out, cap * 2, MALLOC_CAP_SPIRAM);
                if (!n) break;
                out = n;
                cap *= 2;
                continue;
            }
            if (r != ESP_AUDIO_ERR_OK) break;
            const int16_t *pcm = (const int16_t *)out;
            int frames = (int)(o.decoded_size / (2u * (unsigned)s_ch));
            for (int i = 0; i < frames; i++) {
                if (ring_fill() >= RING_FRAMES) break;   /* full: drop (the reader re-centres the fill) */
                uint32_t k = s_w % RING_FRAMES;
                s_ring[2 * k] = pcm[i * s_ch];
                s_ring[2 * k + 1] = pcm[i * s_ch + (s_ch > 1)];
                __atomic_store_n(&s_w, s_w + 1, __ATOMIC_RELEASE);
            }
            in.buffer += in.consumed;
            in.len -= in.consumed;
        }
        esp_a2d_audio_buff_free(b);
    }
}

/* ---- the mixer source (audio task) ---- */
bool s_primed;
static volatile uint32_t s_dry;                       /* times the ring ran dry while playing (a gap the listener hears) */
int mix_render(float *out, int frames, void *u)
{
    (void)u;
    uint32_t target = s_rate * TARGET_MS / 1000;
    uint32_t fill = __atomic_load_n(&s_w, __ATOMIC_ACQUIRE) - s_r;
    if (!s_primed) {                                       /* wait for a cushion before starting */
        if (fill < target) { memset(out, 0, (size_t)frames * 2 * sizeof(float)); return frames; }
        s_primed = true;
    }
    if (fill > 2 * target) s_r += fill - target;          /* phone clock faster than ours: drop the excess */
    for (int i = 0; i < frames; i++) {
        if (__atomic_load_n(&s_w, __ATOMIC_ACQUIRE) == s_r) {   /* ran dry: silence, then wait for a cushion again */
            memset(out + 2 * i, 0, (size_t)(frames - i) * 2 * sizeof(float));
            s_primed = false;
            s_dry++;
            break;
        }
        uint32_t k = s_r % RING_FRAMES;
        out[2 * i] = s_ring[2 * k] / 32768.0f;
        out[2 * i + 1] = s_ring[2 * k + 1] / 32768.0f;
        s_r++;
    }
    return frames;
}
void mix_release(void *u) { (void)u; }

/* FIX4 I3 / M6: the preferences the headphones endpoints are registered with (the "applied" ones), a snapshot taken on the LVGL task (cfg is LVGL-side) and read by the Bluetooth tasks and the
 * output status. The stored cfg may be ahead of it until the sink reconnects; the status says "next connection" for that. */
static EXT_RAM_BSS_ATTR struct { int codec, rate, bits, chan, kbps; } s_pf = { PC_BT_PREF_LDAC, 96000, 24, 0, 990 };
void pf_take(void)
{
    s_pf.codec = cfg_int("bt_codec", PC_BT_PREF_LDAC);
    s_pf.rate = cfg_int("bt_rate", 96000);
    s_pf.bits = cfg_int("bt_bits", 24);
    s_pf.chan = cfg_int("bt_chan", 0);
    s_pf.kbps = cfg_int("ldac_kbps", 990);
}
bool pf_reconnect_needed(void)   /* what the endpoints are made of: the codec ceiling, the 44.1k-only flag and the channel-mode offer (bt_bits and 48k vs 96k change nothing on the link) */
{
    return cfg_int("bt_codec", PC_BT_PREF_LDAC) != s_pf.codec || (cfg_int("bt_rate", 96000) == 44100) != (s_pf.rate == 44100) || cfg_int("bt_chan", 0) != s_pf.chan;
}
static void hp_a2d_event(esp_a2d_cb_event_t ev, esp_a2d_cb_param_t *p);
static void register_seps(void);
static void hp_register_seps(void);
void a2d_cb(esp_a2d_cb_event_t ev, esp_a2d_cb_param_t *p)   /* BTC task */
{
    if (ev == ESP_A2D_PROF_STATE_EVT && p->a2d_prof_stat.init_state == ESP_A2D_DEINIT_SUCCESS) {
        if (s_a2d_down) xSemaphoreGive(s_a2d_down);
        return;
    }
    if (ev == ESP_A2D_PROF_STATE_EVT) {
        if (p->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS) {
            if (s_mode == BT_MODE_HEADPHONES || s_hp_active) hp_register_seps();
            else register_seps();
        } else printf("APP bt a2dp prof state=%d\n", p->a2d_prof_stat.init_state);
        if (s_a2d_up) xSemaphoreGive(s_a2d_up);
        return;
    }
    if (s_mode == BT_MODE_HEADPHONES || s_hp_active) { hp_a2d_event(ev, p); return; }
    switch (ev) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (p->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            memcpy(s_spk_bda, p->conn_stat.remote_bda, 6);
            s_spk_connected = true;
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            snprintf(s_status, sizeof s_status, "%s", T(S_CONNECTED) /* 已連線 */);
            esp_bt_gap_read_remote_name(p->conn_stat.remote_bda);
        } else if (p->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            s_spk_connected = false;
            if (!s_spk_leaving) esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);   /* not while speaker_off() takes the link down */
            snprintf(s_status, sizeof s_status, "%s", T(S_WAITING_FOR_A_PHONE_JC_PLAYER) /* 等待手機連線（JukeDial） */);
            s_peer[0] = 0;
            ui_async(ui_stream, (void *)0);
        }
        printf("APP bt a2dp state=%d\n", p->conn_stat.state);
        ui_async(ui_status, NULL);
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        printf("APP bt audio state=%d\n", p->audio_stat.state);
        ui_async(ui_stream, (void *)(intptr_t)(p->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED));
        break;
    case ESP_A2D_AUDIO_CFG_EVT:
        s_mcc = p->audio_cfg.mcc;
        s_mcc_new = true;
        { int ch; s_rate = mcc_rate(&s_mcc, &ch); }
        break;
    case ESP_A2D_SEP_REG_STATE_EVT:
        printf("APP bt sep=%d reg=%d\n", p->a2d_sep_reg_stat.seid, p->a2d_sep_reg_stat.reg_state);
        break;
    default:
        break;
    }
}

void data_cb(esp_a2d_conn_hdl_t h, esp_a2d_audio_buff_t *b)
{
    (void)h;
    if (xQueueSend(s_raw_q, &b, 0) != pdTRUE) esp_a2d_audio_buff_free(b);
}

static void register_seps(void)
{
    /* SBC is registered first: simple sources read only the first endpoint; phones still pick AAC by their own preference */
    esp_a2d_mcc_t sbc = { .type = ESP_A2D_MCT_SBC };
    sbc.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_32K | ESP_A2D_SBC_CIE_SF_44K | ESP_A2D_SBC_CIE_SF_48K;
    sbc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_MONO | ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL | ESP_A2D_SBC_CIE_CH_MODE_STEREO | ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
    sbc.cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_4 | ESP_A2D_SBC_CIE_BLOCK_LEN_8 | ESP_A2D_SBC_CIE_BLOCK_LEN_12 | ESP_A2D_SBC_CIE_BLOCK_LEN_16;
    sbc.cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_4 | ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
    sbc.cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR | ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
    sbc.cie.sbc_info.min_bitpool = 2;
    sbc.cie.sbc_info.max_bitpool = 250;
    esp_err_t e = esp_a2d_sink_register_stream_endpoint(0, &sbc);
    if (e != ESP_OK) printf("APP bt sep=0 SBC register rc=%s\n", esp_err_to_name(e));
    esp_a2d_mcc_t aac = { .type = ESP_A2D_MCT_M24 };
    aac.cie.m24_info.drc = ESP_A2D_M24_CIE_DRC_NS;
    aac.cie.m24_info.obj_type = ESP_A2D_M24_CIE_OBJ_TYPE_2_AAC_LC | ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC;
    aac.cie.m24_info.samp_freq1 = ESP_A2D_M24_CIE_SF1_32K | ESP_A2D_M24_CIE_SF1_44K;
    aac.cie.m24_info.samp_freq2 = ESP_A2D_M24_CIE_SF2_48K;
    aac.cie.m24_info.ch = ESP_A2D_M24_CIE_CH_1 | ESP_A2D_M24_CIE_CH_2;
    aac.cie.m24_info.vbr = ESP_A2D_M24_CIE_VBR_SUPPORT;
    aac.cie.m24_info.br1 = 0x7F & ESP_A2D_M24_CIE_BR1_MSK;
    aac.cie.m24_info.br2 = 0xFF & ESP_A2D_M24_CIE_BR2_MSK;
    aac.cie.m24_info.br3 = 0xFF & ESP_A2D_M24_CIE_BR3_MSK;
    e = esp_a2d_sink_register_stream_endpoint(1, &aac);
    if (e != ESP_OK) printf("APP bt sep=1 AAC register rc=%s\n", esp_err_to_name(e));
}

/* ===================== headphones (A2DP source): our audio -> the chosen headphones ===================== */
bt_dev_t s_devs[HP_MAX];
int s_ndevs;
portMUX_TYPE s_dev_mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t s_hp_bda[6];
static esp_a2d_mcc_t s_hp_mcc;
volatile bool s_hp_streaming;
/* One encoder reads s_hp_ring at a time. s_hp_gen changes with every new configuration, start, suspend and
 * disconnect; an encoder loop ends when it changes or when the codec is no longer its own (pc_btc_encoder), and
 * gives the ring back (s_ring_owner) before the other task may take it. */
volatile uint32_t s_hp_gen;
static int s_ring_owner = PC_ENC_NONE;
static EXT_RAM_BSS_ATTR volatile bool s_hp_neg;                       /* FIX4 I3: the stack reported the codec configuration of this link (ESP_A2D_AUDIO_CFG_EVT): the negotiated state is known */
static EXT_RAM_BSS_ATTR volatile uint8_t s_neg_chan;                  /* pc_chan_t of it */
volatile unsigned s_hp_skip;                  /* codecs that could not start this boot (1 << pc_btc_t): not offered */
char s_hp_note[64];                          /* why, shown after the codec in the status line */
uint16_t s_hp_mtu = 600;
static esp_a2d_conn_hdl_t s_hp_hdl;
uint32_t s_link_rate = 44100;
int16_t *s_hp_ring;
volatile uint32_t s_hw, s_hr;
/* the resampler (hires T10b, H29): two table blocks, PSRAM, allocated once at the first sink mode (hp_rsq_alloc) and never freed (hp_sink may be
 * inside one). The table for a (source rate, link rate) pair takes about 26 ms to build, so the audio task never builds it: the builder task
 * (rsq_build_task) fills the spare and hp_sink swaps it in at the pair change (pc_rsq_swap_t: the ownership rules are in pc_rsq.h); until the
 * spare holds the block's pair the block is dropped. waited / dropped: the audio task's last waited-for pair (its wait line once) and the blocks
 * dropped since; on: a sink is on (the builder polls the plan only then). One object above 8 bytes, so libmain's .bss puts it in PSRAM. */
bt_hp_rs_t s_hp_rs;

/* a codec step under way (vendor builds: suspend, reconfigure, start; pc_cstep): -1 none, else the rung */
static volatile int s_stp_target = -1;
static bool hp_owns(pc_btc_enc_t who, uint32_t gen)
{
    return s_hp_streaming && s_hp_gen == gen && s_stp_target < 0 && pc_btc_encoder(mcc_codec(&s_hp_mcc)) == who;
}
static bool ring_take(pc_btc_enc_t who, uint32_t gen)   /* waits for the other encoder to give the ring back */
{
    for (;;) {
        int f = PC_ENC_NONE;
        if (__atomic_compare_exchange_n(&s_ring_owner, &f, (int)who, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return true;
        if (!hp_owns(who, gen)) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
static void ring_give(void) { __atomic_store_n(&s_ring_owner, PC_ENC_NONE, __ATOMIC_RELEASE); }

#if CONFIG_BT_BAP_BROADCAST_SINK
static void listen_clear(void);
#endif
void bt_svc_scan(void)
{
#if CONFIG_BT_BAP_BROADCAST_SINK
    if (s_mode == BT_MODE_LISTEN) { listen_clear(); return; }   /* the scan runs on: the list just starts over */
#endif
    if (s_mode != BT_MODE_HEADPHONES || s_hp_connected) return;
    portENTER_CRITICAL(&s_dev_mux);
    s_ndevs = 0;
    portEXIT_CRITICAL(&s_dev_mux);
    esp_bt_gap_cancel_discovery();
    esp_err_t e = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);   /* ~13 s */
    printf("APP bt scan start rc=%s\n", esp_err_to_name(e));
    snprintf(s_status, sizeof s_status, "%s", T(S_SCANNING_FOR_HEADPHONES) /* 掃描耳機中… */);
}

int bt_svc_devices(bt_dev_t *out, int max)
{
    portENTER_CRITICAL(&s_dev_mux);
    int n = s_ndevs < max ? s_ndevs : max;
    memcpy(out, s_devs, (size_t)n * sizeof *out);
    portEXIT_CRITICAL(&s_dev_mux);
    return n;
}

/* the builder (hires T10b): its own task at priority 2 on core 0, below the UI and Bluetooth, PSRAM stack. Not the LVGL task: 26 ms there is a
 * visible UI stall, and the audio task can wake this one directly when a block waits (a task notification: no allocation, never blocks).
 * Builds the spare for the audio task's request, else for the plan the audio side publishes (audio_svc_info: a sink plan keeps every pipeline
 * rate up to 48k, so that rate with the link's is the next block's pair, built before the block comes when the plan is ahead of it), else
 * (fix 1) for the queued gapless next track (next_rate, planned for the sink by pc_sink_pipe_rate): seconds before the join, so the swap there
 * finds it ready and no block is dropped. The stack high-water mark is printed once, after the first build (APP rsq stack free=<bytes>). */
static void rsq_build_task(void *arg)
{
    (void)arg;
    pc_rsq_pair_t failed = { 0, 0 };
    int64_t failed_until = 0;
    bool stack_said = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, s_hp_rs.on ? pdMS_TO_TICKS(50) : portMAX_DELAY);
        aud_info_t ai;
        audio_svc_info(&ai);
        pc_rsq_pair_t now, next, p;
        pc_rsq_hints(s_hp_rs.on, ai.src_rate, ai.pipe_rate, ai.next_rate, s_link_rate, &now, &next);   /* the plan's pair, the queued next track's */
        pc_rsq_t *q = pc_rsq_swap_spare(&s_hp_rs.sw, now, next, &p);
        if (!q) continue;
        int64_t t0 = esp_timer_get_time();
        if (p.in == failed.in && p.out == failed.out && t0 < failed_until) continue;   /* failed a moment ago: no new attempt on every wake */
        int err = pc_rsq_init(q, p.in, p.out);
        if (err) {                                       /* no audio rather than a wrong rate: the blocks keep waiting; one line, the next try in 1 s */
            printf("APP rsq fail in=%lu out=%lu err=%d\n", (unsigned long)p.in, (unsigned long)p.out, err);
            failed = p;
            failed_until = t0 + 1000000;
            continue;
        }
        printf("APP rsq init in=%lu out=%lu us=%lu task=%s\n", (unsigned long)p.in, (unsigned long)p.out, (unsigned long)(esp_timer_get_time() - t0),
               pcTaskGetName(NULL));
        pc_rsq_swap_publish(&s_hp_rs.sw, p);
        if (!stack_said) {                               /* fix 1: the 4 KB PSRAM stack's margin, once (ESP-IDF counts the high-water mark in bytes) */
            stack_said = true;
            printf("APP rsq stack free=%u\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
    }
}

/* the mode switch's task (headphones_on / broadcast_on on bt_mode), before this mode sets a sink: the resampler's two blocks (once, never freed) and
 * the builder. A retry after a failed allocation may overlap the previous mode's last hp_sink call (its sink-off is posted to the LVGL task):
 * setup's memset then only zeroes a request or a fresh flag that call wrote (both stay 0 while there are no blocks: every block waits), benign. */
void hp_rsq_alloc(void)
{
    if (!s_hp_rs.sw.blk[0]) {
        pc_rsq_t *a = heap_caps_malloc(pc_rsq_size(), MALLOC_CAP_SPIRAM), *b = heap_caps_malloc(pc_rsq_size(), MALLOC_CAP_SPIRAM);
        if (!a || !b) {                                  /* every resampled block waits (pass-through still plays); tried again at the next sink mode */
            printf("APP rsq alloc failed bytes=%u\n", (unsigned)(2 * pc_rsq_size()));
            heap_caps_free(a);
            heap_caps_free(b);
            return;
        }
        pc_rsq_swap_setup(&s_hp_rs.sw, a, b);
    }
    if (!s_hp_rs.task && xTaskCreatePinnedToCoreWithCaps(rsq_build_task, "rsq", 4096, NULL, 2, &s_hp_rs.task, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        s_hp_rs.task = NULL;
        printf("APP rsq builder task failed\n");       /* the blocks wait (one wait line each pair); tried again at the next sink mode */
    }
}

/* audio task: the player's 10 ms float blocks -> resampled to the link rate -> narrowed to 16 bit with TPDF dither -> the ring
 * (waits while it is full). The table comes from the builder (hires T10b): a block whose pair has no table yet is dropped and waits 10 ms. */
void hp_sink(const float *lr, int frames, uint32_t rate)
{
    static float out[2 * 600];
    static int16_t q[2 * 600];                         /* libmain .bss: PSRAM */
    static pc_dither_t dither;
    static bool dither_ready;
    if (!dither_ready) { pc_dither_init(&dither, 0); dither_ready = true; }
    if (frames > PC_SINK_MAX_BLOCK) frames = PC_SINK_MAX_BLOCK;     /* 10 ms at 48k: the audio task plans a sink at <= 48k and splits a longer block; never more here */
    uint32_t link = s_link_rate;
    pc_rsq_t *rq;                                       /* the table for (rate, link); pass-through needs none; a pass-through block also makes the next resampled one start fresh */
    pc_rsq_pick_t pk = pc_rsq_swap_take(&s_hp_rs.sw, rate, link, &rq);
    if (pk == PC_RSQ_WAIT) {                            /* not built yet (the builder has it): dropped, never at another pair's table, never built here */
        if (s_hp_rs.waited.in != rate || s_hp_rs.waited.out != link) {
            s_hp_rs.waited.in = rate;
            s_hp_rs.waited.out = link;
            printf("APP rsq wait in=%lu out=%lu\n", (unsigned long)rate, (unsigned long)link);
        }
        s_hp_rs.dropped++;
        s_hp_rs.dropped_total++;                        /* 10c: every block a missing table cost (the status lines print the total: rsqdrop=) */
        if (s_hp_rs.task) xTaskNotifyGive(s_hp_rs.task);
        vTaskDelay(pdMS_TO_TICKS(10));                  /* the DROP_WAIT rule: a dropped block keeps real time (a 10 ms block), the decoder does not race ahead */
        return;
    }
    if (pk == PC_RSQ_SWAP) printf("APP rsq swap in=%lu out=%lu dropped=%lu\n", (unsigned long)rate, (unsigned long)link, (unsigned long)s_hp_rs.dropped);
    s_hp_rs.dropped = 0;                                /* fix 2 (review m1): a wait stretch ends at any block that does not wait (swap, pass-through, the live */
    s_hp_rs.waited.in = s_hp_rs.waited.out = 0;         /* table again): a later swap counts only its own drops and a later wait for the same pair is logged */
    int done = 0;
    while (done < frames) {
        int used = 0;
        const float *src = out;
        int n;
        if (rate == link) {                 /* no resampling: the input in chunks of at most the buffers' size */
            src = lr + 2 * done;
            n = used = frames - done > 600 ? 600 : frames - done;
        } else {
            n = pc_rsq_process(rq, lr + 2 * done, frames - done, out, 600, &used);
        }
        pc_f32_to_s16_tpdf(&dither, src, q, 2 * n);
        for (int i = 0; i < n; i++) {
            for (int w = 0; w < 50 && s_hw - s_hr >= HP_FILL; w++) vTaskDelay(1);   /* full: the link sets the pace */
            if (s_hw - s_hr >= HP_RING) return;                                   /* link stalled: drop */
            uint32_t k = s_hw % HP_RING;
            s_hp_ring[2 * k] = q[2 * i];
            s_hp_ring[2 * k + 1] = q[2 * i + 1];
            __atomic_store_n(&s_hw, s_hw + 1, __ATOMIC_RELEASE);
        }
        done += used;
        if (!used && !n) break;
    }
}

#if JC_LDAC
/* ===== vendor codecs in headphones mode (patched Bluedroid, `make build-ldac`): LDAC (libldac, fixed point) with
 * an adaptive bit rate, aptX and aptX HD (libfreeaptx). They encode the player's PCM on their own task, bt_venc,
 * pinned to the core in cfg venc_core. LDAC packets: 128-sample frames, as many as libldac puts in one 2-DH5
 * packet (990/660/492/330 kbps: 2/3/4/6 frames), the frame count in the stack's one-octet header, the RTP
 * timestamp in samples (as AOSP's a2dp_vendor_ldac_encoder). aptX: AOSP's packet sizes, no RTP header for
 * classic aptX (the stack leaves it out), aptX HD with one. ===== */
#include "ldacBT.h"
#include "freeaptx.h"
#include "fx_state.h"
#include "wifi_svc.h"

static volatile int s_ldac_kbps = 990;               /* the user's top rate (cfg ldac_kbps); the ABR stays at or below */
static EXT_RAM_BSS_ATTR volatile int s_venc_nom;                      /* LDAC: the nominal level now (990 / 660 / 492 / 330), for the output status */
static volatile int s_venc_kbps;                     /* LDAC: the rate in use (status line) */
static void ui_ldac_save(void *arg) { cfg_set_int("ldac_kbps", (int)(intptr_t)arg); }
int bt_svc_ldac_kbps(int kbps)
{
    if (kbps != 330 && kbps != 492 && kbps != 660 && kbps != 990) return -2;
    s_ldac_kbps = kbps;
    s_pf.kbps = kbps;
    ui_async(ui_ldac_save, (void *)(intptr_t)kbps);
    return 0;
}

typedef struct {                                      /* one stream's counters, printed every 5 s */
    uint32_t sent, fail, bytes, under, drops0, qmax, lost;
    int64_t enc_us, t0;
} venc_stats_t;

/* the ring -> n stereo frames; silence (one underflow) where it runs dry */
static void hp_pull(int16_t *dst, int n, venc_stats_t *st)
{
    bool dry = false;
    for (int i = 0; i < n; i++) {
        if (s_hw == s_hr) { dst[2 * i] = dst[2 * i + 1] = 0; dry = true; continue; }
        uint32_t k = s_hr % HP_RING;
        dst[2 * i] = s_hp_ring[2 * k];
        dst[2 * i + 1] = s_hp_ring[2 * k + 1];
        __atomic_store_n(&s_hr, s_hr + 1, __ATOMIC_RELEASE);
    }
    if (dry) st->under++;
    sink_block(dry);
}

static bool hp_send(const uint8_t *data, int len, int frames, uint32_t ts, venc_stats_t *st)
{
    esp_a2d_audio_buff_t *b = esp_a2d_audio_buff_alloc((uint16_t)len);
    if (!b) { st->fail++; return false; }
    memcpy(b->data, data, (size_t)len);
    b->data_len = (uint16_t)len;
    b->number_frame = (uint16_t)frames;
    b->timestamp = ts;
    if (esp_a2d_source_audio_data_send(s_hp_hdl, b) != ESP_OK) { esp_a2d_audio_buff_free(b); st->fail++; return false; }
    st->sent++;
    st->bytes += (uint32_t)len;
    return true;
}

/* real-time pacing from the sample count (no drift); a stall of more than 100 ms is not made up in a burst */
typedef struct { int64_t t0; uint64_t samples; uint32_t rate; } pace_t;
static void hp_pace(pace_t *p, int samples)
{
    p->samples += (uint64_t)samples;
    int64_t due = p->t0 + (int64_t)(p->samples * 1000000ull / p->rate), now = esp_timer_get_time();
    if (now - due > 100000) { p->t0 = now; p->samples = 0; return; }
    if (due - now > 3000) vTaskDelay(pdMS_TO_TICKS((due - now) / 1000));
}

/* queue depth and losses for the ABR and the stats; returns packets lost since the last call */
static int hp_flow(venc_stats_t *st, int *queued, uint32_t fail_before)
{
    uint16_t q = 0;
    uint32_t drops = st->drops0;
    esp_a2d_source_get_tx_stats(&q, &drops);
    int lost = (int)(drops - st->drops0) + (int)(st->fail - fail_before);
    st->drops0 = drops;
    st->lost += (uint32_t)lost;
    if (q > st->qmax) st->qmax = q;
    *queued = q;
    return lost;
}

static void hp_venc_report(const char *name, venc_stats_t *st, int kbps_target)
{
    int64_t now = esp_timer_get_time(), dt = now - st->t0;
    if (dt < 5000000) return;
    printf("APP bt hp venc codec=%s target=%d kbps=%.1f sent=%lu txfail=%lu lost=%lu qmax=%lu underflows=%lu ring=%lu enc=%.1f%% core=%d\n",
           name, kbps_target, st->bytes * 8.0 / (dt / 1000.0), (unsigned long)st->sent, (unsigned long)st->fail,
           (unsigned long)st->lost, (unsigned long)st->qmax, (unsigned long)st->under, (unsigned long)(s_hw - s_hr),
           st->enc_us * 100.0 / dt, (int)xPortGetCoreID());
    st->bytes = 0;
    st->qmax = 0;
    st->enc_us = 0;
    st->t0 = now;
}

static void hp_status_playing(void);

/* ---- fixed-rate codec steps (pc_cstep, host-tested): aptX HD and aptX have one bit rate each, so a link that
 * cannot carry it (Wi-Fi traffic) loses packets in bursts. On sustained losses the stream steps to the next rung
 * the sink also has (aptX HD 576 > aptX 384 > SBC at bitpool 35, 249 kbps) and back up after a quiet spell. A step
 * is done in place: suspend, esp_a2d_source_set_pref_mcc (AVDTP reconfiguration; another sink endpoint means a
 * close/open of the media channel, the ACL stays), start. The player keeps feeding the ring meanwhile (it waits:
 * MP3 loses nothing, the radio's buffer covers it). If the sink refuses, the stream goes on with the codec it has
 * and stepping stops for this connection. cfg cstep=0 turns it off (default 1). */
static unsigned s_sink_mask;                 /* codecs the sink offers (ESP_A2D_REPORT_SNK_ALL_CODEC_CAPS_EVT) */
static EXT_RAM_BSS_ATTR uint32_t s_sink_rate[PC_BTC_N];       /* BTAUDIO: the highest rate each of them takes (the same report) */
/* the highest sample rate a capability set names (the sink's offer, not the one in use) */
static uint32_t mcc_max_rate(const esp_a2d_mcc_t *m)
{
    if (m->type == ESP_A2D_MCT_NON_A2DP) {
        const uint8_t f = m->cie.vendor_info.value[0];
        if (mcc_codec(m) == PC_BTC_LDAC) return f & ESP_A2D_LDAC_SF_96K ? 96000 : f & ESP_A2D_LDAC_SF_88K ? 88200 : f & ESP_A2D_LDAC_SF_48K ? 48000 : 44100;
        return f & ESP_A2D_APTX_SF_48K ? 48000 : 44100;
    }
    if (m->type == ESP_A2D_MCT_SBC) return m->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_48K ? 48000 : 44100;
    return m->cie.m24_info.samp_freq2 & ESP_A2D_M24_CIE_SF2_48K ? 48000 : 44100;
}
static pc_cstep_t s_stp;                      /* n = 0: no ladder yet on this connection */
static pc_btc_t s_stp_rung[PC_BTC_N];
static volatile bool s_sbc_low;              /* SBC as a rung: the lower bitpool */
static int64_t s_stp_t_req;
static esp_timer_handle_t s_stp_timer;
static uint32_t s_stp_lost_total;
static bool hp_switching(void) { return s_stp_target >= 0; }
static int stp_index(pc_btc_t c)
{
    for (int i = 0; i < s_stp.n; i++)
        if (s_stp_rung[i] == c) return i;
    return -1;
}

/* the one configuration asked for (the stack still checks it against both sides) */
static void stp_mcc(pc_btc_t c, esp_a2d_mcc_t *m)
{
    memset(m, 0, sizeof *m);
    bool k48 = s_link_rate == 48000;
    if (c == PC_BTC_SBC) {
        m->type = ESP_A2D_MCT_SBC;
        m->cie.sbc_info.samp_freq = k48 ? ESP_A2D_SBC_CIE_SF_48K : ESP_A2D_SBC_CIE_SF_44K;
        m->cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
        m->cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_16;
        m->cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
        m->cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
        m->cie.sbc_info.min_bitpool = 2;
        m->cie.sbc_info.max_bitpool = 53;
        return;
    }
    m->type = ESP_A2D_MCT_NON_A2DP;
    m->cie.vendor_info.vendor_id = c == PC_BTC_APTX ? ESP_A2D_APTX_VENDOR_ID : ESP_A2D_APTX_HD_VENDOR_ID;
    m->cie.vendor_info.codec_id = c == PC_BTC_APTX ? ESP_A2D_APTX_CODEC_ID : ESP_A2D_APTX_HD_CODEC_ID;
    m->cie.vendor_info.value[0] = (k48 ? ESP_A2D_APTX_SF_48K : ESP_A2D_APTX_SF_44K) | ESP_A2D_APTX_CH_STEREO;
}

/* the switch did not happen (refused, or no answer): go on with the codec in use, no more steps */
static void stp_abort(const char *why)
{
    if (!hp_switching()) return;
    int in_use = stp_index(mcc_codec(&s_hp_mcc));
    printf("APP bt hp step to %s failed (%s): staying on %s, no more steps on this connection\n",
           pc_btc_name(s_stp_rung[s_stp_target]), why, mcc_name(&s_hp_mcc));
    pc_cstep_failed(&s_stp, in_use < 0 ? 0 : in_use);
    s_sbc_low = mcc_codec(&s_hp_mcc) == PC_BTC_SBC && in_use > 0;
    s_stp_target = -1;
    if (s_stp_timer) esp_timer_stop(s_stp_timer);
    s_hp_gen++;
    if (s_hp_connected) esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);   /* start again, old codec */
}
static void stp_timeout_cb(void *arg) { (void)arg; stp_abort("timeout"); }

/* first stream of a connection: the ladder from the negotiated codec */
static void stp_setup(pc_btc_t c)
{
    if (s_stp.n) return;
    int n = cfg_int("cstep", 1) ? pc_btc_ladder(c, s_sink_mask, s_stp_rung) : 1;
    pc_cstep_init(&s_stp, n);
    s_stp_lost_total = 0;
    if (n > 1) {
        char l[64] = "";
        for (int i = 0; i < n; i++) snprintf(l + strlen(l), sizeof l - strlen(l), "%s%s", i ? " > " : "", pc_btc_name(s_stp_rung[i]));
        printf("APP bt hp step ladder %s (sink mask 0x%x)\n", l, s_sink_mask);
    }
}

/* encoders, after each packet: losses since the last call. Starts a step when the policy asks for one. */
static void hp_cs_tick(int lost)
{
    if (s_stp.n <= 1 || hp_switching()) return;
    s_stp_lost_total += (uint32_t)lost;
    int cur = s_stp.level;
    int lv = pc_cstep_update(&s_stp, (uint32_t)(esp_timer_get_time() / 1000), lost, wifi_svc_on());
    if (lv == cur) return;
    printf("APP bt hp step %s -> %s (%s, lost %lu so far, wifi=%d, next up-wait %lu s)\n", pc_btc_name(s_stp_rung[cur]),
           pc_btc_name(s_stp_rung[lv]), lv > cur ? "losses" : "quiet", (unsigned long)s_stp_lost_total, wifi_svc_on(),
           (unsigned long)(s_stp.up_wait_ms / 1000));
    s_stp_target = lv;                                  /* the encoders stop (hp_owns) */
    s_stp_t_req = esp_timer_get_time();
    if (!s_stp_timer) {
        esp_timer_create_args_t a = { .callback = stp_timeout_cb, .name = "hp_step" };
        esp_timer_create(&a, &s_stp_timer);
    }
    if (s_stp_timer) { esp_timer_stop(s_stp_timer); esp_timer_start_once(s_stp_timer, 8000000); }
    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);    /* then hp_cs_suspended() */
}

/* BTC task: the stream is suspended for a step: ask for the new configuration */
static void hp_cs_suspended(void)
{
    esp_a2d_mcc_t m;
    pc_btc_t c = s_stp_rung[s_stp_target];
    stp_mcc(c, &m);
    s_sbc_low = c == PC_BTC_SBC && s_stp_target > 0;
    esp_err_t e = esp_a2d_source_set_pref_mcc(s_hp_hdl, &m);
    printf("APP bt hp step suspended after %lld ms, set_pref_mcc %s rc=%s\n", (esp_timer_get_time() - s_stp_t_req) / 1000,
           pc_btc_name(c), esp_err_to_name(e));
    if (e != ESP_OK) stp_abort("set_pref_mcc");
}

/* BTC task: the reconfiguration's answer (the new codec came in ESP_A2D_AUDIO_CFG_EVT before it) */
static void hp_cs_pref_done(esp_bt_status_t st)
{
    if (!hp_switching()) return;
    if (st != ESP_BT_STATUS_SUCCESS) { char w[24]; snprintf(w, sizeof w, "status %d", st); stp_abort(w); return; }
    if (mcc_codec(&s_hp_mcc) != s_stp_rung[s_stp_target]) { stp_abort("other codec"); return; }
    printf("APP bt hp step reconfigured to %s after %lld ms, starting\n", mcc_name(&s_hp_mcc), (esp_timer_get_time() - s_stp_t_req) / 1000);
    s_stp_target = -1;
    if (s_stp_timer) esp_timer_stop(s_stp_timer);
    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);   /* then START, then hp_stream_on */
}

static void hp_cs_reset(void)                       /* a new connection, or none */
{
    if (s_stp_timer) esp_timer_stop(s_stp_timer);
    s_stp_target = -1;
    s_stp.n = 0;
    s_sbc_low = false;
}

/* The fx mode bit of a vendor-codec stream (spec fx-apps §5): aptX / aptX HD suspend the HEAVY apps (the trade-off list: with the full chain
 * core 1 overran, 9 IDLE1 watchdogs in 70 s); LDAC suspends them too (T12 bench, 2026-11-14: all heavy apps on at LDAC 990 = 99 %
 * of core 1, FAIL; light + freeverb alone passed at 66-69 %, see the trade-off notes). cfg venc_full_fx (tests): 1 = no bit for every vendor
 * codec, 0 = the aptX bit for every one, unset or -1 = the defaults above. The caller sets the bit at the stream start and clears it at the end. */
static uint32_t venc_fx_bit(pc_btc_t c)
{
    int k = cfg_int("venc_full_fx", -1);
    uint32_t bit = k == 1 ? 0 : k == 0 ? PC_FXM_APTX : c == PC_BTC_LDAC ? PC_FXM_LDAC : PC_FXM_APTX;
    printf("APP bt hp fx bit=%u (%s, cfg venc_full_fx=%d)\n", (unsigned)bit, pc_btc_name(c), k);
    return bit;
}

static void ldac_set_level(HANDLE_LDAC_BT h, int lv)
{
    ldacBT_set_eqmid(h, lv == 0 ? LDACBT_EQMID_HQ : lv == 3 ? LDACBT_EQMID_MQ : LDACBT_EQMID_SQ);
    if (lv == 2) ldacBT_alter_eqmid_priority(h, LDACBT_EQMID_INC_CONNECTION);   /* SQ -> Q0 (492) */
}

/* Wi-Fi as the ABR sees it. cfg ldac_test_nocap=1 (tests only) lifts the 660 kbps cap, to watch the ABR step
 * down on a link that really fails (990 with Wi-Fi traffic, stage 1) */
static bool s_ldac_nocap;
static bool ldac_wifi(void) { return wifi_svc_on() && !s_ldac_nocap; }

/* tests only: cfg ldac_test_kbps=N (> 0; read every second, `seti ldac_test_kbps N`) pretends the link carries
 * only N kbps. Packets above that are not sent and count as send failures, as a congested link's would, so the
 * ABR can be watched stepping down to 330 and back on hardware. 0 (default) = off. */
/* cfg link_test_kbps does the same for aptX / aptX HD and SBC (the codec steps, pc_cstep). */
typedef struct { int kbps; int64_t t_us, check_us; double tokens; const char *key; } ldac_sim_t;
static bool ldac_sim_drop(ldac_sim_t *m, int len)
{
    int64_t now = esp_timer_get_time();
    if (now - m->check_us >= 1000000) {
        const char *key = m->key ? m->key : "ldac_test_kbps";
        int k = cfg_int(key, 0);
        if (k != m->kbps) printf("APP bt hp TEST (%s): link limited to %d kbps (0 = off)\n", key, k);
        m->kbps = k;
        m->check_us = now;
    }
    if (m->kbps <= 0) { m->t_us = now; m->tokens = 0; return false; }
    m->tokens += (now - m->t_us) * (m->kbps * 1000.0 / 8 / 1e6);
    m->t_us = now;
    double burst = m->kbps * 1000.0 / 8 * 0.2;             /* 200 ms of slack, like the source queue */
    if (m->tokens > burst) m->tokens = burst;
    if (m->tokens < len) return true;
    m->tokens -= len;
    return false;
}

static void hp_ldac_stream(uint32_t gen)
{
    const uint32_t rate = s_link_rate;
    int mtu = cfg_int("ldac_test_mtu", 0) > 0 ? cfg_int("ldac_test_mtu", 0) : s_hp_mtu;   /* TEST: pretend a small MTU */
    if (!pc_btc_fits_mtu(PC_BTC_LDAC, mtu)) {          /* libldac needs one whole 2-DH5 packet at every rate */
        printf("APP bt hp LDAC needs mtu >= %d, link mtu=%d\n", PC_LDAC_MTU_MIN, mtu);
        hp_codec_failed(PC_BTC_LDAC, "MTU 太小", "MTU too small");
        return;
    }
    s_ldac_nocap = cfg_int("ldac_test_nocap", 0) != 0;
    pc_ldac_abr_t abr;
    pc_ldac_abr_init(&abr, ldac_wifi(), s_ldac_kbps);
    int lv = abr.level;
    size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), before_ps = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    HANDLE_LDAC_BT h = ldacBT_get_handle();
    if (!h || ldacBT_init_handle_encode(h, s_hp_mtu, LDACBT_EQMID_SQ, LDACBT_CHANNEL_MODE_STEREO, LDACBT_SMPL_FMT_S16, (int)rate)) {
        printf("APP bt hp encoder=LDAC init failed err=%d mtu=%u\n", h ? ldacBT_get_error_code(h) : -1, s_hp_mtu);
        if (h) ldacBT_free_handle(h);
        hp_codec_failed(PC_BTC_LDAC, "初始化失敗", "init failed");
        return;
    }
    ldac_set_level(h, lv);
    s_venc_kbps = pc_ldac_kbps(pc_ldac_level(lv)->frame_bytes, rate);   /* 909/606/452/303 at 44.1 */
    s_venc_nom = pc_ldac_level(lv)->kbps;
    uint32_t fx_bit = venc_fx_bit(PC_BTC_LDAC);
    fx_mode_set(fx_bit, true);
    hp_status_playing();
    printf("APP bt hp encoder=LDAC rate=%lu level=%d kbps (cap %d, wifi=%d%s) mtu=%u internal=%u B psram=%u B\n",
           (unsigned long)rate, s_venc_kbps, s_ldac_kbps, wifi_svc_on(), s_ldac_nocap ? ", TEST: no Wi-Fi cap" : "", s_hp_mtu,
           (unsigned)(before - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), (unsigned)(before_ps - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    static int16_t pcm[2 * LDACBT_ENC_LSU];
    static uint8_t out[LDACBT_MAX_NBYTES];
    venc_stats_t st = { .t0 = esp_timer_get_time() };
    esp_a2d_source_get_tx_stats(NULL, &st.drops0);
    pace_t pace = { .t0 = esp_timer_get_time(), .rate = rate };
    uint32_t ts = 0, errs = 0;
    ldac_sim_t sim = { .t_us = esp_timer_get_time() };
    while (hp_owns(PC_ENC_VENDOR, gen) && mcc_is_ldac(&s_hp_mcc)) {
        hp_pull(pcm, LDACBT_ENC_LSU, &st);
        int used = 0, sz = 0, nf = 0;
        int64_t t = esp_timer_get_time();
        int r = ldacBT_encode(h, pcm, &used, out, &sz, &nf);
        st.enc_us += esp_timer_get_time() - t;
        if (r) { if (errs++ < 5) printf("APP bt hp LDAC encode error 0x%x\n", ldacBT_get_error_code(h)); }
        else if (sz > 0) {
            uint32_t f0 = st.fail;
            if (ldac_sim_drop(&sim, sz)) st.fail++;         /* TEST knob: as if the stack refused it */
            else hp_send(out, sz, nf, ts, &st);
            ts += (uint32_t)nf * LDACBT_ENC_LSU;          /* the timestamp of the next packet's first frame */
            int q, lost = hp_flow(&st, &q, f0);
            int nl = pc_ldac_abr_update(&abr, (uint32_t)(esp_timer_get_time() / 1000), q, lost, ldac_wifi(), s_ldac_kbps);
            if (nl != lv) {
                printf("APP bt hp LDAC abr %d -> %d kbps (queue=%d lost=%d wifi=%d)\n", pc_ldac_kbps(pc_ldac_level(lv)->frame_bytes, rate),
                       pc_ldac_kbps(pc_ldac_level(nl)->frame_bytes, rate), q, lost, wifi_svc_on());
                ldac_set_level(h, nl);
                lv = nl;
                s_venc_kbps = pc_ldac_kbps(pc_ldac_level(lv)->frame_bytes, rate);   /* 909/606/452/303 at 44.1 */
                s_venc_nom = pc_ldac_level(lv)->kbps;
                hp_status_playing();
            }
        }
        hp_venc_report("LDAC", &st, s_venc_kbps);
        hp_pace(&pace, LDACBT_ENC_LSU);
    }
    ldacBT_free_handle(h);
    fx_mode_set(fx_bit, false);
    printf("APP bt hp LDAC end sent=%lu txfail=%lu lost=%lu abr_downs=%lu ups=%lu\n", (unsigned long)st.sent, (unsigned long)st.fail,
           (unsigned long)st.lost, (unsigned long)abr.downs, (unsigned long)abr.ups);
}

static void hp_aptx_stream(bool hd, uint32_t gen)
{
    const uint32_t rate = s_link_rate;
    const int ns = pc_aptx_samples_per_packet(hd, rate, s_hp_mtu), nb = pc_aptx_packet_bytes(hd, ns);
    const char *name = hd ? "aptX HD" : "aptX";
    size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    struct aptx_context *c = cfg_int("venc_test_fail", 0) ? NULL : aptx_init(hd);   /* TEST: as if it failed */
    int16_t *pcm = heap_caps_malloc((size_t)ns * 4, MALLOC_CAP_SPIRAM);
    uint8_t *in = heap_caps_malloc((size_t)ns * 6, MALLOC_CAP_SPIRAM), *out = heap_caps_malloc((size_t)nb + 32, MALLOC_CAP_SPIRAM);
    printf("APP bt hp encoder=%s rate=%lu samples/pkt=%d bytes/pkt=%d rtp=%d mtu=%u internal=%u B ok=%d\n", name, (unsigned long)rate,
           ns, nb, hd, s_hp_mtu, (unsigned)(before - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), c && pcm && in && out);
    pc_btc_t want = hd ? PC_BTC_APTX_HD : PC_BTC_APTX;
    if (!(c && pcm && in && out)) {                     /* set-up failed: free what we got, report once, fall back */
        if (c) aptx_finish(c);
        heap_caps_free(pcm);
        heap_caps_free(in);
        heap_caps_free(out);
        hp_codec_failed(want, "記憶體不足", "out of memory");
        return;
    }
    uint32_t fx_bit = venc_fx_bit(want);
    fx_mode_set(fx_bit, true);
    stp_setup(want);
    hp_status_playing();
    venc_stats_t st = { .t0 = esp_timer_get_time() };
    esp_a2d_source_get_tx_stats(NULL, &st.drops0);
    pace_t pace = { .t0 = esp_timer_get_time(), .rate = rate };
    uint32_t ts = 0;
    ldac_sim_t sim = { .t_us = esp_timer_get_time(), .key = "link_test_kbps" };
    while (hp_owns(PC_ENC_VENDOR, gen) && mcc_codec(&s_hp_mcc) == want) {
        hp_pull(pcm, ns, &st);
        for (int i = 0; i < 2 * ns; i++) {                 /* int16 -> 24-bit little-endian, as libfreeaptx reads */
            int32_t v = (int32_t)pcm[i] * 256;
            in[3 * i] = (uint8_t)v;
            in[3 * i + 1] = (uint8_t)(v >> 8);
            in[3 * i + 2] = (uint8_t)(v >> 16);
        }
        size_t w = 0;
        int64_t t = esp_timer_get_time();
        aptx_encode(c, in, (size_t)ns * 6, out, (size_t)nb + 32, &w);
        st.enc_us += esp_timer_get_time() - t;
        uint32_t f0 = st.fail;
        if (w && ldac_sim_drop(&sim, (int)w)) st.fail++;      /* TEST knob: as if the stack refused it */
        else if (w) hp_send(out, (int)w, 1, ts, &st);
        ts += (uint32_t)ns;
        int q;
        hp_cs_tick(hp_flow(&st, &q, f0));
        hp_venc_report(name, &st, (int)((uint64_t)nb * 8 * rate / ns / 1000));
        hp_pace(&pace, ns);
    }
    aptx_finish(c);
    fx_mode_set(fx_bit, false);
    heap_caps_free(pcm);
    heap_caps_free(in);
    heap_caps_free(out);
    printf("APP bt hp %s end sent=%lu txfail=%lu lost=%lu\n", name, (unsigned long)st.sent, (unsigned long)st.fail, (unsigned long)st.lost);
}

void hp_venc_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t gen = s_hp_gen;
        pc_btc_t c = mcc_codec(&s_hp_mcc);
        if (!s_hp_streaming || hp_switching() || pc_btc_encoder(c) != PC_ENC_VENDOR) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (!ring_take(PC_ENC_VENDOR, gen)) continue;
        if (c == PC_BTC_LDAC) hp_ldac_stream(gen);
        else hp_aptx_stream(c == PC_BTC_APTX_HD, gen);
        ring_give();
    }
}
#else
int bt_svc_ldac_kbps(int kbps) { (void)kbps; return -1; }
static void hp_status_playing(void);
static bool hp_switching(void) { return false; }
#endif

/* the codec line for the status ("LDAC 660 kbps", "aptX HD", "AAC"); "" while nothing streams */
static char s_codec_line[32];
const char *bt_svc_codec(void)
{
    if (!s_hp_streaming) return "";
    pc_btc_t c = mcc_codec(&s_hp_mcc);
#if JC_LDAC
    if (c == PC_BTC_LDAC) { snprintf(s_codec_line, sizeof s_codec_line, "LDAC %d kbps", s_venc_kbps); return s_codec_line; }
#endif
#if JC_LDAC
    if (s_stp.n > 1 && s_stp.level > 0) {               /* stepped down for a busy link */
        snprintf(s_codec_line, sizeof s_codec_line, T(S_S_STEPPED_DOWN) /* %s（暫時） */, pc_btc_name(c));
        return s_codec_line;
    }
#endif
    snprintf(s_codec_line, sizeof s_codec_line, "%s", pc_btc_name(c));
    return s_codec_line;
}

/* BTAUDIO: the live state of the headphones link for the output status (any task): connected, the sink's codecs and their top rates (A2DP capability exchange; vendor builds only,
 * else the codec in use stands for the sink), the adaptive LDAC step, a codec step, Wi-Fi. The preferences are added by the caller (out_status_bt_prefs). */
void bt_svc_out_in(pc_os_bt_in_t *in)
{
    in->connected = s_mode == BT_MODE_HEADPHONES && s_hp_connected;
#if JC_LDAC
    in->build_ldac = true;
    in->sink_codecs = s_sink_mask;
    for (int c = 0; c < PC_BTC_N; c++) in->sink_rate[c] = s_sink_rate[c];
    in->live_kbps = s_hp_streaming && mcc_codec(&s_hp_mcc) == PC_BTC_LDAC ? s_venc_nom : 0;
    if (s_stp.n > 1 && s_stp.level > 0) { in->step_rung = s_stp.level; in->step_codec = (int)mcc_codec(&s_hp_mcc); }
#else
    in->build_ldac = false;
    if (in->connected && s_hp_streaming) { const pc_btc_t c = mcc_codec(&s_hp_mcc); if (c != PC_BTC_NONE) { in->sink_codecs = 1u << c; in->sink_rate[c] = s_link_rate; } }
#endif
    in->wifi_on = wifi_svc_on();
    in->failed = s_hp_skip;                              /* FIX4 I3: codecs that could not start this boot are no longer on offer */
    if (in->connected && s_hp_neg && mcc_codec(&s_hp_mcc) != PC_BTC_NONE) {   /* the negotiated state: what the stack configured, not what the preferences would give */
        int ch;
        in->neg = true;
        in->neg_codec = (uint8_t)mcc_codec(&s_hp_mcc);
        in->neg_rate = mcc_rate(&s_hp_mcc, &ch);
        in->neg_chan = s_neg_chan;
    }
}
/* FIX4 I3: the preferences the registered endpoints were made with (the status of a connected link is planned with these, not with the stored values) */
void bt_svc_applied_prefs(pc_os_bt_in_t *in)
{
    in->pref_codec = s_pf.codec; in->pref_rate = (uint32_t)s_pf.rate; in->pref_bits = s_pf.bits; in->pref_chan = s_pf.chan;
}
bool bt_svc_prefs_pending(void) { return s_mode == BT_MODE_HEADPHONES && pf_reconnect_needed(); }
static void hp_status_playing(void)
{
    snprintf(s_status, sizeof s_status, "%s%s (%s)%s%s", T(S_PLAYING_ON) /* 輸出到耳機： */, s_peer[0] ? s_peer : "?", bt_svc_codec(),
             s_hp_note[0] ? " - " : "", s_hp_note);
    ui_async(ui_status, NULL);
}

/* encoder task: pulls the ring at real time, encodes (AAC or SBC as negotiated) and sends */
void hp_encode_task(void *arg)
{
    (void)arg;
    for (;;) {
        while (!s_hp_streaming) vTaskDelay(pdMS_TO_TICKS(20));
        uint32_t gen = s_hp_gen;
        if (hp_switching() || pc_btc_encoder(mcc_codec(&s_hp_mcc)) != PC_ENC_STD) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }   /* bt_venc's */
        if (!ring_take(PC_ENC_STD, gen)) continue;
        bool aac = s_hp_mcc.type == ESP_A2D_MCT_M24;
        esp_audio_enc_handle_t enc = NULL;
        esp_audio_err_t r;
        if (aac) {
            esp_aac_enc_config_t c = ESP_AAC_ENC_CONFIG_DEFAULT();
            c.sample_rate = (int)s_link_rate;
            c.channel = 2;
            c.bits_per_sample = 16;
            c.bitrate = 256000;
            c.adts_used = false;
            esp_audio_enc_config_t ec = { .type = ESP_AUDIO_TYPE_AAC, .cfg = &c, .cfg_sz = sizeof c };
            esp_aac_enc_register();
            r = esp_audio_enc_open(&ec, &enc);
        } else {
            esp_sbc_enc_config_t c = ESP_SBC_STD_ENC_CONFIG_DEFAULT();
            c.sample_rate = s_link_rate;
            c.ch_mode = ESP_SBC_CH_MODE_JOINT_STEREO;
            c.allocation_method = ESP_SBC_ALLOC_LOUDNESS;
            c.bitpool = s_hp_mcc.cie.sbc_info.max_bitpool < 53 ? s_hp_mcc.cie.sbc_info.max_bitpool : 53;   /* "high quality" */
#if JC_LDAC
            if (s_sbc_low && c.bitpool > PC_SBC_STEP_BITPOOL) c.bitpool = PC_SBC_STEP_BITPOOL;   /* a codec step's bottom rung */
#endif
            c.block_length = 16;
            c.sub_bands_num = 8;
            esp_audio_enc_config_t ec = { .type = ESP_AUDIO_TYPE_SBC, .cfg = &c, .cfg_sz = sizeof c };
            esp_sbc_enc_register();
            r = esp_audio_enc_open(&ec, &enc);
        }
        int in_sz = 0, out_sz = 0;
        if (r == ESP_AUDIO_ERR_OK) esp_audio_enc_get_frame_size(enc, &in_sz, &out_sz);
        printf("APP bt hp encoder=%s rate=%lu in=%d out=%d rc=%d%s\n", aac ? "AAC" : "SBC", (unsigned long)s_link_rate, in_sz, out_sz, r,
#if JC_LDAC
               s_sbc_low ? " (step rung, bitpool 35)" : "");
#else
               "");
#endif
        if (r != ESP_AUDIO_ERR_OK || in_sz <= 0) {
            if (enc) esp_audio_enc_close(enc);
            ring_give();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        int fs = in_sz / 4;                                    /* samples per frame (stereo int16) */
        uint8_t *pcm = heap_caps_malloc((size_t)in_sz, MALLOC_CAP_SPIRAM), *frame = heap_caps_malloc((size_t)out_sz, MALLOC_CAP_SPIRAM);
        uint8_t *pkt = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
        uint32_t ts = 0;
        int64_t next = esp_timer_get_time();
        uint32_t under = 0, sent = 0, fails = 0;
#if JC_LDAC
        uint32_t drops0 = 0, fails0 = 0;
        esp_a2d_source_get_tx_stats(NULL, &drops0);
        ldac_sim_t sim = { .t_us = esp_timer_get_time(), .key = "link_test_kbps" };
        if (!aac && s_stp.n > 1) printf("APP bt hp SBC on step rung %d of %d\n", s_stp.level, s_stp.n);
#endif
        while (hp_owns(PC_ENC_STD, gen) && pcm && frame && pkt) {
            int nf = 0, len = 0, cap = aac ? 1 : 15;
            int maxlen = (int)s_hp_mtu - (aac ? 20 : 13);
            while (nf < cap) {
                int16_t *p16 = (int16_t *)pcm;
                bool d0 = false;                               /* the block started dry (one underflow, as before) */
                for (int i = 0; i < fs; i++) {
                    if (s_hw == s_hr) { p16[2 * i] = p16[2 * i + 1] = 0; if (i == 0) { under++; d0 = true; } continue; }
                    uint32_t k = s_hr % HP_RING;
                    p16[2 * i] = s_hp_ring[2 * k];
                    p16[2 * i + 1] = s_hp_ring[2 * k + 1];
                    __atomic_store_n(&s_hr, s_hr + 1, __ATOMIC_RELEASE);
                }
                sink_block(d0);
                esp_audio_enc_in_frame_t in = { .buffer = pcm, .len = (uint32_t)in_sz };
                esp_audio_enc_out_frame_t o = { .buffer = frame, .len = (uint32_t)out_sz };
                if (esp_audio_enc_process(enc, &in, &o) != ESP_AUDIO_ERR_OK || !o.encoded_bytes) break;
                if (len + (int)o.encoded_bytes > maxlen || len + (int)o.encoded_bytes > 2048) { /* next packet */
                    if (!nf) break;
                }
                memcpy(pkt + len, frame, o.encoded_bytes);
                len += (int)o.encoded_bytes;
                nf++;
                if (len + (int)o.encoded_bytes > maxlen) break;
            }
            if (!nf) { vTaskDelay(1); continue; }
            esp_a2d_audio_buff_t *b = esp_a2d_audio_buff_alloc((uint16_t)len);
#if JC_LDAC
            if (b && ldac_sim_drop(&sim, len)) { esp_a2d_audio_buff_free(b); b = NULL; fails++; }   /* TEST knob */
#endif
            if (b) {
                memcpy(b->data, pkt, (size_t)len);
                b->data_len = (uint16_t)len;
                b->number_frame = (uint16_t)nf;
                b->timestamp = ts;
                if (esp_a2d_source_audio_data_send(s_hp_hdl, b) != ESP_OK) { esp_a2d_audio_buff_free(b); fails++; }
                else sent++;
            }
#if JC_LDAC
            if (s_stp.n > 1) {                                  /* on a codec step's rung: losses for the policy */
                uint32_t d = drops0;
                esp_a2d_source_get_tx_stats(NULL, &d);
                hp_cs_tick((int)(d - drops0) + (int)(fails - fails0));
                drops0 = d;
                fails0 = fails;
            }
#endif
            ts += (uint32_t)(nf * fs);
            next += (int64_t)nf * fs * 1000000 / s_link_rate;          /* real time */
            int64_t wait = next - esp_timer_get_time();
            if (wait > 2000) vTaskDelay(pdMS_TO_TICKS(wait / 1000));
            if ((sent & 255) == 0 && sent) {
#if JC_LDAC
                uint32_t d = 0;
                esp_a2d_source_get_tx_stats(NULL, &d);
                printf("APP bt hp sent=%lu underflows=%lu ring=%lu txfail=%lu drops=%lu rsqdrop=%lu\n", (unsigned long)sent, (unsigned long)under,
                       (unsigned long)(s_hw - s_hr), (unsigned long)fails, (unsigned long)d, (unsigned long)s_hp_rs.dropped_total);
#else
                printf("APP bt hp sent=%lu underflows=%lu ring=%lu rsqdrop=%lu\n", (unsigned long)sent, (unsigned long)under, (unsigned long)(s_hw - s_hr),
                       (unsigned long)s_hp_rs.dropped_total);
#endif
            }
        }
        if (fails) printf("APP bt hp %s end sent=%lu txfail=%lu\n", aac ? "AAC" : "SBC", (unsigned long)sent, (unsigned long)fails);
        if (enc) esp_audio_enc_close(enc);
        heap_caps_free(pcm);
        heap_caps_free(frame);
        heap_caps_free(pkt);
        ring_give();
    }
}

void hp_rsq_sink(bool on)                         /* LVGL, sink on/off: the table is kept, its history starts silent; the builder wakes */
{
    s_hp_rs.on = on;
    pc_rsq_swap_restart(&s_hp_rs.sw);
    if (s_hp_rs.task) xTaskNotifyGive(s_hp_rs.task);
    /* hires 10c: sink start builds the sink's table BEFORE the first block (the caller sets the sink right after this returns): the first block
     * never waits for it. The pair is the builder's plan (pc_rsq_hints); already held (live or cached spare), a pass-through, or a plan above 48k
     * (the block is re-planned after the sink is set) need no wait. Bounded (150 ms: a build is about 26 ms); a miss only falls back to the
     * waiting block of T10b, counted in rsqdrop=. */
    if (!on || !s_hp_rs.task) return;
    aud_info_t ai;
    audio_svc_info(&ai);
    pc_rsq_pair_t now, next;
    pc_rsq_hints(true, ai.src_rate, ai.pipe_rate, ai.next_rate, s_link_rate, &now, &next);
    if (!now.in || now.in == now.out) return;
    if (pc_rsq_swap_spare_busy(&s_hp_rs.sw) && !pc_rsq_swap_has(&s_hp_rs.sw, now)) return;   /* full cache, wrong pair: only a block hands the spare back (no sink yet), so no wait; the first block's WAIT does it */
    int64_t t0 = esp_timer_get_time();
    while (!pc_rsq_swap_has(&s_hp_rs.sw, now) && esp_timer_get_time() - t0 < 150000) {
        xTaskNotifyGive(s_hp_rs.task);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    printf("APP rsq sink-start in=%lu out=%lu ready=%d wait_us=%lu\n", (unsigned long)now.in, (unsigned long)now.out, pc_rsq_swap_has(&s_hp_rs.sw, now),
           (unsigned long)(esp_timer_get_time() - t0));
}

void ui_hp_stream(void *arg)                      /* LVGL: route the player's output */
{
    bool on = (bool)(intptr_t)arg;
    s_hw = s_hr = 0;
    s_sink_sess++;                                       /* fix 2 (N2): a new sink session */
    hp_rsq_sink(on);
    vol_out_switch(on ? PC_VOUT_BT : PC_VOUT_WIRED, s_hp_bda, on ? hp_sink : NULL);   /* BTVOL: this headset's own volume (or the wired one back), in the safe order, and the sink */
    player_ctl_changed();
}

static void hp_stream_on(void)
{
    s_hp_gen++;
    s_hp_streaming = true;
    ui_async(ui_hp_stream, (void *)1);
    hp_status_playing();
}
/* one try to start the stream again, 1 s after a suspend or a reconfiguration (no loop: a refused start stays
 * stopped, the player on 3.5 mm) */
static esp_timer_handle_t s_hp_restart_t;
static void hp_restart_cb(void *arg)
{
    (void)arg;
    if (s_hp_connected && !s_hp_streaming) esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
}
static void hp_restart_later(void)
{
    if (!s_hp_restart_t) {
        esp_timer_create_args_t a = { .callback = hp_restart_cb, .name = "hp_restart" };
        if (esp_timer_create(&a, &s_hp_restart_t) != ESP_OK) return;
    }
    esp_timer_stop(s_hp_restart_t);
    esp_timer_start_once(s_hp_restart_t, 1000000);
}

/* headphones that drop (out of range, switched off) are called back: 3 s, doubling, then once a minute.
 * Not after our own disconnect (mode change, codec restart): s_hp_leaving. */
esp_timer_handle_t s_rc_t;
int s_rc_n;
volatile bool s_hp_leaving;
static void hp_reconnect_cb(void *arg)
{
    (void)arg;
    if (s_mode != BT_MODE_HEADPHONES || !s_hp_active || s_hp_leaving || s_hp_connected) return;
    printf("APP bt hp reconnect try=%d\n", s_rc_n);
    esp_a2d_source_connect(s_hp_bda);
    snprintf(s_status, sizeof s_status, "%s", T(S_RECONNECTING_HEADPHONES) /* 重新連線耳機中… */);
}
static void hp_reconnect_later(void)
{
    if (!s_rc_t) {
        esp_timer_create_args_t a = { .callback = hp_reconnect_cb, .name = "hp_rc" };
        if (esp_timer_create(&a, &s_rc_t) != ESP_OK) return;
    }
    esp_timer_stop(s_rc_t);
    esp_timer_start_once(s_rc_t, (uint64_t)pc_bt_reconnect_ms(s_rc_n++) * 1000);
}

static void hp_a2d_event(esp_a2d_cb_event_t ev, esp_a2d_cb_param_t *p)   /* BTC task */
{
    switch (ev) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (p->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            s_hp_connected = true;
            s_hp_hdl = p->conn_stat.conn_hdl;
            if (p->conn_stat.audio_mtu) s_hp_mtu = p->conn_stat.audio_mtu;
            memcpy(s_hp_bda, p->conn_stat.remote_bda, 6);
#if JC_LDAC
            hp_cs_reset();
#endif
            ui_async(ui_remember, NULL);
            esp_bt_gap_read_remote_name(s_hp_bda);
            snprintf(s_status, sizeof s_status, "%s", T(S_HEADPHONES_CONNECTED) /* 耳機已連線 */);
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
            s_rc_n = 0;
            if (s_rc_t) esp_timer_stop(s_rc_t);
        } else if (p->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            s_hp_connected = false;
            s_hp_neg = false;
            if (s_hp_active && !s_hp_leaving) hp_reconnect_later();   /* also after a failed attempt */
            s_hp_streaming = false;
            s_hp_gen++;
#if JC_LDAC
            hp_cs_reset();
            s_sink_mask = 0;
            memset(s_sink_rate, 0, sizeof s_sink_rate);
#endif
            ui_async(ui_hp_stream, (void *)0);
            snprintf(s_status, sizeof s_status, "%s", T(S_HEADPHONES_GONE_3_5_MM_OUT) /* 耳機已斷線（3.5mm 輸出） */);
        }
        printf("APP bt hp state=%d mtu=%u\n", p->conn_stat.state, s_hp_mtu);
        break;
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
        printf("APP bt hp media cmd=%d status=%d\n", p->media_ctrl_stat.cmd, p->media_ctrl_stat.status);
#if JC_LDAC
        if (p->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_SUSPEND && hp_switching() &&
            p->media_ctrl_stat.status != ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) stp_abort("suspend refused");
#endif
        if (p->media_ctrl_stat.status != ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) break;
        if (p->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY) esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
        else if (p->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START && !s_hp_streaming) hp_stream_on();
        break;
    case ESP_A2D_AUDIO_STATE_EVT:                       /* the headphones suspended (or started) the stream */
        printf("APP bt hp audio state=%d\n", p->audio_stat.state);
        if (p->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) { if (!s_hp_streaming) hp_stream_on(); }
#if JC_LDAC
        else if (hp_switching()) {                     /* our suspend for a codec step: the player stays on the ring */
            s_hp_streaming = false;
            s_hp_gen++;
            hp_cs_suspended();
        }
#endif
        else if (s_hp_streaming) {
            s_hp_streaming = false;                        /* the encoders stop; the player goes back to 3.5 mm */
            s_hp_gen++;
            ui_async(ui_hp_stream, (void *)0);
            hp_restart_later();                            /* we still have audio: ask to start again */
        }
        break;
    case ESP_A2D_AUDIO_CFG_EVT: {
        s_hp_gen++;                                        /* a running encoder stops before the codec changes */
        s_hp_mcc = p->audio_cfg.mcc;
        int ch;
        s_link_rate = mcc_rate(&s_hp_mcc, &ch);
        s_neg_chan = (uint8_t)(mcc_codec(&s_hp_mcc) == PC_BTC_SBC && (s_hp_mcc.cie.sbc_info.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO) && !(s_hp_mcc.cie.sbc_info.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_STEREO) ? PC_CHAN_JOINT : PC_CHAN_STEREO);
        s_hp_neg = true;
        printf("APP bt hp codec=%s rate=%lu\n", mcc_name(&s_hp_mcc), (unsigned long)s_link_rate);
        if (s_hp_connected && !s_hp_streaming && !hp_switching()) hp_restart_later();   /* reconfigured after a suspend */
        break;
    }
#if JC_LDAC
    case ESP_A2D_SRC_SET_PREF_MCC_EVT:
        printf("APP bt hp set_pref_mcc status=%d\n", p->a2d_set_pref_mcc_stat.set_status);
        hp_cs_pref_done(p->a2d_set_pref_mcc_stat.set_status);
        break;
    case ESP_A2D_REPORT_SNK_ALL_CODEC_CAPS_EVT: {        /* what the sink has: the rungs of a codec step */
        unsigned m = 0;
        memset(s_sink_rate, 0, sizeof s_sink_rate);
        for (int i = 0; i < p->a2d_report_snk_all_codec_caps_stat.sep_num; i++) {
            pc_btc_t c = mcc_codec(&p->a2d_report_snk_all_codec_caps_stat.sep_mcc[i].mcc);
            if (c != PC_BTC_NONE) {
                m |= 1u << c;
                const uint32_t r = mcc_max_rate(&p->a2d_report_snk_all_codec_caps_stat.sep_mcc[i].mcc);
                if (r > s_sink_rate[c]) s_sink_rate[c] = r;
            }
        }
        s_sink_mask = m;
        printf("APP bt hp sink codecs 0x%x\n", m);
        break;
    }
#endif
    case ESP_A2D_SEP_REG_STATE_EVT:
        printf("APP bt hp sep=%d reg=%d\n", p->a2d_sep_reg_stat.seid, p->a2d_sep_reg_stat.reg_state);
        break;
    default:
        break;
    }
}

static void hp_register_one(int seid, pc_btc_t c)
{
    esp_a2d_mcc_t m = { 0 };
    const bool rate44 = s_pf.rate == 44100;   /* BTAUDIO: the link-rate ceiling narrows what the endpoint offers */
    (void)rate44;
    switch (c) {
#if JC_LDAC
    case PC_BTC_LDAC:
        m.type = ESP_A2D_MCT_NON_A2DP;
        m.cie.vendor_info.vendor_id = ESP_A2D_LDAC_VENDOR_ID;
        m.cie.vendor_info.codec_id = ESP_A2D_LDAC_CODEC_ID;
        m.cie.vendor_info.value[0] = rate44 ? ESP_A2D_LDAC_SF_44K : ESP_A2D_LDAC_SF_44K | ESP_A2D_LDAC_SF_48K;   /* nothing above 48 kHz; bt_rate 44.1k offers only 44.1 */
        m.cie.vendor_info.value[1] = ESP_A2D_LDAC_CH_STEREO;
        break;
    case PC_BTC_APTX_HD:
    case PC_BTC_APTX:
        m.type = ESP_A2D_MCT_NON_A2DP;
        m.cie.vendor_info.vendor_id = c == PC_BTC_APTX ? ESP_A2D_APTX_VENDOR_ID : ESP_A2D_APTX_HD_VENDOR_ID;
        m.cie.vendor_info.codec_id = c == PC_BTC_APTX ? ESP_A2D_APTX_CODEC_ID : ESP_A2D_APTX_HD_CODEC_ID;
        m.cie.vendor_info.value[0] = (rate44 ? ESP_A2D_APTX_SF_44K : ESP_A2D_APTX_SF_44K | ESP_A2D_APTX_SF_48K) | ESP_A2D_APTX_CH_STEREO;
        break;
#endif
    case PC_BTC_AAC:
        m.type = ESP_A2D_MCT_M24;
        m.cie.m24_info.drc = ESP_A2D_M24_CIE_DRC_NS;
        m.cie.m24_info.obj_type = ESP_A2D_M24_CIE_OBJ_TYPE_2_AAC_LC;
        m.cie.m24_info.samp_freq1 = ESP_A2D_M24_CIE_SF1_44K;
        m.cie.m24_info.samp_freq2 = rate44 ? 0 : ESP_A2D_M24_CIE_SF2_48K;
        m.cie.m24_info.ch = ESP_A2D_M24_CIE_CH_2;
        m.cie.m24_info.vbr = 0;
        m.cie.m24_info.br1 = (256000 >> 16) & ESP_A2D_M24_CIE_BR1_MSK;
        m.cie.m24_info.br2 = (256000 >> 8) & 0xFF;
        m.cie.m24_info.br3 = 256000 & 0xFF;
        break;
    case PC_BTC_SBC:
        m.type = ESP_A2D_MCT_SBC;
        m.cie.sbc_info.samp_freq = rate44 ? ESP_A2D_SBC_CIE_SF_44K : ESP_A2D_SBC_CIE_SF_44K | ESP_A2D_SBC_CIE_SF_48K;
        m.cie.sbc_info.ch_mode = s_pf.chan == 1 ? ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO : ESP_A2D_SBC_CIE_CH_MODE_STEREO | ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;   /* bt_chan 1 = joint stereo only; 0 = plain stereo preferred, the stack picks the best the sink also has */
        m.cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_16;
        m.cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
        m.cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
        m.cie.sbc_info.min_bitpool = 2;
        m.cie.sbc_info.max_bitpool = 53;
        break;
    default:
        return;
    }
    esp_err_t e = esp_a2d_source_register_stream_endpoint((uint8_t)seid, &m);
    if (e != ESP_OK) printf("APP bt hp sep=%d %s register rc=%s\n", seid, pc_btc_name(c), esp_err_to_name(e));   /* the peer hit a race here */
}

/* the stack offers our endpoints in seid order: register them best first (LDAC > aptX HD > aptX > AAC > SBC) */
static void hp_register_seps(void)
{
#if JC_LDAC
    unsigned built = (1u << PC_BTC_LDAC) | (1u << PC_BTC_APTX_HD) | (1u << PC_BTC_APTX) | (1u << PC_BTC_AAC) | (1u << PC_BTC_SBC);
    s_ldac_kbps = s_pf.kbps;
#else
    unsigned built = (1u << PC_BTC_AAC) | (1u << PC_BTC_SBC);
#endif
    if (s_hp_skip) printf("APP bt hp endpoints without mask 0x%x (failed this boot)\n", s_hp_skip);
    pc_btc_t order[PC_BTC_N];
    built &= pc_bt_pref_mask(s_pf.codec);   /* BTAUDIO: the codec preference is a ceiling: nothing above it is offered */
    int n = pc_btc_order(built & ~s_hp_skip, order);
    if (n > CONFIG_BT_A2DP_SEP_NUM_MAX) n = CONFIG_BT_A2DP_SEP_NUM_MAX;   /* sdkconfig: room for this many */
    for (int i = 0; i < n; i++) hp_register_one(i, order[i]);
}

#define LS_FQ 4
static void ls_out(const int16_t *l, int lstep, const int16_t *r, int rstep, int nf)
{
    for (int k = 0; k < nf; k++) {
        if (ring_fill() >= RING_FRAMES) break;
        uint32_t w = s_w % RING_FRAMES;
        s_ring[2 * w] = l[k * lstep];
        s_ring[2 * w + 1] = r[k * rstep];
        __atomic_store_n(&s_w, s_w + 1, __ATOMIC_RELEASE);
    }
}
void ls_decode_task(void *arg)
{
    (void)arg;
    static int16_t q[2][LS_FQ][2 * 480];
    static int qn[2], qf[2][LS_FQ], head[2];
    static uint32_t n, bad, plc, solo;
    for (;;) {
        ls_sdu_t s;
        if (xQueueReceive(s_ls_q, &s, portMAX_DELAY) != pdTRUE) continue;
        int i = s.idx;
        if (!s_lc3[i] || !s_ring) continue;
        int slot = (head[i] + qn[i]) % LS_FQ;
        if (qn[i] == LS_FQ) { head[i] = (head[i] + 1) % LS_FQ; qn[i]--; slot = (head[i] + qn[i]) % LS_FQ; }   /* overrun: drop the oldest */
        esp_audio_dec_in_raw_t in = { .buffer = s.data, .len = s.len ? s.len : 1 };
        if (!s.len) { in.frame_recover = ESP_AUDIO_DEC_RECOVERY_PLC; plc++; }   /* lost: concealed */
        esp_audio_dec_out_frame_t o = { .buffer = (uint8_t *)q[i][slot], .len = sizeof q[i][slot] };
        if (esp_audio_dec_process(s_lc3[i], &in, &o) != ESP_AUDIO_ERR_OK) { bad++; continue; }
        qf[i][slot] = (int)(o.decoded_size / 2 / (unsigned)s_ls_ch[i]);
        qn[i]++;
        if (++n % 1000 == 0) {
            printf("APP bt listen frames=%lu bad=%lu plc=%lu solo=%lu qdrop=%lu fill=%lu dry=%lu\n", (unsigned long)n, (unsigned long)bad,
                   (unsigned long)plc, (unsigned long)solo, (unsigned long)s_ls_qdrop, (unsigned long)ring_fill(), (unsigned long)s_dry);
            printf("APP bt listen per-stream ok=%lu/%lu invalid=%lu/%lu gap=%lu/%lu\n", (unsigned long)s_ls_ok[0], (unsigned long)s_ls_ok[1],
                   (unsigned long)s_ls_inval[0], (unsigned long)s_ls_inval[1], (unsigned long)s_ls_gap[0], (unsigned long)s_ls_gap[1]);
        }
        if (s_ls_nstreams < 2) {                         /* one BIS: mono, or both channels in it */
            int16_t *p = q[i][head[i]];
            ls_out(p, s_ls_ch[i], p + (s_ls_ch[i] > 1), s_ls_ch[i], qf[i][head[i]]);
            head[i] = (head[i] + 1) % LS_FQ;
            qn[i]--;
            continue;
        }
        while (qn[0] && qn[1]) {                         /* left and right, one frame each */
            int16_t *l = q[0][head[0]], *r = q[1][head[1]];
            int nf = qf[0][head[0]] < qf[1][head[1]] ? qf[0][head[0]] : qf[1][head[1]];
            ls_out(l, s_ls_ch[0], r, s_ls_ch[1], nf);
            for (int c = 0; c < 2; c++) { head[c] = (head[c] + 1) % LS_FQ; qn[c]--; }
        }
        for (int c = 0; c < 2; c++)
            if (qn[c] >= 3 && !qn[!c]) {                 /* the other side is missing: this one on both channels */
                int16_t *p = q[c][head[c]];
                ls_out(p, s_ls_ch[c], p, s_ls_ch[c], qf[c][head[c]]);
                head[c] = (head[c] + 1) % LS_FQ;
                qn[c]--;
                solo++;
            }
    }
}

#if CONFIG_BT_BAP_BROADCAST_SINK
static void listen_clear(void) { portENTER_CRITICAL(&s_dev_mux); s_ndevs = 0; s_nfound = 0; portEXIT_CRITICAL(&s_dev_mux); }
#endif
#endif
