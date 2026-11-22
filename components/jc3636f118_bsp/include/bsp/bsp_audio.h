// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* I2S std TX on BSP_I2S_PORT_DAC -> PCM5102A, stereo Philips, no MCLK; 16-bit or 32-bit slots (24-bit samples in the top of a 32-bit slot).
 * XSMT (soft mute) is driven low whenever clocks change. */
/* The wired ceiling a board gets when dac_max_rate is not stored (hires T6, H26): the highest tier whose board gates passed on this board's DAC.
 * The controller raises it at a batch close; pc_dacmax_norm brings it to a tier the build offers. */
#if defined(BSP_FAKEPOD_BOARD) || defined(SIM_FP)   /* BSP_FAKEPOD_BOARD: the fakepod_bsp component's PUBLIC definition */
#define BSP_DAC_RATE_DEFAULT 384000   /* DACFS (FakePod): no halving by default; the user lowers it (the halve threshold, dac_max_rate) */
#else
#define BSP_DAC_RATE_DEFAULT 48000
#endif
esp_err_t bsp_audio_init(uint32_t sample_rate);     /* leaves output muted; 6 x 480-frame DMA buffers, 32-bit slots */
/* same with other DMA buffers, 16-bit slots (instrument app: 3 x 64 frames = 4 ms at 48 kHz) */
esp_err_t bsp_audio_init_ex(uint32_t sample_rate, uint32_t desc_num, uint32_t frame_num);
/* both of the above in one: bits = 16 or 32 (the slot width) */
esp_err_t bsp_audio_init_w(uint32_t sample_rate, uint32_t desc_num, uint32_t frame_num, uint8_t bits);
/* mute -> disable -> reconfig clock -> zero the ring -> enable -> read back -> 20 ms settle written as silence (the ring is full on return) ->
 * unmute (if it was unmuted). The clock (source, MCLK multiple) comes from the clock table row (pc_i2s_clk_for; init too, while init's DMA shape
 * is the caller's): ESP_ERR_NOT_SUPPORTED without a usable row (nothing changed). After the enable the BCLK is read back (bsp_audio_bclk):
 * ESP_ERR_INVALID_RESPONSE when it does not give the rate (pc_i2s_rate_ok); the output then stays muted and the next call reconfigures (init: the
 * channel is deleted). Reshape (hires T8b): a channel on a table shape (pc_i2s_shape_follows) whose row has another DMA shape (8 x 480 at
 * 88.2/96k, 6 x 480 at <= 48k) is deleted and made again with the row's shape, then the same settle; on a failure no channel is left
 * (bsp_audio_is_init() false). A caller's own shape (the instrument's 3 x 64) is never changed. */
esp_err_t bsp_audio_set_rate(uint32_t sample_rate);
/* hires T12: another clock source (XTAL <-> APLL) or DMA shape is the same reshape; both paths log "APP audio clk rate= src= mclk= dma= bclk= us=".
 * 176.4k and up run on the APLL (mclk 128 fs, 8 x 511 descriptors, +9,664 B internal while above 96k; this file is the APLL's only owner,
 * tools/check/apll_guard.sh). The read-back is MEASURED: tx-done events against esp_timer over the first 50 ms of an APLL settle (muted; a mismatch beyond
 * 1 % -> ESP_ERR_INVALID_RESPONSE, muted) and over the first 500 ms of any clock above 48k (bsp_audio_clock_poll). A shape that cannot be allocated
 * above 48k falls back to 6 x 480 at the same rate ("APP audio dma fallback"). */
esp_err_t bsp_audio_clock_poll(void);               /* the audio task calls it every block: ESP_ERR_INVALID_RESPONSE once when the 500 ms window (and a 250 ms confirming one) measured a wrong rate; the output is then muted and the next set_rate reprograms */
/* 16-bit channel: interleaved int16 stereo; ESP_ERR_INVALID_STATE on a 32-bit channel */
esp_err_t bsp_audio_write(const int16_t *stereo, size_t frames, size_t *written_frames, uint32_t timeout_ms);
/* 32-bit channel: interleaved int32 stereo (frames * 8 bytes); ESP_ERR_INVALID_STATE on a 16-bit channel */
esp_err_t bsp_audio_write32(const int32_t *stereo, size_t frames, size_t *written_frames, uint32_t timeout_ms);
/* BTAUDIO (FakePod, cfg dac_slot24_from): at or above the rate the mode names the slot is 24 bits wide (ASP 24-bit slot, BCLK 48 x Fs), else the caller's width; set_rate and reinit
 * make the channel again when the width differs. The round board ignores it. write24: packed 3-byte little-endian samples (6 bytes per frame); ESP_ERR_INVALID_STATE on any other width. */
void      bsp_audio_set_slot_mode(int mode);
esp_err_t bsp_audio_apply_options(void);            /* FIX4: slot width + DAC options applied now (muted around it; no channel: the next open does); call after set_slot_mode / dac_config */
esp_err_t bsp_audio_write24(const uint8_t *packed, size_t frames, size_t *written_frames, uint32_t timeout_ms);
uint32_t  bsp_audio_rate(void);                     /* the rate the clock is set to; 0 when not init */
uint8_t   bsp_audio_bits(void);                     /* slot width of the open channel (16, 32 or 24); 0 when not init */
uint32_t  bsp_audio_bclk(void);                     /* BCLK read back after the last enable (Hz); 0 when not init */
esp_err_t bsp_audio_mute(bool mute);
esp_err_t bsp_audio_deinit(void);                   /* mutes, deletes the channel (and its DMA); remembers width and DMA shape for bsp_audio_reinit */
esp_err_t bsp_audio_reinit(uint32_t rate);          /* init_w again with the last successful width and DMA shape (a table shape: the rate's row shape); leaves output muted; ESP_ERR_INVALID_STATE if none ever succeeded */
uint32_t  bsp_audio_underruns(void);                /* steady underruns (the DMA ran dry after the first data of a start), since boot: callers take deltas */
uint32_t  bsp_audio_start_underruns(void);          /* night3 C: the ones counted while a start was armed (channel enable, flush, bsp_audio_arm_start) and its first data not yet queued: silence while the first block decodes; since boot */
void      bsp_audio_arm_start(void);                /* a new source is opening: the underruns until its first data are start-gap ones */
/* Decode-ahead reservoir (pc_burst.h): a wired native rate above 96 kHz only. Writes (bsp_audio_write*) then go into a 250 ms PSRAM FIFO that a writer task feeds to the I2S;
 * a mute from the audio task drops what is queued; deinit / set_rate close it first. active: the FIFO is up. wr_err: failed writer writes since boot (add to the caller's own count).
 * window: the lowest fill (ms) since the last call, valid only once the FIFO had been full in the window; false when not active. */
bool      bsp_audio_burst_active(void);
uint32_t  bsp_audio_burst_wr_err(void);
bool      bsp_audio_burst_window(uint32_t *min_ms, bool *valid);
bool      bsp_audio_is_init(void);
/* F2: the headphone-detect verdict of the DAC (the FakePod's CS43131 senses the jack itself): 1 plugged, 0 out, -1 unknown / no such part (the round board's PCM5102A cannot tell). `dac` console command. */
int  bsp_audio_headphone(void);
/* DACFS (FakePod only): the DAC's own volume register is the volume of the wired path (the digital stage passes unity there). vol 0..100, 0 = mute; kept until the DAC
 * is up; sound only after the registers read back. dac_config (dac_hpf, dac_fs, dac_amute) must come before the first bsp_audio_init. Both are no-ops on the round board (PCM5102A: no control bus). */
esp_err_t bsp_audio_set_volume(int vol);   /* ESP_ERR_INVALID_STATE when the DAC is faulted (not answering): the caller drops its digital gain to 0 */
int       bsp_audio_dac_errors(void);      /* FakePod: the DAC serial-port error bits since the last call (0 none / not a FakePod) */
uint8_t   bsp_audio_dac_note(void);        /* FakePod: 1 PLL fallback, 2 DAC faulted (read and cleared); 0 none */
bool      bsp_audio_dac_absent(void);      /* FakePod: no CS43131 answered (a PCM5102 variant): wired output is refused; false on the round board */
void      bsp_audio_power_down(void);      /* FakePod: before the PMU cuts the power: pop-free DAC stop, crystal off, RESET held low (the clocks must still run); no-op on the round board */
bool      bsp_audio_dac_fault(void);       /* FakePod: faulted, not yet initialised again */
void bsp_audio_dac_config(int hpf, int fs, int amute);
int  bsp_audio_dac_drive(void);        /* FakePod: the output drive the DAC's registers show (0 = 1.0 V, 1 = 1.41 V, 2 = 1.73 V, 3 = not what boot wrote), -1 unknown / round board */
bool bsp_audio_dac_cfg_pending(void);  /* FakePod: a DAC option changed and the chip still runs the old one (applied at the next start) */
int  bsp_audio_dac_drive_set(void);    /* FakePod: the drive the chip was last configured for; -1 round board */
void bsp_audio_dump(void);
