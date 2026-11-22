// SPDX-License-Identifier: LGPL-2.1-or-later
/* BSP calls the UI makes: display lock = nothing to lock, backlight and haptic remember the last value (SIM_TRACE=1 prints it),
 * the knob hook is kept for sim_knob(). Signatures come from the real bsp headers. */
#include <stdio.h>
#include <stdlib.h>
#include "bsp/bsp_display.h"
#include "bsp/bsp_haptic.h"
#include "bsp/bsp_knob.h"
#include "sim.h"

static uint8_t g_backlight = 100;
static uint8_t g_haptic;
static bsp_knob_hook_t g_hook;
static bool trace(void) { const char *e = getenv("SIM_TRACE"); return e && *e == '1'; }

/* the simulator's display exists before the firmware's boot front runs: the "start" hands it back (the picture's dark start is splash_show's own set(0)); the card is the sim's root directory (SIM_STATE sd=0: none) */
esp_err_t bsp_display_start_ex(lv_disp_t **out_disp, uint8_t backlight_pct) { (void)backlight_pct; if (out_disp) *out_disp = lv_display_get_default(); return ESP_OK; }
esp_err_t bsp_i2c_init(void) { return ESP_OK; }
void bsp_display_set_task_stack(uint32_t bytes, bool psram) { (void)bytes; (void)psram; }
esp_err_t bsp_sdcard_mount(void) { return sim_state_int("sd", 1) ? ESP_OK : ESP_FAIL; }
esp_err_t bsp_sdcard_unmount(void) { return ESP_OK; }
void bsp_display_set_rotation(int angle) { (void)angle; }   /* the simulator latches its own (sim_rotation) */
void bsp_display_set_clock_mhz(int mhz) { (void)mhz; }
void bsp_display_flush_stat(void) {}
int bsp_display_rotation(void) { return sim_rotation(); }
bool bsp_display_lock(int32_t timeout_ms) { (void)timeout_ms; return true; }
void bsp_display_unlock(void) {}
esp_err_t bsp_display_backlight_set(uint8_t percent)
{
    g_backlight = percent > 100 ? 100 : percent;
    if (trace()) printf("SIM backlight %u\n", g_backlight);
    return ESP_OK;
}
esp_err_t bsp_haptic_play(uint8_t effect_id)
{
    g_haptic = effect_id;
    if (trace()) printf("SIM haptic %u\n", g_haptic);
    return ESP_OK;
}
esp_err_t bsp_haptic_stop(void)
{
    g_haptic = 0;
    if (trace()) printf("SIM haptic stop\n");
    return ESP_OK;
}
esp_err_t bsp_haptic_set_clamp(uint8_t od_clamp) { (void)od_clamp; return ESP_OK; }

void bsp_knob_set_hook(bsp_knob_hook_t hook) { g_hook = hook; }
bsp_knob_hook_t sim_knob_hook(void) { return g_hook; }
void bsp_display_release_dma(void) {}

/* F2: the console's `dac` (bsp_audio.h): the simulator has no DAC */
#include <stdio.h>
#include "bsp/bsp_audio.h"
int bsp_audio_headphone(void) { return -1; }
esp_err_t bsp_audio_set_volume(int vol) { (void)vol; return ESP_OK; }
int bsp_audio_dac_errors(void) { return 0; }
uint8_t bsp_audio_dac_note(void) { return 0; }
bool bsp_audio_dac_fault(void) { return false; }
bool bsp_audio_dac_absent(void) { return false; }
void bsp_audio_power_down(void) {}
static int s_sim_fs;
void bsp_audio_dac_config(int hpf, int fs, int amute) { (void)hpf; (void)amute; s_sim_fs = fs; }
bool bsp_audio_dac_cfg_pending(void) { return false; }
int bsp_audio_dac_drive(void) { return s_sim_fs; }   /* the simulated chip always shows what was set */
int bsp_audio_dac_drive_set(void) { return s_sim_fs; }
void bsp_audio_set_slot_mode(int mode) { (void)mode; }
esp_err_t bsp_audio_apply_options(void) { return ESP_OK; }
bool bsp_audio_burst_active(void) { return false; }   /* the simulator has no I2S: no reservoir */
uint32_t bsp_audio_burst_wr_err(void) { return 0; }
bool bsp_audio_burst_window(uint32_t *min_ms, bool *valid) { (void)min_ms; (void)valid; return false; }
uint8_t bsp_audio_bits(void) { return 0; }   /* the sim has no channel: the status shows the settings' width */
esp_err_t bsp_audio_write24(const uint8_t *packed, size_t frames, size_t *written_frames, uint32_t timeout_ms) { (void)packed; (void)frames; (void)written_frames; (void)timeout_ms; return ESP_OK; }
void bsp_audio_dump(void) { printf("APP dac none (simulator)\n"); }

/* The tuner's microphone (bsp_mic.h): the simulator has none. A synthetic source instead (console `simtone <Hz> [cents]`, `simtone noise [amp]`, `simtone off`):
 * the samples that "arrived" are counted from the virtual clock at 16 kHz (as the PDM DMA fills), so a poll returns what has passed since the last one, at most
 * the 6 x 240 samples the board's DMA queue holds (an older backlog is dropped like an overrun). Silence (a low noise) when no tone is set. */
#include <math.h>
#include "bsp/bsp_mic.h"
static bool s_mic_on;
static int64_t s_mic_t0;        /* virtual us of the last read */
static double s_mic_hz, s_mic_ph, s_mic_noise;
static unsigned s_mic_rng = 1;
static uint32_t s_mic_rate = 16000;
void sim_mic_tone(double hz, double cents, double noise)
{
    s_mic_hz = hz > 0 ? hz * pow(2.0, cents / 1200.0) : 0;
    s_mic_noise = noise;
}
bool sim_mic_is_on(void) { return s_mic_on; }
esp_err_t bsp_mic_start(uint32_t sample_rate)
{
    if (s_mic_on) return ESP_OK;
    s_mic_on = true;
    s_mic_rate = sample_rate ? sample_rate : 16000;
    s_mic_t0 = sim_now_us();
    return ESP_OK;
}
esp_err_t bsp_mic_read(int16_t *buf, size_t samples, size_t *got, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_mic_on) return ESP_ERR_INVALID_STATE;
    int64_t now = sim_now_us();
    size_t n = (size_t)((now - s_mic_t0) * (int64_t)s_mic_rate / 1000000);
    if (n > 1440) { n = 1440; s_mic_t0 = now - 1440LL * 1000000 / s_mic_rate; }   /* an overrun: the DMA ring kept the newest 90 ms */
    if (n > samples) n = samples;
    s_mic_t0 += (int64_t)n * 1000000 / s_mic_rate;
    for (size_t i = 0; i < n; i++) {
        double y = s_mic_hz > 0 ? 6000.0 * sin(6.283185307179586 * s_mic_ph) : 0.0;
        s_mic_rng = s_mic_rng * 1664525u + 1013904223u;
        double r = (double)(s_mic_rng >> 8) / 8388608.0 - 1.0;
        y += (s_mic_noise > 0 ? s_mic_noise : 8.0) * r;
        buf[i] = (int16_t)lrint(y);
        s_mic_ph += s_mic_hz / s_mic_rate;
        if (s_mic_ph >= 1.0) s_mic_ph -= 1.0;
    }
    if (got) *got = n;
    return n ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t bsp_mic_stop(void) { s_mic_on = false; return ESP_OK; }
