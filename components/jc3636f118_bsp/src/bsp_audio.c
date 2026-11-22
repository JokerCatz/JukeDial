// SPDX-License-Identifier: LGPL-2.1-or-later
#include "esp_attr.h"
#include "bsp/bsp_audio.h"
#include "bsp/bsp_pins.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pc/pc_rate.h"
#include "pc/pc_outstat.h"
#include "pc/pc_clkmeas.h"
#include "pc/pc_burst.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#ifdef BSP_DAC_CS43131      /* the FakePod's DAC has a control bus: it is configured and muted over I2C (F2); the round board's PCM5102A only has the XSMT pin */
#include "bsp/bsp_dac.h"
#include "pc/pc_cs43131.h"
#endif

static const char *TAG = "bsp_audio";
static i2s_chan_handle_t s_tx;
static volatile uint32_t s_underruns;
static bool s_muted = true;
static uint32_t s_rate;
static uint8_t s_bits;
static EXT_RAM_BSS_ATTR uint8_t s_base_bits;      /* BTAUDIO: the width asked for by the caller (16 / 32); the FakePod slot can be 24 at some rates (dac_slot24_from) */
#ifdef BSP_DAC_CS43131
static EXT_RAM_BSS_ATTR uint8_t s_slot_mode;      /* BTAUDIO: pc_slot24_mode, FakePod only */
#endif
static uint8_t s_bits_cfg;       /* width and DMA shape of the last successful init_w: kept across deinit for bsp_audio_reinit */
static uint8_t s_desc_cfg;
static uint16_t s_frames_cfg;
static uint8_t s_src;            /* PC_CLK_XTAL / PC_CLK_APLL of the open channel's clock (hires T12) */

/* hires T12: the real read-back. The driver's bclk_hz is its own computed number; what the pins do is measured: the DMA finishes one descriptor
 * (s_frames_cfg frames) every s_frames_cfg / rate seconds whether or not anyone writes. The tx-done callback feeds pc_clkmeas (player_core, host
 * tested): it learns the ring order from the buffers and counts descriptors by ring position, so merged or lost interrupts cannot undercount.
 * One writer (the ISR) after the enable, a sequence byte instead of a lock (a reader retries over an odd or changed sequence). The state is a
 * heap struct in internal RAM (the callback gets it as user_ctx), allocated and freed with the channel: no static DIRAM beyond its pointer. */
typedef struct {
    pc_clkmeas_t m;
    uint32_t tb;                   /* the confirming window's base: the stamp of the event it starts from */
    uint32_t nb;                   /* and its descriptor count */
    uint8_t seq;                   /* odd while the ISR writes */
    uint8_t chk;                   /* background check: 0 none (or done), 1 first 500 ms window, 2 the confirming window */
    pc_gap_t gap;                  /* the start gap (pc_burst.h): underruns before the first data of a start are counted apart (heap: no static DRAM) */
} ev_t;
static ev_t *s_ev;                 /* heap, internal RAM */
static EXT_RAM_BSS_ATTR uint32_t s_start_acc;   /* start-gap underruns of the channels already gone (PSRAM) */
static void ev_free(void) { if (s_ev) { s_start_acc += s_ev->gap.start_n; free(s_ev); s_ev = NULL; } }
static void gap_data(void) { if (s_ev) pc_gap_data(&s_ev->gap); }
#define s_chk (s_ev->chk)

/* not IRAM (night3 C: the start-gap split made it 18 B longer, and the DIRAM ceiling counts IRAM): it is called from the I2S ISR, which is not IRAM-safe in this build either (see on_sent) */
static bool on_q_ovf(i2s_chan_handle_t h, i2s_event_data_t *ev, void *ctx)
{
    (void)h; (void)ev;
    ev_t *e = ctx;
    if (!pc_gap_underrun(&e->gap)) s_underruns++;                    /* armed (a start, before its first data): counted in the gap, not as a steady underrun */
    return false;
}

static bool on_sent(i2s_chan_handle_t h, i2s_event_data_t *ev, void *ctx)   /* not IRAM: the I2S ISR is not IRAM-safe in this build; init_w checks the registration, so a later CONFIG_I2S_ISR_IRAM_SAFE is logged, not silent */
{
    (void)h;
    ev_t *e = ctx;                                                   /* the channel's own counters */
    uint32_t t = (uint32_t)esp_timer_get_time();
    uint8_t q = e->seq;
    __atomic_store_n(&e->seq, (uint8_t)(q + 1), __ATOMIC_RELEASE);
    pc_clkmeas_event(&e->m, ev->dma_buf, t);
    __atomic_store_n(&e->seq, (uint8_t)(q + 2), __ATOMIC_RELEASE);
    return false;
}

static void ev_restart(uint8_t desc)    /* only while the channel is not running (before an enable): no interrupt writes, so no sequence is needed */
{
    pc_clkmeas_reset(&s_ev->m, desc, (uint32_t)esp_timer_get_time());
}

static bool ev_snap(pc_clkmeas_t *out)  /* false: no stable read in 32 tries (the writer keeps running; treated as "no verdict") */
{
    for (int i = 0; i < 32; i++) {
        uint8_t q = __atomic_load_n(&s_ev->seq, __ATOMIC_ACQUIRE);
        *out = s_ev->m;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (!(q & 1) && q == __atomic_load_n(&s_ev->seq, __ATOMIC_RELAXED)) return true;
    }
    return false;
}

/* the measured output rate (Hz) since (n_from, t_from), >= min_n descriptors; *dt = the span in us; 0 = no verdict yet */
static uint32_t ev_rate_since(uint32_t n_from, uint32_t t_from, uint32_t min_n, uint32_t *dt)
{
    pc_clkmeas_t m;
    if (!ev_snap(&m)) return 0;
    *dt = m.t1 - t_from;
    return pc_clkmeas_rate(&m, n_from, t_from, s_frames_cfg, min_n);   /* s_frames_cfg: the shape of the channel that counts (a measurement only follows a successful init) */
}

static uint32_t ev_rate(uint32_t min_n, uint32_t *dt)       /* since the reference event */
{
    pc_clkmeas_t m;
    if (!ev_snap(&m) || !m.n) return 0;
    return ev_rate_since(1, m.t0, min_n, dt);
}

static uint32_t ev_age_us(void)               /* the window's age: since its reference event (its restart while none came) or, in the confirming window, since its base */
{
    pc_clkmeas_t m;
    if (!ev_snap(&m)) return 0;
    return (uint32_t)esp_timer_get_time() - (s_chk == 2 ? s_ev->tb : m.t0);
}


/* Decode-ahead reservoir (pc_burst.h). Above 96 kHz native a FLAC frame of 16384 samples decodes in one 42-56 ms call, longer than the DMA ring: the
 * audio task's finished blocks (exactly what it hands to bsp_audio_write*) go into a 250 ms FIFO in PSRAM and a small writer task, above the audio task's
 * priority, does the blocking I2S write. Internal RAM: the writer's stack and TCB, this struct (heap, only while the mode runs); the state here is PSRAM.
 * Everything that reconfigures the channel (deinit, set_rate, reshape) closes it first and opens it again after; a mute from the audio task drops what is queued. */
typedef struct {
    pc_bq_t q;
    portMUX_TYPE mux;
    TaskHandle_t writer, producer;
    volatile bool stop, alive, busy;
    uint32_t slot_bytes, bpf, slot_frames;
    uint8_t *mem;
    uint32_t len[PC_BURST_SLOTS];
} bf_t;
static EXT_RAM_BSS_ATTR bf_t *s_bf;
static EXT_RAM_BSS_ATTR volatile uint32_t s_bf_err;       /* failed / timed-out writer writes since boot (the audio task adds it to its wr_err) */

static void bf_writer(void *arg)
{
    bf_t *b = arg;
    while (!b->stop) {
        uint32_t slot = 0, gen = 0, bytes = 0;
        bool got;
        portENTER_CRITICAL(&b->mux);
        got = pc_bq_take(&b->q, &slot, &gen);
        if (got) { bytes = b->len[slot]; b->busy = true; }
        portEXIT_CRITICAL(&b->mux);
        if (!got) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10)); continue; }
        const uint8_t *p = b->mem + (size_t)slot * b->slot_bytes;
        size_t off = 0;
        esp_err_t e = ESP_OK;
        while (off < bytes && !b->stop) {
            size_t w = 0;
            e = i2s_channel_write(s_tx, p + off, bytes - off, &w, 100);
            off += w;
            if (e != ESP_OK) break;
        }
        if (e != ESP_OK) s_bf_err++;
        portENTER_CRITICAL(&b->mux);
        pc_bq_done(&b->q, gen);
        b->busy = false;
        portEXIT_CRITICAL(&b->mux);
        if (b->producer) xTaskNotifyGive(b->producer);
    }
    b->alive = false;                                              /* the last touch of b: bf_close frees it after this */
    vTaskDelete(NULL);
}

static void bf_close(void)
{
    bf_t *b = s_bf;
    if (!b) return;
    s_bf = NULL;                                                   /* writes from now on take the direct path */
    UBaseType_t hw = uxTaskGetStackHighWaterMark(b->writer);
    b->stop = true;
    xTaskNotifyGive(b->writer);
    for (int i = 0; i < 400 && b->alive; i++) vTaskDelay(1);
    if (b->alive) { printf("APP audio burst writer did not stop: memory kept\n"); return; }   /* never free what a task may still read */
    printf("APP audio burst off writer_stack_free=%u\n", (unsigned)hw);
    heap_caps_free(b->mem);
    heap_caps_free(b);
}

static void bf_open(void)       /* after a channel is up at s_rate / s_bits; no-op below the threshold or when already open */
{
    if (s_bf || !s_tx || !pc_burst_wanted(s_rate)) return;
    const uint32_t bpf = s_bits == 32 ? 8 : s_bits == 24 ? 6 : 4;
    bf_t *b = heap_caps_calloc(1, sizeof *b, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!b) return;
    b->slot_frames = pc_burst_slot_frames(s_rate);
    b->bpf = bpf;
    b->slot_bytes = b->slot_frames * bpf;
    b->mem = heap_caps_malloc((size_t)b->slot_bytes * PC_BURST_SLOTS, MALLOC_CAP_SPIRAM);
    if (!b->mem) { printf("APP audio burst off: no PSRAM for %u bytes\n", (unsigned)(b->slot_bytes * PC_BURST_SLOTS)); heap_caps_free(b); return; }
    pc_bq_init(&b->q, PC_BURST_SLOTS);
    portMUX_INITIALIZE(&b->mux);
    b->alive = true;
    if (xTaskCreatePinnedToCore(bf_writer, "i2s_wr", 1536, b, configMAX_PRIORITIES - 1, &b->writer, 1) != pdPASS) {
        printf("APP audio burst off: no writer task\n");
        heap_caps_free(b->mem);
        heap_caps_free(b);
        return;
    }
    s_bf = b;
    printf("APP audio burst on rate=%lu depth_ms=%d bytes=%u\n", (unsigned long)s_rate, PC_BURST_MS, (unsigned)(b->slot_bytes * PC_BURST_SLOTS));
}

static void bf_flush(void)      /* only the producer (the audio task) drops the queue: another task's mute leaves it to the audio task, which closes or flushes at its next step */
{
    bf_t *b = s_bf;
    if (!b || xTaskGetCurrentTaskHandle() != b->producer) return;
    portENTER_CRITICAL(&b->mux);
    const uint32_t dropped = pc_bq_fill(&b->q);
    pc_bq_flush(&b->q);
    if (s_ev) pc_gap_arm(&s_ev->gap);                              /* the ring runs dry until the next data: a start (pause, track change) */
    portEXIT_CRITICAL(&b->mux);
    for (int i = 0; i < 100 && b->busy; i++) vTaskDelay(1);        /* a block being written to the ring finishes (ms) */
    if (dropped) printf("APP audio burst flush dropped_ms=%lu\n", (unsigned long)dropped * 10);   /* on a user action (stop, pause, track change), never per block */
}

static esp_err_t bf_push(const void *data, size_t frames, size_t *written)
{
    bf_t *b = s_bf;
    b->producer = xTaskGetCurrentTaskHandle();
    const int64_t t_end = esp_timer_get_time() + 100000;
    size_t done = 0;
    while (done < frames) {
        size_t n = frames - done;
        if (n > b->slot_frames) n = b->slot_frames;
        uint32_t slot;
        for (;;) {
            bool room;
            portENTER_CRITICAL(&b->mux);
            room = pc_bq_space(&b->q) > 0;
            slot = pc_bq_wr_slot(&b->q);
            portEXIT_CRITICAL(&b->mux);
            if (room) break;
            if (b->stop || esp_timer_get_time() >= t_end) goto out;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
        }
        memcpy(b->mem + (size_t)slot * b->slot_bytes, (const uint8_t *)data + done * b->bpf, n * b->bpf);
        b->len[slot] = (uint32_t)(n * b->bpf);
        portENTER_CRITICAL(&b->mux);
        pc_bq_commit(&b->q);
        portEXIT_CRITICAL(&b->mux);
        xTaskNotifyGive(b->writer);
        gap_data();
        done += n;
    }
out:
    *written = done;
    return done == frames ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool bsp_audio_burst_active(void) { return s_bf != NULL; }
uint32_t bsp_audio_burst_wr_err(void) { return s_bf_err; }
bool bsp_audio_burst_window(uint32_t *min_ms, bool *valid)
{
    bf_t *b = s_bf;
    if (!b) return false;
    portENTER_CRITICAL(&b->mux);
    *min_ms = pc_bq_min_ms(&b->q, valid);
    pc_bq_window_reset(&b->q);
    portEXIT_CRITICAL(&b->mux);
    return true;
}

esp_err_t bsp_audio_mute(bool mute)
{
    s_muted = mute;
    if (mute) bf_flush();                                           /* burst mode: what is queued is not played after a mute */
#ifdef BSP_DAC_CS43131
    return bsp_dac_mute(mute);      /* unmutes only while the DAC runs (a stopped or failed one stays silent) */
#else
    return gpio_set_level(BSP_PIN_DAC_XSMT, mute ? 0 : 1);
#endif
}

#ifdef BSP_DAC_CS43131
int bsp_audio_headphone(void) { return bsp_dac_headphone(); }
esp_err_t bsp_audio_set_volume(int vol) { return bsp_dac_set_volume(vol); }
int bsp_audio_dac_errors(void) { return bsp_dac_serial_errors(); }
uint8_t bsp_audio_dac_note(void) { return bsp_dac_take_note(); }
bool bsp_audio_dac_fault(void) { return bsp_dac_fault(); }
bool bsp_audio_dac_absent(void) { return bsp_dac_absent(); }
void bsp_audio_power_down(void) { bsp_dac_shutdown(); }
void bsp_audio_dac_config(int hpf, int fs, int amute) { bsp_dac_config(hpf, fs, amute); }
void bsp_audio_dump(void) { bsp_dac_dump(); }
int bsp_audio_dac_drive(void) { return bsp_dac_drive(); }
bool bsp_audio_dac_cfg_pending(void) { return bsp_dac_cfg_pending(); }
int bsp_audio_dac_drive_set(void) { return bsp_dac_drive_set(); }
#else
int bsp_audio_headphone(void) { return -1; }
esp_err_t bsp_audio_set_volume(int vol) { (void)vol; return ESP_OK; }
int bsp_audio_dac_errors(void) { return 0; }
uint8_t bsp_audio_dac_note(void) { return 0; }
bool bsp_audio_dac_fault(void) { return false; }
bool bsp_audio_dac_absent(void) { return false; }
void bsp_audio_power_down(void) {}
void bsp_audio_dac_config(int hpf, int fs, int amute) { (void)hpf; (void)fs; (void)amute; }
void bsp_audio_dump(void) { printf("APP dac none (PCM5102A, no control bus)\n"); }
int bsp_audio_dac_drive(void) { return -1; }
bool bsp_audio_dac_cfg_pending(void) { return false; }
int bsp_audio_dac_drive_set(void) { return -1; }
#endif

bool bsp_audio_is_init(void) { return s_tx != NULL; }
uint32_t bsp_audio_underruns(void) { return s_underruns; }
uint32_t bsp_audio_start_underruns(void) { return s_start_acc + (s_ev ? s_ev->gap.start_n : 0); }
void bsp_audio_arm_start(void) { if (s_ev) pc_gap_arm(&s_ev->gap); }
uint32_t bsp_audio_rate(void) { return s_tx ? s_rate : 0; }
uint8_t bsp_audio_bits(void) { return s_tx ? s_bits : 0; }
/* BTAUDIO: the slot width a rate gets: the caller's width, or 24 on the FakePod from the rate the setting names (CS43131 ASP 24-bit slots,
 * BCLK 48 x Fs: 18.432 MHz at 384k, 25 % under the 24.576 MHz slave limit). The round board never changes the width. */
static uint8_t want_bits(uint32_t rate)
{
#ifdef BSP_DAC_CS43131
    return pc_slot_bits(s_slot_mode, rate, s_base_bits ? s_base_bits : 32);
#else
    (void)rate;
    return s_base_bits ? s_base_bits : 32;
#endif
}
void bsp_audio_set_slot_mode(int mode)
{
#ifdef BSP_DAC_CS43131
    s_slot_mode = (uint8_t)pc_slot24_mode(mode);
#else
    (void)mode;
#endif
}
uint32_t bsp_audio_bclk(void)     /* the driver keeps the BCLK of the last clock set-up: read, not stored (no internal RAM) */
{
    i2s_chan_info_t inf;
    return s_tx && i2s_channel_get_info(s_tx, &inf) == ESP_OK ? inf.bclk_hz : 0;
}

/* The clock of a rate comes from the clock table (pc_i2s_clk_for, H19): source and MCLK multiple. <= 48k: XTAL x 256 (today's setup, the
 * driver's default on this chip); 88.2/96k: XTAL x 128; 176.4k and up: APLL x 128 (hires T12). This file is the APLL's only owner (the
 * driver's clk_src choice below; tools/check/apll_guard.sh keeps every other file away from it). */
static esp_err_t clk_of(uint32_t rate, uint8_t bits, i2s_std_clk_config_t *clk)
{
    pc_i2s_clk_t row;
    if (!pc_i2s_clk_for(rate, &row)) return ESP_ERR_NOT_SUPPORTED;
    i2s_std_clk_config_t c = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    c.clk_src = row.src == PC_CLK_APLL ? I2S_CLK_SRC_APLL : I2S_CLK_SRC_XTAL;
    c.mclk_multiple = (i2s_mclk_multiple_t)(bits == 24 ? row.mclk_mult * 3 / 2 : row.mclk_mult);   /* a 24-bit slot needs a multiple of 3 (i2s_std.c): 256 -> 384, 128 -> 192; BCLK = 48 x Fs divides it */
    *clk = c;
    return ESP_OK;
}

/* after an enable: the BCLK the driver set up must give the wanted rate (pc_i2s_rate_ok) */
static esp_err_t read_back(uint32_t rate, uint8_t bits)
{
    uint32_t bclk = bsp_audio_bclk();
    if (pc_i2s_rate_ok(rate, bclk, bits)) return ESP_OK;
    ESP_LOGE(TAG, "rate %lu: bclk %lu read back", (unsigned long)rate, (unsigned long)bclk);
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t bsp_audio_init(uint32_t sample_rate) { return bsp_audio_init_w(sample_rate, 6, 480, 32); }
esp_err_t bsp_audio_init_ex(uint32_t sample_rate, uint32_t desc_num, uint32_t frame_num) { return bsp_audio_init_w(sample_rate, desc_num, frame_num, 16); }

esp_err_t bsp_audio_init_w(uint32_t sample_rate, uint32_t desc_num, uint32_t frame_num, uint8_t bits)
{
    if (s_tx) return ESP_OK;
    #ifdef BSP_DAC_CS43131
    ESP_RETURN_ON_FALSE(bits == 16 || bits == 24 || bits == 32, ESP_ERR_INVALID_ARG, TAG, "bits");
#else
    ESP_RETURN_ON_FALSE(bits == 16 || bits == 32, ESP_ERR_INVALID_ARG, TAG, "bits");
#endif
    if (bits != 24) s_base_bits = bits;
#ifdef BSP_DAC_CS43131
    ESP_RETURN_ON_ERROR(bsp_dac_init(), TAG, "dac");   /* reset, id, crystal, fixed level; muted. Idempotent */
    ESP_RETURN_ON_FALSE(pc_cs_rate_code(sample_rate) >= 0, ESP_ERR_NOT_SUPPORTED, TAG, "the DAC has no code for %lu", (unsigned long)sample_rate);
#else
    if (!s_bits_cfg) {   /* once (no init has ever succeeded yet; deinit keeps the pin an output): a second gpio_config on the pin made the driver print "conflict found for GPIO[50]" on every I2S return */
        const gpio_config_t xc = { .pin_bit_mask = 1ULL << BSP_PIN_DAC_XSMT, .mode = GPIO_MODE_OUTPUT };
        ESP_RETURN_ON_ERROR(gpio_config(&xc), TAG, "xsmt");
    }
#endif
    bsp_audio_mute(true);

    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_PORT_DAC, I2S_ROLE_MASTER);
    cc.dma_desc_num = desc_num;
    cc.dma_frame_num = frame_num;
    cc.auto_clear = true; /* underrun plays silence, not stale data */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&cc, &s_tx, NULL), TAG, "new channel");
    s_ev = heap_caps_calloc(1, sizeof *s_ev, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);   /* the read-back counters of this channel (hires T12) */
    if (!s_ev) { i2s_del_channel(s_tx); s_tx = NULL; return ESP_ERR_NO_MEM; }

    i2s_std_config_t sc = {
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits == 32 ? I2S_DATA_BIT_WIDTH_32BIT : bits == 24 ? I2S_DATA_BIT_WIDTH_24BIT : I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC, .bclk = BSP_PIN_I2S_BCK, .ws = BSP_PIN_I2S_WS,
            .dout = BSP_PIN_I2S_DOUT, .din = GPIO_NUM_NC,
        },
    };
    esp_err_t e = clk_of(sample_rate, bits, &sc.clk_cfg);
    if (e == ESP_OK) e = i2s_channel_init_std_mode(s_tx, &sc);
    if (e == ESP_OK) {
        i2s_event_callbacks_t cbs = { .on_send_q_ovf = on_q_ovf, .on_sent = on_sent };
        e = i2s_channel_register_event_callback(s_tx, &cbs, s_ev);
        if (e != ESP_OK) ESP_LOGE(TAG, "event callbacks: %s (no underrun count, no clock measurement)", esp_err_to_name(e));   /* e.g. CONFIG_I2S_ISR_IRAM_SAFE needs on_sent in IRAM: said loudly, the channel still plays */
        ev_restart((uint8_t)desc_num);
        pc_gap_arm(&s_ev->gap);                                      /* the DMA runs from the enable; the first data comes later */
        e = i2s_channel_enable(s_tx);
    }
    if (e == ESP_OK) e = read_back(sample_rate, bits);
#ifdef BSP_DAC_CS43131
    if (e == ESP_OK) { e = bsp_dac_start(sample_rate, bits); if (e != ESP_OK) i2s_channel_disable(s_tx); }   /* the clocks run: serial port and amp up, still muted; a refused rate leaves no channel */
#endif
    if (e != ESP_OK) {
        if (e == ESP_ERR_INVALID_RESPONSE) i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
        ev_free();
        return e;
    }
    { pc_i2s_clk_t row; s_src = pc_i2s_clk_for(sample_rate, &row) ? row.src : PC_CLK_XTAL; }
    s_chk = sample_rate > 48000 ? 1 : 0;                           /* the real read-back of the first 500 ms (bsp_audio_clock_poll); <= 48k keeps today's setup unmeasured */
    s_rate = sample_rate;
    s_bits = bits;
    s_bits_cfg = bits;
    s_desc_cfg = (uint8_t)desc_num;
    s_frames_cfg = (uint16_t)frame_num;   /* s_underruns is not reset: it counts from boot, callers take deltas */
    bf_open();                                                       /* > 96 kHz: the decode-ahead reservoir (a no-op otherwise) */
    return ESP_OK;
}

/* hires T8b. The DAC's PLL locks on new clocks for 20 ms while the DMA already runs. That time used to be an idle delay: the ring played on with
 * nobody writing, so at 96k (6 x 480 = 30 ms) only ~10 ms were left when the caller's first block came, and the first FLAC frame of a new source
 * (an 8192-frame decode, ~15 ms) underran the ring 1-2 times at every change into 96k (Batch B M-B2). Now the 20 ms are spent writing silence
 * (a 512 B stack buffer, nothing static): the call takes as long as before, and the ring is full when it returns, as in steady play. */
static void settle_silence(uint32_t rate)
{
    uint8_t z[512] = { 0 };
    size_t left = (size_t)(rate / 50) * (s_bits == 32 ? 8 : s_bits == 24 ? 6 : 4);   /* 20 ms of frames */
    int64_t t0 = esp_timer_get_time();
    while (left) {
        size_t w = 0;
        if (i2s_channel_write(s_tx, z, left < sizeof z ? left : sizeof z, &w, 100) != ESP_OK || !w) break;
        left -= w;
    }
    int64_t ms = 20 - (esp_timer_get_time() - t0) / 1000;
    if (ms > 0) vTaskDelay(pdMS_TO_TICKS(ms));                    /* a failed write: the PLL still gets its 20 ms */
}

/* hires T12. The settle of an APLL rate is also the measurement: silence is written (the DAC is muted, the ring stays full) until CHK_SYNC_MS
 * have passed since the enable, then the counted descriptors give the real output rate. A clock that is not the rate fails here, muted: nothing
 * is ever played at it. A later block is then not needed for the proof (the DMA runs by itself). */
#define CHK_SYNC_MS 50
#define CHK_SYNC_MAX_MS 150
#define CHK_BG_MS 500              /* the background window: the first blocks after the unmute (bsp_audio_clock_poll) */
#define CHK_CONFIRM_MS 250
static esp_err_t settle_and_measure(uint32_t rate, bool measure)
{
    settle_silence(rate);
    if (!measure) return ESP_OK;
    uint8_t z[512] = { 0 };
    /* 50 ms, then (muted, silence written) up to CHK_SYNC_MAX_MS: a verdict only when the long window also disagrees (the tolerance of pc_clkmeas_close grows
     * as the window shortens: a late interrupt costs a few ms of stamp, a wrong divider is far outside) */
    for (;;) {
        uint32_t age = ev_age_us();
        if (age >= CHK_SYNC_MS * 1000u) {
            uint32_t dt = 0, got = ev_rate(8, &dt);
            if (!got || pc_clkmeas_close(rate, got, dt)) return ESP_OK;   /* too few events: no verdict (the 500 ms window carries on in the background, armed by the enable) */
            if (age >= CHK_SYNC_MAX_MS * 1000u) {
                printf("APP audio clk measured rate=%lu got=%lu (settle)\n", (unsigned long)rate, (unsigned long)got);
                s_chk = 0;
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
        size_t w = 0;
        if (i2s_channel_write(s_tx, z, sizeof z, &w, 100) != ESP_OK || !w) vTaskDelay(1);
    }
}

esp_err_t bsp_audio_clock_poll(void)
{
    if (!s_tx || !s_ev || !s_chk) return ESP_OK;
    if (ev_age_us() < (s_chk == 1 ? CHK_BG_MS : CHK_CONFIRM_MS) * 1000u) return ESP_OK;
    uint32_t dt = 0, got = s_chk == 1 ? ev_rate(8, &dt) : ev_rate_since(s_ev->nb, s_ev->tb, 8, &dt);
    if (!got || pc_clkmeas_close(s_rate, got, dt)) {              /* too few events is not a verdict (the channel idles); else it is right */
        if (got) printf("APP audio clk measured rate=%lu got=%lu\n", (unsigned long)s_rate, (unsigned long)got);
        s_chk = 0;
        return ESP_OK;
    }
    if (s_chk == 1) {                                             /* one confirming window, from the events after this one, before a verdict */
        pc_clkmeas_t m;
        if (!ev_snap(&m)) return ESP_OK;
        s_ev->nb = m.n;
        s_ev->tb = m.t1;
        s_chk = 2;
        return ESP_OK;
    }
    printf("APP audio clk measured rate=%lu got=%lu (background)\n", (unsigned long)s_rate, (unsigned long)got);
    s_chk = 0;
    s_rate = 0;                                                    /* the next call (any rate) reconfigures */
    bsp_audio_mute(true);
    return ESP_ERR_INVALID_RESPONSE;
}

static const char *src_name(uint8_t src) { return src == PC_CLK_APLL ? "apll" : "xtal"; }
static void log_clk(uint32_t rate, const pc_i2s_clk_t *row, int64_t t0)   /* once per clock change, never per block */
{
    printf("APP audio clk rate=%lu src=%s mclk=%u dma=%lux%lu bclk=%lu us=%lld\n", (unsigned long)rate, src_name(row->src), (unsigned)row->mclk_mult,
           (unsigned long)s_desc_cfg, (unsigned long)s_frames_cfg, (unsigned long)bsp_audio_bclk(), (long long)(esp_timer_get_time() - t0));
}

/* Before an enable on a reconfigured channel: the ring still holds the last blocks of the old clock (disable/enable keep the buffers), which the
 * DMA would replay at the new one. Zeroed (preload, the channel is disabled), nothing of the old rate is clocked out at the new rate. A preload
 * error is logged and not a switch failure (it is no clock error; the DAC is muted through the settle as before T8b). */
static void ring_zero(void)
{
    uint8_t z[512] = { 0 };
    size_t got;
    do {
        got = 0;
        esp_err_t e = i2s_channel_preload_data(s_tx, z, sizeof z, &got);
        if (e != ESP_OK) { ESP_LOGW(TAG, "ring zero: %s", esp_err_to_name(e)); return; }
    } while (got);
}

/* Another DMA shape or another clock source (the clock table row's, hires T8b / T12: 8 x 480 at 88.2/96k, 6 x 480 at <= 48k, 8 x 511 on the APLL):
 * the channel is deleted and made again with the row's shape (Task 3's deinit / init_w; init reads the clock back), then the settle in silence (an
 * APLL rate: with the measurement); the mute as it was. The channel is always made while no other exists: the old ring is freed before the new one
 * is taken, so the internal peak never adds (+9,664 B of 8 x 511 over 6 x 480 only while above 96k). On a failure there is no channel (the caller
 * treats it as released), except a measurement mismatch: the channel stays, muted, s_rate = 0.
 * A shape that cannot be allocated above 48k falls back to 6 x 480 at the SAME rate (no silence, no other rate: APP audio dma fallback). */
static esp_err_t init_shape(uint32_t rate, uint32_t desc, uint32_t frames, uint8_t bits)   /* init_w with the same-rate fallback (shared by reshape and reinit) */
{
    esp_err_t e = bsp_audio_init_w(rate, desc, frames, bits);          /* leaves it muted */
    if (e != ESP_OK && e != ESP_ERR_INVALID_RESPONSE && rate > 48000 && (desc != 6 || frames != 480)) {   /* hook for M-B4: the shape of a tier is chosen in pc_i2s_clk_for; a shorter ring is the last resort, never a lower rate */
        printf("APP audio dma fallback rate=%lu dma=6x480\n", (unsigned long)rate);
        e = bsp_audio_init_w(rate, 6, 480, bits);
    }
    return e;
}

static esp_err_t reshape(uint32_t rate, const pc_i2s_clk_t *row, uint8_t bits)   /* bits: the slot width of the new channel (BTAUDIO: it may change here) */
{
    int64_t t0 = esp_timer_get_time();
    bool was_muted = s_muted;
    bsp_audio_deinit();                                            /* mutes */
    esp_err_t e = init_shape(rate, row->desc, row->frames, bits);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "reshape %lu (%ux%u): %s", (unsigned long)rate, (unsigned)row->desc, (unsigned)row->frames, esp_err_to_name(e));
        return e;
    }
    e = settle_and_measure(rate, row->src == PC_CLK_APLL);
    if (e != ESP_OK) { bf_close(); s_rate = 0; return e; }         /* stays muted: never heard at the wrong rate */
    if (!was_muted) bsp_audio_mute(false);
    printf("APP audio dma rate=%lu dma=%lux%lu us=%lld\n", (unsigned long)rate, (unsigned long)s_desc_cfg, (unsigned long)s_frames_cfg,
           (long long)(esp_timer_get_time() - t0));   /* once per shape change (a rate change), never per block */
    log_clk(rate, row, t0);
    return ESP_OK;
}

esp_err_t bsp_audio_set_rate(uint32_t sample_rate)
{
    ESP_RETURN_ON_FALSE(s_tx, ESP_ERR_INVALID_STATE, TAG, "not init");
    const uint8_t wb = want_bits(sample_rate);                     /* BTAUDIO: the slot width this rate gets (24 from the dac_slot24_from rate on the FakePod) */
    if (sample_rate == s_rate && wb == s_bits) return ESP_OK;
    i2s_std_clk_config_t clk;
    ESP_RETURN_ON_ERROR(clk_of(sample_rate, wb, &clk), TAG, "no clock row for %lu", (unsigned long)sample_rate);   /* nothing touched */
#ifdef BSP_DAC_CS43131
    ESP_RETURN_ON_FALSE(pc_cs_rate_code(sample_rate) >= 0, ESP_ERR_NOT_SUPPORTED, TAG, "the DAC has no code for %lu", (unsigned long)sample_rate);   /* below 32 kHz (no ASP_SPRATE code): nothing touched */
#endif
    pc_i2s_clk_t row;
    pc_i2s_clk_for(sample_rate, &row);                            /* clk_of found it */
    bool follows = pc_i2s_shape_follows(s_desc_cfg, s_frames_cfg);
    if (wb != s_bits) return reshape(sample_rate, &row, wb);      /* another slot width: the channel is made again (muted, settled, as a shape change) */
    if (follows && (row.desc != s_desc_cfg || row.frames != s_frames_cfg || row.src != s_src)) return reshape(sample_rate, &row, s_bits);
    if (row.src != s_src) return ESP_ERR_NOT_SUPPORTED;           /* the caller's own shape (the instrument's 3 x 64) never moves to the APLL: nothing touched */
    int64_t t0 = esp_timer_get_time();
    bool was_muted = s_muted;
    bsp_audio_mute(true);
    bf_close();                                                    /* the clock changes under the channel: no writer, nothing queued */
    vTaskDelay(pdMS_TO_TICKS(5));
    s_chk = 0;
#ifdef BSP_DAC_CS43131
    bsp_dac_stop();                                                /* amp and serial port down while the old clocks still run (the pop-free order) */
#endif
    esp_err_t e = i2s_channel_disable(s_tx), e2 = ESP_OK;
    if (e == ESP_OK) {
        e = i2s_channel_reconfig_std_clock(s_tx, &clk);
        if (e == ESP_OK) ring_zero();
        ev_restart(s_desc_cfg);
        if (s_ev) pc_gap_arm(&s_ev->gap);
        e2 = i2s_channel_enable(s_tx);
        if (e == ESP_OK && e2 != ESP_OK) e2 = i2s_channel_enable(s_tx);   /* the clock is reconfigured: try once more before giving up */
        if (e != ESP_OK || e2 != ESP_OK) s_rate = 0;   /* past the disable: the clock may be half set or the channel off; remember nothing, the next call (any rate, the old one too) reprograms */
        if (e == ESP_OK && e2 == ESP_OK && read_back(sample_rate, s_bits) != ESP_OK) {
            s_rate = 0;                                 /* running at a clock that is not the rate: the next call (any rate) reconfigures */
            return ESP_ERR_INVALID_RESPONSE;            /* stays muted: never heard at the wrong rate */
        }
#ifdef BSP_DAC_CS43131
        if (e == ESP_OK && e2 == ESP_OK) {              /* the new clocks run: the DAC's serial port and amp come up again at this rate, still muted */
            esp_err_t d = bsp_dac_start(sample_rate, s_bits);
            if (d != ESP_OK) { s_rate = 0; return d; }
        }
#endif
    }
    if (e != ESP_OK || e2 != ESP_OK) {              /* give the mute back as it was (s_rate: 0 after any failure past the disable, unchanged when the disable failed) */
        if (!was_muted) bsp_audio_mute(false);
        ESP_LOGE(TAG, "set rate %lu: %s", (unsigned long)sample_rate, esp_err_to_name(e != ESP_OK ? e : e2));
        return e != ESP_OK ? e : e2;
    }
    s_rate = sample_rate;
    s_chk = sample_rate > 48000 ? 1 : 0;                           /* armed at the enable */
    if (settle_and_measure(sample_rate, row.src == PC_CLK_APLL) != ESP_OK) { s_rate = 0; return ESP_ERR_INVALID_RESPONSE; }   /* PCM5102A PLL lock + the real rate; muted on a mismatch */
    bf_open();                                                     /* the rate may now be above 96 kHz */
    if (!was_muted) bsp_audio_mute(false);
    log_clk(sample_rate, &row, t0);
    return ESP_OK;
}

/* FIX4 I1 + I2: a changed wired option applies NOW, playing or idle, never at some later rate change. The slot width the setting gives for the rate that runs is made
 * (set_rate reshapes: muted, settled, as any width change); a changed DAC option (dac_hpf / dac_fs / dac_amute) resets and boots the chip again with the output muted, the volume
 * written and read back before the unmute (chip_init_locked, bsp_dac_start). The mute is given back as it was. No channel (released, or the first start not yet made): the next open applies both. */
esp_err_t bsp_audio_apply_options(void)
{
    if (!s_tx || !s_rate) return ESP_OK;
    if (want_bits(s_rate) != s_bits) return bsp_audio_set_rate(s_rate);   /* the reshape starts the DAC again too (a pending option is applied there) */
#ifdef BSP_DAC_CS43131
    if (!bsp_dac_cfg_pending()) return ESP_OK;
    const bool was_muted = s_muted;
    bsp_audio_mute(true);
    vTaskDelay(pdMS_TO_TICKS(5));
    bsp_dac_stop();                                                /* the pop-free power-down while the clocks still run */
    esp_err_t e = bsp_dac_start(s_rate, s_bits);                   /* resets and boots the chip with the new options, volume read back, still muted */
    if (e != ESP_OK) return e;                                     /* stays muted: a part that did not come up is never heard */
    if (!was_muted) bsp_audio_mute(false);
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

esp_err_t bsp_audio_write(const int16_t *stereo, size_t frames, size_t *written_frames, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_tx && s_bits == 16, ESP_ERR_INVALID_STATE, TAG, "not a 16-bit channel");
    if (s_bf) return bf_push(stereo, frames, written_frames ? written_frames : &(size_t){0});
    size_t bytes = 0;
    esp_err_t e = i2s_channel_write(s_tx, stereo, frames * 4, &bytes, timeout_ms);
    if (written_frames) *written_frames = bytes / 4;
    if (bytes) gap_data();
    return e;
}

esp_err_t bsp_audio_write32(const int32_t *stereo, size_t frames, size_t *written_frames, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_tx && s_bits == 32, ESP_ERR_INVALID_STATE, TAG, "not a 32-bit channel");
    if (s_bf) return bf_push(stereo, frames, written_frames ? written_frames : &(size_t){0});
    size_t bytes = 0;
    esp_err_t e = i2s_channel_write(s_tx, stereo, frames * 8, &bytes, timeout_ms);
    if (written_frames) *written_frames = bytes / 8;
    if (bytes) gap_data();
    return e;
}

/* 24-bit slot channel (FakePod, BTAUDIO): packed little-endian 3-byte samples, 6 bytes per stereo frame (the driver's 24-bit buffer on this chip family) */
esp_err_t bsp_audio_write24(const uint8_t *packed, size_t frames, size_t *written_frames, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_tx && s_bits == 24, ESP_ERR_INVALID_STATE, TAG, "not a 24-bit channel");
    if (s_bf) return bf_push(packed, frames, written_frames ? written_frames : &(size_t){0});
    size_t bytes = 0;
    esp_err_t e = i2s_channel_write(s_tx, packed, frames * 6, &bytes, timeout_ms);
    if (written_frames) *written_frames = bytes / 6;
    if (bytes) gap_data();
    return e;
}

esp_err_t bsp_audio_reinit(uint32_t rate)
{
    if (!s_bits_cfg) return ESP_ERR_INVALID_STATE;       /* no init ever succeeded: nothing to repeat */
    pc_i2s_clk_t row;
    bool fol = pc_i2s_shape_follows(s_desc_cfg, s_frames_cfg) && pc_i2s_clk_for(rate, &row);   /* a table-shaped channel comes back with the rate's row shape (T8b) */
    if (s_tx) {
        if (s_rate == rate && s_bits == want_bits(rate) && (!fol || (row.desc == s_desc_cfg && row.frames == s_frames_cfg))) return ESP_OK;
        bsp_audio_deinit();                              /* another rate, width or shape is up: never hand it back as if it matched */
    }
    /* hires T12 fix 2: the same road as reshape (the fallback shape at the same rate; an APLL rate is measured, muted, before anything is played at it) */
    esp_err_t e = init_shape(rate, fol ? row.desc : s_desc_cfg, fol ? row.frames : s_frames_cfg, want_bits(rate));
    if (e == ESP_OK && pc_i2s_clk_for(rate, &row) && row.src == PC_CLK_APLL) {
        e = settle_and_measure(rate, true);              /* silence in the ring, the output still muted: the caller unmutes after its first block */
        if (e != ESP_OK) s_rate = 0;                     /* the channel stays (muted); the next reinit or set_rate reprograms */
    }
    return e;
}

esp_err_t bsp_audio_deinit(void)
{
    if (!s_tx) return ESP_OK;
    bsp_audio_mute(true);
    bf_close();                                                    /* the writer is gone before the channel is */
#ifdef BSP_DAC_CS43131
    bsp_dac_stop();                                                /* amp and serial port down while the clocks still run */
#endif
    i2s_channel_disable(s_tx);
    esp_err_t e = i2s_del_channel(s_tx);
    s_tx = NULL;
    s_rate = 0;
    s_bits = 0;
    ev_free();                                                     /* the channel (and its interrupt) is gone */
    return e;
}
