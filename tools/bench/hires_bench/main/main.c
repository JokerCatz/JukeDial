// SPDX-License-Identifier: LGPL-2.1-or-later
/* hires_bench (2026-11-13), peer S31 only: which I2S TX clocks the chip makes up to 384 kHz (XTAL vs APLL) and what
 * the DMA path costs, then FLAC decode cost (flac_part.c). Nothing is wired to the board: BCK/WS leave on GPIO53/51
 * (the player's own I2S pads, bsp_pins.h:38-40; not strapping 36-40/60/61, not flash 26-32, not USB 33/34, not UART0
 * 58/59) and come straight back in through the GPIO matrix to two PCNT units (input enable on the same pad).
 * Timebase: esp_timer (systimer, from the same 40 MHz crystal as XTAL and APLL), so ppm below are divider/PLL error
 * against the crystal, not the crystal's own error. Output lines: see README.md. */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/pulse_cnt.h"
#include "esp_attr.h"
#include "esp_clk_tree.h"
#include "esp_cpu.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"
#include "esp_timer.h"
#include "hal/clk_tree_hal.h"
#include "hal/clk_tree_ll.h"
#include "hal/misc.h"
#include "soc/hp_sys_clkrst_struct.h"
#include "soc/i2s_struct.h"
#include "soc/rtc.h"
#include "hal/i2s_periph.h"
#include "esp_rom_gpio.h"

#define HB_PORT  I2S_NUM_1                 /* the player's DAC port (bsp_pins.h BSP_I2S_PORT_DAC) */
#define PIN_BCK  GPIO_NUM_53
#define PIN_WS   GPIO_NUM_51
#define PIN_DOUT GPIO_NUM_52
#define LIM      30000

void flac_part(void);                      /* flac_part.c */
void dsp_part(void);                       /* dsp_part.c: pc_dec2 / pc_int2 / pc_rsq cost per 10 ms block */
uint32_t hb_cpu_mhz(void);

static pcnt_unit_handle_t u_ws, u_bck;

static void pcnt_one(int pin, pcnt_unit_handle_t *u)
{
    pcnt_unit_config_t uc = { .low_limit = -LIM, .high_limit = LIM, .flags.accum_count = 1 };
    ESP_ERROR_CHECK(pcnt_new_unit(&uc, u));
    pcnt_chan_config_t cc = { .edge_gpio_num = pin, .level_gpio_num = -1 };
    pcnt_channel_handle_t ch;
    ESP_ERROR_CHECK(pcnt_new_channel(*u, &cc, &ch));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(ch, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(*u, LIM));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(*u, -LIM));
    ESP_ERROR_CHECK(pcnt_unit_enable(*u));       /* no glitch filter: count every edge the APB-clocked sampler sees */
    ESP_ERROR_CHECK(pcnt_unit_clear_count(*u));
    ESP_ERROR_CHECK(pcnt_unit_start(*u));
}

/* WS and BCK rising edges over `ms`; window = midpoint of the bracketing esp_timer reads */
static void measure(int ms, double *ws_hz, double *bck_hz)
{
    int w0, b0, w1, b1;
    gpio_input_enable(PIN_WS);
    gpio_input_enable(PIN_BCK);
    vTaskDelay(pdMS_TO_TICKS(20));
    int64_t a0 = esp_timer_get_time();
    pcnt_unit_get_count(u_ws, &w0);
    pcnt_unit_get_count(u_bck, &b0);
    int64_t a1 = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(ms));
    int64_t c0 = esp_timer_get_time();
    pcnt_unit_get_count(u_ws, &w1);
    pcnt_unit_get_count(u_bck, &b1);
    int64_t c1 = esp_timer_get_time();
    double dt = ((c0 + c1) - (a0 + a1)) / 2e6;
    *ws_hz = (double)(uint32_t)(w1 - w0) / dt;
    *bck_hz = (double)(uint32_t)(b1 - b0) / dt;
}

/* read back what the driver wrote into the clock registers of I2S1 TX */
typedef struct { uint32_t src, n, x, y, z, yn1, bdiv, num, den; double sclk, mdiv; uint32_t od, s0, s1, s2; } clkregs_t;
static void read_regs(clkregs_t *r)
{
    memset(r, 0, sizeof *r);
    r->src = HP_SYS_CLKRST.i2s1_tx_ctrl0.reg_i2s1_tx_clk_src_sel;
    r->n = HAL_FORCE_READ_U32_REG_FIELD(HP_SYS_CLKRST.i2s1_tx_ctrl0, reg_i2s1_tx_div_n);
    r->x = HP_SYS_CLKRST.i2s1_tx_div_ctrl0.reg_i2s1_tx_div_x;
    r->y = HP_SYS_CLKRST.i2s1_tx_div_ctrl0.reg_i2s1_tx_div_y;
    r->z = HP_SYS_CLKRST.i2s1_tx_div_ctrl0.reg_i2s1_tx_div_z;
    r->yn1 = HP_SYS_CLKRST.i2s1_tx_div_ctrl0.reg_i2s1_tx_div_yn1;
    r->bdiv = I2S1.tx_conf.tx_bck_div_num + 1;
    if (r->z) {                            /* inverse of i2s_ll_tx_set_mclk(): den = z (x + 1) + y, num = yn1 ? den - z : z */
        r->den = r->z * (r->x + 1) + r->y;
        r->num = r->yn1 ? r->den - r->z : r->z;
    }
    clk_ll_apll_get_config(&r->od, &r->s0, &r->s1, &r->s2);
    /* clk_src_sel: 0 XTAL, 1 APLL, 2 RC_FAST? (i2s_ll.h:351-360 order: XTAL, APLL, ... ) -- printed raw too */
    r->sclk = r->src == 1 ? 40e6 * (((4 + r->s2) << 16) | (r->s1 << 8) | r->s0) / (double)((r->od + 2) << 17) : 40e6;
    r->mdiv = r->n + (r->den ? (double)r->num / r->den : 0);
}

static double apll_reg_hz(void)
{
    uint32_t od, s0, s1, s2;
    clk_ll_apll_get_config(&od, &s0, &s1, &s2);
    return 40e6 * (((4 + s2) << 16) | (s1 << 8) | s0) / (double)((od + 2) << 17);
}

static double apll_cal_hz(void)            /* TIMG0 calibration of APLL/200 against XTAL (rtc_time.c:31-33) */
{
    uint32_t p = rtc_clk_cal(CLK_CAL_APLL, 200 * 10000);
    /* scale checked against the APLL register value on the board: the returned period is per APLL cycle here */
    return p ? 1e6 * (double)(1 << 19) / p : 0;
}

static i2s_chan_handle_t open_tx(int port, uint32_t ndesc, uint32_t nframe)
{
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    cc.dma_desc_num = ndesc;
    cc.dma_frame_num = nframe;
    cc.auto_clear = true;
    i2s_chan_handle_t tx = NULL;
    if (i2s_new_channel(&cc, &tx, NULL) != ESP_OK) return NULL;
    return tx;
}

static esp_err_t init_std(i2s_chan_handle_t tx, i2s_clock_src_t src, uint32_t fs, uint32_t mult, bool pins)
{
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(fs),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = GPIO_NUM_NC, .bclk = pins ? PIN_BCK : GPIO_NUM_NC, .ws = pins ? PIN_WS : GPIO_NUM_NC,
                      .dout = pins ? PIN_DOUT : GPIO_NUM_NC, .din = GPIO_NUM_NC },
    };
    sc.clk_cfg.clk_src = src;
    sc.clk_cfg.mclk_multiple = (i2s_mclk_multiple_t)mult;
    return i2s_channel_init_std_mode(tx, &sc);
}

static const char *srcname(i2s_clock_src_t s) { return s == I2S_CLK_SRC_APLL ? "apll" : "xtal"; }

/* ---- 1/2/3: one clock row ---- */
static void clk_row(i2s_clock_src_t src, uint32_t fs, uint32_t mult)
{
    size_t h0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    i2s_chan_handle_t tx = open_tx(HB_PORT, 6, 480);
    if (!tx) { printf("HB clk src=%s fs=%" PRIu32 " new_channel failed\n", srcname(src), fs); return; }
    esp_err_t e = init_std(tx, src, fs, mult, true);
    size_t h1 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    double ws = 0, bck = 0;
    clkregs_t r = { 0 };
    double cal = 0;
    if (e == ESP_OK) e = i2s_channel_enable(tx);
    i2s_chan_info_t inf = { 0 };
    if (e == ESP_OK) {
        read_regs(&r);
        i2s_channel_get_info(tx, &inf);
        if (src == I2S_CLK_SRC_APLL) cal = apll_cal_hz();
        measure(2000, &ws, &bck);
        i2s_channel_disable(tx);
    }
    if (e == ESP_OK) {
        double mclk = r.sclk / r.mdiv, exp_ws = mclk / r.bdiv / 64.0;
        printf("HB clk src=%s fs=%" PRIu32 " slot=32 mclk_mult=%" PRIu32 " ret=%s ws_hz=%.2f bck_hz=%.1f mclk_hz=%" PRIu32 "/%.1f"
               " | ws_ppm=%+.1f bck_ws=%.4f exp_ws_hz=%.3f exp_ppm=%+.2f sclk_hz=%.1f mdiv=%" PRIu32 "+%" PRIu32 "/%" PRIu32 " (%s) bdiv=%" PRIu32
               " src_sel=%" PRIu32 " apll_reg=od%" PRIu32 ",sdm%" PRIu32 "/%" PRIu32 "/%" PRIu32 " apll_cal_hz=%.0f drv_sclk=%" PRIu32 " drv_mclk=%" PRIu32 " drv_bclk=%" PRIu32 " int_ram=%d\n",
               srcname(src), fs, mult, esp_err_to_name(e), ws, bck, fs * mult, mclk,
               (ws / fs - 1) * 1e6, ws > 0 ? bck / ws : 0, exp_ws, (exp_ws / fs - 1) * 1e6, r.sclk, r.n, r.num, r.den,
               r.num ? "fractional" : "integer", r.bdiv, r.src, r.od, r.s2, r.s1, r.s0, cal, inf.sclk_hz, inf.mclk_hz, inf.bclk_hz, (int)(h0 - h1));
    } else {
        printf("HB clk src=%s fs=%" PRIu32 " slot=32 mclk_mult=%" PRIu32 " ret=%s ws_hz=- bck_hz=- mclk_hz=%" PRIu32 "/-\n",
               srcname(src), fs, mult, esp_err_to_name(e), fs * mult);
    }
    i2s_del_channel(tx);
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* ---- 4: switch test ---- */
static void switch_test(void)
{
    i2s_chan_handle_t tx = open_tx(HB_PORT, 6, 480);
    esp_err_t e = init_std(tx, I2S_CLK_SRC_APLL, 44100, 256, true);
    if (e == ESP_OK) e = i2s_channel_enable(tx);
    printf("HB sw start apll fs=44100 mult=256 ret=%s\n", esp_err_to_name(e));
    if (e != ESP_OK) { i2s_del_channel(tx); return; }
    static const uint32_t seq[2] = { 192000, 44100 };
    for (int i = 0; i < 10; i++) {
        for (int k = 0; k < 2; k++) {
            int64_t t0 = esp_timer_get_time();
            esp_err_t e1 = i2s_channel_disable(tx);
            i2s_std_clk_config_t ck = I2S_STD_CLK_DEFAULT_CONFIG(seq[k]);
            ck.clk_src = I2S_CLK_SRC_APLL;
            ck.mclk_multiple = I2S_MCLK_MULTIPLE_256;
            esp_err_t e2 = i2s_channel_reconfig_std_clock(tx, &ck);
            esp_err_t e3 = i2s_channel_enable(tx);
            int64_t us = esp_timer_get_time() - t0;
            clkregs_t r;
            read_regs(&r);
            double ws, bck;
            measure(300, &ws, &bck);
            printf("HB sw i=%d to=%" PRIu32 " ret=%s/%s/%s us=%" PRId64 " ws_hz=%.1f bck_ws=%.3f apll_hz=%.1f mdiv=%" PRIu32 "+%" PRIu32 "/%" PRIu32 "\n",
                   i, seq[k], esp_err_to_name(e1), esp_err_to_name(e2), esp_err_to_name(e3), us, ws, ws > 0 ? bck / ws : 0,
                   r.sclk, r.n, r.num, r.den);
        }
    }
    i2s_channel_disable(tx);
    i2s_del_channel(tx);

    /* a second APLL user: I2S0 TX at 48000 on APLL holds the APLL (its WS routed to GPIO51 by the matrix);
     * then I2S1 asks for 192000 on APLL (no pins). Does I2S0's rate move? */
    i2s_chan_handle_t t0 = open_tx(I2S_NUM_0, 2, 240);
    esp_err_t a = init_std(t0, I2S_CLK_SRC_APLL, 48000, 256, false);
    if (a == ESP_OK) a = i2s_channel_enable(t0);
    esp_rom_gpio_connect_out_signal(PIN_WS, i2s_periph_signal[0].m_tx_ws_sig, 0, 0);
    double ws0a = 0, ws0b = 0, ws1 = 0, bck;
    double apll0 = apll_reg_hz();
    measure(1000, &ws0a, &bck);
    tx = open_tx(HB_PORT, 6, 480);
    e = init_std(tx, I2S_CLK_SRC_APLL, 192000, 256, false);
    if (e == ESP_OK) e = i2s_channel_enable(tx);
    double apll1 = apll_reg_hz();
    i2s_chan_info_t i1 = { 0 };
    i2s_channel_get_info(tx, &i1);
    measure(1000, &ws0b, &bck);                      /* still I2S0's WS on the pin */
    esp_rom_gpio_connect_out_signal(PIN_WS, i2s_periph_signal[1].m_tx_ws_sig, 0, 0);
    if (e == ESP_OK) measure(1000, &ws1, &bck);
    printf("HB sw shared i2s0 apll fs=48000 ret=%s ws0_before=%.1f apll_before=%.1f | i2s1 apll fs=192000 ret=%s drv_sclk=%" PRIu32 " drv_mclk=%" PRIu32
           " ws1=%.1f apll_after=%.1f ws0_after=%.1f\n",
           esp_err_to_name(a), ws0a, apll0, esp_err_to_name(e), i1.sclk_hz, i1.mclk_hz, ws1, apll1, ws0b);
    if (e == ESP_OK) i2s_channel_disable(tx);
    i2s_del_channel(tx);
    if (a == ESP_OK) i2s_channel_disable(t0);
    i2s_del_channel(t0);
}

/* ---- 5: internal RAM of the channel per DMA shape ---- */
static void ram_row(i2s_clock_src_t src, uint32_t fs, uint32_t mult, uint32_t nd, uint32_t nf)
{
    size_t i0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), p0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    i2s_chan_handle_t tx = open_tx(HB_PORT, nd, nf);
    size_t i1 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    esp_err_t e = tx ? init_std(tx, src, fs, mult, true) : ESP_FAIL;
    size_t i2 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), p2 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    i2s_chan_info_t inf = { 0 };
    if (tx) i2s_channel_get_info(tx, &inf);
    printf("HB ram src=%s fs=%" PRIu32 " desc=%" PRIu32 " frames=%" PRIu32 " ret=%s int_new=%d int_total=%d psram=%d buf_bytes=%" PRIu32 " buffered_ms=%.2f\n",
           srcname(src), fs, nd, nf, esp_err_to_name(e), (int)(i0 - i1), (int)(i0 - i2), (int)(p0 - p2),
           (uint32_t)inf.total_dma_buf_size, inf.total_dma_buf_size / 8.0 / fs * 1000.0);
    if (tx) i2s_del_channel(tx);
    size_t i3 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (i3 != i0) printf("HB ram note: internal after delete differs by %d B\n", (int)(i0 - i3));
}

/* ---- 6: write rate ---- */
static volatile uint32_t s_spin;
static volatile bool s_spin_run;
static void spin_task(void *a) { (void)a; for (;;) { if (s_spin_run) s_spin++; } }
static volatile uint32_t s_sent, s_ovf;
static bool IRAM_ATTR on_sent(i2s_chan_handle_t h, i2s_event_data_t *ev, void *c) { (void)h; (void)ev; (void)c; s_sent++; return false; }
static bool IRAM_ATTR on_ovf(i2s_chan_handle_t h, i2s_event_data_t *ev, void *c) { (void)h; (void)ev; (void)c; s_ovf++; return false; }

static uint32_t spin_rate(int ms)
{
    uint32_t a = s_spin;
    vTaskDelay(pdMS_TO_TICKS(ms));
    return s_spin - a;
}

typedef struct { uint32_t fs, mult; SemaphoreHandle_t done; } wr_arg_t;
static uint32_t s_base;
static void write_task(void *p)
{
    wr_arg_t *w = p;
    i2s_chan_handle_t tx = open_tx(HB_PORT, 6, 480);          /* created on core 1: the I2S ISR lands on core 1 */
    esp_err_t e = init_std(tx, I2S_CLK_SRC_APLL, w->fs, w->mult, true);
    i2s_event_callbacks_t cb = { .on_sent = on_sent, .on_send_q_ovf = on_ovf };
    if (e == ESP_OK) e = i2s_channel_register_event_callback(tx, &cb, NULL);
    size_t blk = (size_t)w->fs / 100 * 8;
    int32_t *z = heap_caps_calloc(1, blk, MALLOC_CAP_SPIRAM);  /* the player's pcm block is PSRAM too */
    if (e == ESP_OK) e = i2s_channel_enable(tx);
    if (e != ESP_OK || !z) { printf("HB wr fs=%" PRIu32 " ret=%s\n", w->fs, esp_err_to_name(e)); goto out; }
    /* settle: fill once */
    size_t got;
    for (int i = 0; i < 5; i++) i2s_channel_write(tx, z, blk, &got, 100);
    s_sent = s_ovf = 0;
    TaskStatus_t st;
    vTaskGetInfo(NULL, &st, pdFALSE, eRunning);
    uint32_t rt0 = st.ulRunTimeCounter;
    uint32_t sp0 = s_spin;
    int64_t t0 = esp_timer_get_time();
    uint32_t blocks = 0, tmo = 0, shortw = 0;
    int64_t wmax = 0;
    while (esp_timer_get_time() - t0 < 20000000) {
        int64_t a = esp_timer_get_time();
        esp_err_t r = i2s_channel_write(tx, z, blk, &got, 100);
        int64_t d = esp_timer_get_time() - a;
        if (d > wmax) wmax = d;
        if (r == ESP_ERR_TIMEOUT) tmo++;
        if (got != blk) shortw++;
        blocks++;
    }
    int64_t dt = esp_timer_get_time() - t0;
    uint32_t sp = s_spin - sp0;
    vTaskGetInfo(NULL, &st, pdFALSE, eRunning);
    uint32_t rt = st.ulRunTimeCounter - rt0;                  /* run-time counter = esp_timer us (port default) */
    double base = s_base * (dt / 2e6);                        /* spin count the window would have without I2S */
    printf("HB wr fs=%" PRIu32 " mult=%" PRIu32 " blocks=%" PRIu32 " in_s=%.3f blocks_per_s=%.2f timeouts=%" PRIu32 " short=%" PRIu32
           " write_max_us=%" PRId64 " on_sent=%" PRIu32 " (%.1f/s) underrun_ev=%" PRIu32 " writer_cpu=%.2f%% core1_lost=%.2f%%\n",
           w->fs, w->mult, blocks, dt / 1e6, blocks / (dt / 1e6), tmo, shortw, wmax, s_sent, s_sent / (dt / 1e6), s_ovf,
           100.0 * rt / dt, 100.0 * (1.0 - sp / base));
    i2s_channel_disable(tx);
out:
    free(z);
    i2s_del_channel(tx);
    xSemaphoreGive(w->done);
    vTaskDelete(NULL);
}

static void write_test(void)
{
    xTaskCreatePinnedToCore(spin_task, "spin", 2048, NULL, 1, NULL, 1);
    s_spin_run = true;
    spin_rate(200);
    s_base = spin_rate(2000);
    printf("HB wr spin baseline core1 %" PRIu32 " loops / 2 s (no I2S)\n", s_base);
    static const uint32_t fsl[2] = { 192000, 384000 }, ml[2] = { 256, 128 };
    for (int i = 0; i < 2; i++) {
        wr_arg_t w = { .fs = fsl[i], .mult = ml[i], .done = xSemaphoreCreateBinary() };
        xTaskCreatePinnedToCore(write_task, "wr", 4096, &w, 10, NULL, 1);
        xSemaphoreTake(w.done, portMAX_DELAY);
        vSemaphoreDelete(w.done);
    }
    uint32_t again = spin_rate(2000);
    printf("HB wr spin after %" PRIu32 " loops / 2 s (%.2f%% of baseline)\n", again, 100.0 * again / s_base);
    s_spin_run = false;
}

uint32_t hb_cpu_mhz(void)
{
    int64_t t0 = esp_timer_get_time();
    uint32_t c0 = esp_cpu_get_cycle_count();
    while (esp_timer_get_time() - t0 < 200000) { }
    uint32_t c1 = esp_cpu_get_cycle_count();
    int64_t t1 = esp_timer_get_time();
    return (uint32_t)((uint64_t)(c1 - c0) / (uint64_t)(t1 - t0));
}

static void clk_part(void)
{
    uint32_t apb = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_APB, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &apb);
    printf("HB info pins bck=%d ws=%d dout=%d port=I2S%d apb_hz=%" PRIu32 " (PCNT samples on APB) apll_range=%d..%d Hz\n",
           PIN_BCK, PIN_WS, PIN_DOUT, HB_PORT, apb, CLK_LL_APLL_MIN_HZ, CLK_LL_APLL_MAX_HZ);
    pcnt_one(PIN_WS, &u_ws);
    pcnt_one(PIN_BCK, &u_bck);
    /* 1: XTAL */
    static const uint32_t xr[][2] = { { 44100, 256 }, { 48000, 256 }, { 88200, 128 }, { 96000, 128 }, { 88200, 256 }, { 96000, 256 },
                                      { 176400, 128 }, { 192000, 128 }, { 176400, 64 }, { 192000, 64 }, { 352800, 64 }, { 384000, 64 } };
    for (size_t i = 0; i < sizeof xr / sizeof xr[0]; i++) clk_row(I2S_CLK_SRC_XTAL, xr[i][0], xr[i][1]);
    /* 2: APLL, multiples 64..512 */
    static const uint32_t ar[] = { 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };
    static const uint32_t am[] = { 64, 128, 256, 384, 512 };
    for (size_t i = 0; i < sizeof ar / sizeof ar[0]; i++)
        for (size_t k = 0; k < sizeof am / sizeof am[0]; k++) clk_row(I2S_CLK_SRC_APLL, ar[i], am[k]);
    /* 4 */
    switch_test();
    /* 5 */
    ram_row(I2S_CLK_SRC_XTAL, 48000, 256, 6, 480);
    ram_row(I2S_CLK_SRC_XTAL, 96000, 128, 6, 480);
    ram_row(I2S_CLK_SRC_APLL, 192000, 256, 6, 480);
    ram_row(I2S_CLK_SRC_APLL, 384000, 128, 6, 480);
    ram_row(I2S_CLK_SRC_APLL, 192000, 256, 4, 480);
    ram_row(I2S_CLK_SRC_APLL, 192000, 256, 12, 480);
    ram_row(I2S_CLK_SRC_APLL, 384000, 128, 8, 480);
    ram_row(I2S_CLK_SRC_APLL, 384000, 128, 24, 480);
    ram_row(I2S_CLK_SRC_APLL, 192000, 256, 6, 511);
    ram_row(I2S_CLK_SRC_APLL, 192000, 256, 6, 512);
    ram_row(I2S_CLK_SRC_APLL, 192000, 256, 6, 1024);
    /* 6 */
    write_test();
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("HB boot hires_bench CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=%d esp_clk_cpu_freq=%d measured=%" PRIu32 " MHz flash_mode=%s\n",
           CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, esp_clk_cpu_freq(), hb_cpu_mhz(),
#if CONFIG_ESPTOOLPY_FLASHMODE_QIO
           "QIO"
#else
           "not-QIO"
#endif
    );
    printf("HB heap internal_free=%u psram_free=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    clk_part();
    flac_part();
    dsp_part();
    printf("HB done\n");
}
