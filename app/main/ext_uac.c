// SPDX-License-Identifier: LGPL-2.1-or-later
/* 外接模式 T4 / T5: ext_uac.h (firmware only). Static state is PSRAM (this file is built with -msmall-data-limit=0). */
#include "ext_uac.h"
#include <stdio.h>
#include "esp_attr.h"
#include "pc/pc_extuac.h"
#include "tusb.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ext_audio.h"
#include "ext_prio.h"
#include "cfg.h"

#define s_u (*ext_audio_uac())                                  /* the control plane's state lives in ext_audio.c (the audio task reads its gain) */
EXT_RAM_BSS_ATTR static volatile uint8_t s_alt;
EXT_RAM_BSS_ATTR static uint8_t s_buf[128];   /* the largest answer: the clock RANGE of 8 rates = 2 + 12 x 8 = 98 bytes (CFG_TUD_AUDIO_CTRL_BUF_SZ 128 matches) */
EXT_RAM_BSS_ATTR static TaskHandle_t s_pump;
/* ext_diag (cfg key, default off): a 1 Hz line "APP ext diag ..." from the pump; the counters below are written by the USB task / ISR and the pump only (diagnostics, no behaviour) */
static volatile uint32_t d_rx_n, d_rx_bytes;                  /* internal RAM: ISR-written */
EXT_RAM_BSS_ATTR static uint8_t s_tmp[1536];  /* the pump's chunk: a multiple of 24, so a whole number of frames at 16 / 24 / 32 bit */



static void wake_pump(void)
{
    if (!s_pump) return;
    if (xPortInIsrContext()) { BaseType_t w = pdFALSE; vTaskNotifyGiveFromISR(s_pump, &w); portYIELD_FROM_ISR(w); }
    else xTaskNotifyGive(s_pump);
}
/* the feedback value 16.16 samples per micro-frame at the current rate: sent until the pump has its own (the first packets after an alt change) */
static void fb_nominal(void) { tud_audio_n_fb_set(0, pc_extfb_nominal(s_u.freq)); }
void ext_uac_init(const pc_extdesc_info_t *info) { pc_extuac_init(&s_u, info->rates, info->n_rates); s_alt = 0; ext_audio_set_alts(info); fb_nominal(); }   /* (no effect before the descriptor is installed; ext_uac_start and every clock SET seed it again) */
/* the cable went (esp_tinyusb DETACHED = unmount): no stream, whatever alt the computer had */
void ext_uac_bus_gone(void) { s_alt = 0; ext_audio_event(0, s_u.freq); wake_pump(); }

/* ---- class requests (TinyUSB audio_device.c): the entity and control selector from the setup packet ---- */
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    uint8_t ch = TU_U16_LOW(p_request->wValue), cs = TU_U16_HIGH(p_request->wValue), ent = TU_U16_HIGH(p_request->wIndex);
    int n = pc_extuac_get(&s_u, ent, cs, ch, p_request->bRequest, s_buf, sizeof s_buf);
    if (n < 0) return false;                                       /* stall: not a request of ours */
    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, s_buf, (uint16_t)TU_MIN((uint16_t)n, p_request->wLength));
}
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
{
    (void)rhport;
    uint8_t ch = TU_U16_LOW(p_request->wValue), cs = TU_U16_HIGH(p_request->wValue), ent = TU_U16_HIGH(p_request->wIndex);
    bool ok = pc_extuac_set(&s_u, ent, cs, ch, buf, p_request->wLength);
    if (ok && ent == PC_EXTUAC_ENT_CLOCK) { printf("APP ext uac rate=%u\n", (unsigned)s_u.freq); fb_nominal(); ext_audio_event(s_alt, s_u.freq); wake_pump(); }
    return ok;
}
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request) { (void)rhport; (void)p_request; return false; }
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) { (void)rhport; (void)p_request; (void)buf; return false; }
bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) { (void)rhport; (void)p_request; return false; }
bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) { (void)rhport; (void)p_request; (void)buf; return false; }

/* ---- alternate settings: alt 0 = no stream; 1..3 = 16 / 24 / 32 bit. The pump (below) moves the endpoint's data into the ring; ext_audio.c starts the mixer ---- */
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;
    s_alt = (uint8_t)TU_U16_LOW(p_request->wValue);
    printf("APP ext uac alt=%d\n", s_alt);
    fb_nominal();
    ext_audio_event(s_alt, s_u.freq);
    wake_pump();
    return true;
}
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport; (void)p_request;
    s_alt = 0;
    ext_audio_event(0, s_u.freq);
    wake_pump();
    return true;
}
/* the feedback is computed by the pump from the ring's fill (AUDIO_FEEDBACK_METHOD_DISABLED: TinyUSB only sends the value tud_audio_n_fb_set was given); the nominal value of the
 * rate is the first one (TinyUSB asks here when the streaming endpoints open) */
void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t *feedback_param)
{
    (void)func_id; (void)alt_itf;
    feedback_param->method = AUDIO_FEEDBACK_METHOD_DISABLED;
    feedback_param->sample_freq = s_u.freq;
    fb_nominal();
}
/* every packet of the stream lands here (the endpoint FIFO already holds it); one wake-up per 8 packets = 1 ms at high speed. No data is touched here. */
bool tud_audio_rx_done_isr(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting)
{
    static EXT_RAM_BSS_ATTR uint8_t n;     /* tinyusb task / ISR only, PSRAM */
    (void)rhport; (void)func_id; (void)ep_out; (void)cur_alt_setting;
    d_rx_n++; d_rx_bytes += n_bytes_received;
    if ((++n & 7u) == 0) wake_pump();
    return true;
}

/* ---- the pump (spec section 3): endpoint FIFO -> PSRAM ring, the feedback value, the mixer's start. Above the keys and the drive in priority (ext_prio.h), the audio task above it ---- */
static void pump_task(void *arg)
{
    (void)arg;
    uint32_t last_fb = 0;
    const bool diag = cfg_int("ext_diag", 0) != 0;
    int64_t t_diag = esp_timer_get_time();
    uint32_t g_rx_n0 = 0, g_rx_b0 = 0, g_rd = 0, g_loops = 0, g_av_max = 0, g_fill_min = 0xFFFFFFFFu, g_fill_max = 0, g_fb = 0, g_rc0 = 0, g_ov0 = 0, g_un0 = 0, g_dr0 = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2));
        ext_audio_pump();
        uint32_t fb = (uint32_t)ext_audio_frame_bytes();
        if (fb) {
            uint32_t n;
            while ((n = tud_audio_available()) >= fb) {
                n -= n % fb;
                if (n > sizeof s_tmp) n = sizeof s_tmp;
                if (ext_audio_event_pending()) { tud_audio_clear_ep_out_ff(); break; }   /* the host changed alt between the turn's start and here: its new-format bytes must not be read as the old format */
                if (diag && n + 0u > g_av_max) g_av_max = n;
                n = tud_audio_read(s_tmp, (uint16_t)n);
                if (!n) break;
                g_rd += n;
                ext_audio_feed(s_tmp, n);                 /* a full ring or a refused format drops the rest: counted, never blocks */
            }
        } else tud_audio_clear_ep_out_ff();               /* no stream (alt 0, a refused rate): what the host still sends is discarded */
        uint32_t v = ext_audio_fb((uint64_t)esp_timer_get_time(), fb ? tud_audio_available() / fb : 0);
        if (v != last_fb) { tud_audio_n_fb_set(0, v); last_fb = v; }
        if (diag) {
            g_loops++; g_fb = v;
            const pc_extstream_t *st = ext_audio_stream();
            uint32_t fl = pc_extstream_fill_frames(st);
            if (fl < g_fill_min) g_fill_min = fl;
            if (fl > g_fill_max) g_fill_max = fl;
            int64_t now = esp_timer_get_time();
            if (now - t_diag >= 1000000) {
                uint32_t rn = d_rx_n, rb = d_rx_bytes, fb2 = (uint32_t)ext_audio_frame_bytes(), rc = st->rcount;
                printf("APP ext diag dt_ms=%d iso_rx/s=%lu rx_bytes/s=%lu rx_frames/s=%lu pump_read_frames/s=%lu rendered_frames/s=%lu fifo_peak_b=%lu ring_fill_frames=%lu..%lu "
                       "underruns=%lu overruns=%lu drops=%lu fb=0x%lx pump_loops=%lu\n", (int)((now - t_diag) / 1000), (unsigned long)(rn - g_rx_n0), (unsigned long)(rb - g_rx_b0),
                       (unsigned long)(fb2 ? (rb - g_rx_b0) / fb2 : 0), (unsigned long)(fb2 ? g_rd / fb2 : 0), (unsigned long)(fb2 ? (rc - g_rc0) / fb2 : 0), (unsigned long)g_av_max,
                       (unsigned long)(g_fill_min == 0xFFFFFFFFu ? 0 : g_fill_min), (unsigned long)g_fill_max, (unsigned long)(st->underruns - g_un0), (unsigned long)(st->overruns - g_ov0),
                       (unsigned long)(st->drops - g_dr0), (unsigned long)g_fb, (unsigned long)g_loops);
                g_rx_n0 = rn; g_rx_b0 = rb; g_rd = 0; g_loops = 0; g_av_max = 0; g_fill_min = 0xFFFFFFFFu; g_fill_max = 0; g_rc0 = rc; g_un0 = st->underruns; g_ov0 = st->overruns; g_dr0 = st->drops;
                t_diag = now;
            }
        }
    }
}
void ext_uac_start(void)
{
    fb_nominal();
    if (!s_pump) xTaskCreatePinnedToCore(pump_task, "uac_pump", 3072, NULL, EXT_PRIO_UAC, &s_pump, 0);
}
