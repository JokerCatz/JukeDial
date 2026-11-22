// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 外接模式 T1: the USB configuration descriptor (spec 2026-11-15-external-mode.md section 2) as a pure function: no TinyUSB, no RTOS.
 * Combinations: HID is always there; MSC and UAC2 (stereo PCM out, async, feedback endpoint) are each in or out (the fallback of section 2).
 * Interface order MSC, HID, AudioControl, AudioStreaming (those present, numbered from 0). Endpoints are fixed whatever the mix:
 * MSC OUT 0x01 / IN 0x81 (bulk, 512), HID IN 0x82 (interrupt, 16 B), audio OUT 0x02 (iso, bInterval 1), feedback IN 0x83 (iso, 4 B, bInterval 1). */
#define PC_EXTDESC_EP_MSC_OUT 0x01
#define PC_EXTDESC_EP_MSC_IN  0x81
#define PC_EXTDESC_EP_HID_IN  0x82
#define PC_EXTDESC_EP_AUD_OUT 0x02
#define PC_EXTDESC_EP_AUD_FB  0x83
#define PC_EXTDESC_MAX_RATES  8
#define PC_EXTDESC_MAX_ALTS   3
#define PC_EXTDESC_MAX_LEN    400   /* a buffer this big always holds any combination */

#define PC_EXTDESC_BITS_16 0x01     /* bits mask (alt 1.. in this order: 16, 24, 32) */
#define PC_EXTDESC_BITS_24 0x02
#define PC_EXTDESC_BITS_32 0x04

typedef struct {
    bool msc;
    bool uac;                    /* UAC2 sound card */
    uint32_t max_rate;           /* the 有線最高取樣率 cap (Hz), already normalised by pc_dacmax_norm; rates above it are not advertised */
    uint8_t bits_mask;           /* PC_EXTDESC_BITS_*; uac with 0 is a build error */
    uint16_t drop_mask;          /* bit i = candidate rate i (pc_extdesc_candidate) taken out (a rate the board could not read back) */
    uint16_t hid_report_len;     /* length of the HID report descriptor the HID class descriptor points to */
    uint8_t str_msc, str_hid, str_audio;   /* string indexes (0 = none) */
} pc_extdesc_cfg_t;

typedef struct {
    uint16_t total_len;
    uint8_t n_itf;
    uint8_t msc_itf, hid_itf, ac_itf, as_itf;   /* 0xFF = absent */
    uint8_t n_alt;               /* AudioStreaming alternate settings with a stream (1..3) */
    uint8_t alt_bits[PC_EXTDESC_MAX_ALTS];      /* 16, 24, 32 */
    uint16_t alt_max_pkt[PC_EXTDESC_MAX_ALTS];  /* wMaxPacketSize of the audio OUT endpoint in that alt */
    uint8_t n_rates;
    uint32_t rates[PC_EXTDESC_MAX_RATES];       /* the advertised list, ascending (what the clock RANGE answer reports) */
} pc_extdesc_info_t;

uint32_t pc_extdesc_candidate(int i);          /* 0..7: 44100 48000 88200 96000 176400 192000 352800 384000; 0 beyond */
/* the advertised rates: candidates that pc_mixer_rate_ok() accepts, <= max_rate, not dropped. Returns the count, rates ascending. */
int pc_extdesc_rates(uint32_t max_rate, uint16_t drop_mask, uint32_t out[PC_EXTDESC_MAX_RATES]);
/* wMaxPacketSize for one rate: (ceil(rate/8000)+1) * bytes * 2 channels (spec section 2, TUD_AUDIO_EP_SIZE) */
uint16_t pc_extdesc_pkt(uint32_t rate, int bytes_per_sample);
/* Builds the whole configuration descriptor (config header included) into buf. Returns its length, or -1 if cap is too small,
 * -2 if the combination is invalid (uac without bits or without any usable rate). info may be NULL. */
int pc_extdesc_build(const pc_extdesc_cfg_t *cfg, uint8_t *buf, size_t cap, pc_extdesc_info_t *info);

/* The three function mixes of the mode (spec section 2): the default three-in-one, and the two exclusive fallbacks (HID is always there). The choice is kept
 * across restarts by the firmware (NVS) and changing it re-enumerates (a restart into the mode). */
typedef enum { PC_EXTCOMBO_ALL = 0, PC_EXTCOMBO_DRIVE, PC_EXTCOMBO_SOUND, PC_EXTCOMBO_N } pc_extcombo_t;
bool pc_extdesc_combo_msc(int combo);          /* MSC present (ALL, DRIVE) */
bool pc_extdesc_combo_uac(int combo);          /* UAC2 present (ALL, SOUND) */
int pc_extdesc_combo_next(int combo);          /* the cell cycles ALL -> DRIVE -> SOUND -> ALL; any other value reads as ALL */
uint16_t pc_extdesc_combo_pid(int combo);      /* idProduct per mix (a computer keeps one driver binding per VID/PID): 0x4004 / 0x4002 / 0x4005 */

/* The DWC2 FIFO words these descriptors ask for (spec section 2; 1024 words, 32 of them taken by DMA mode, 992 usable): one RX FIFO shared by every OUT endpoint,
 * a TX FIFO per IN endpoint. The firmware prints the same numbers at the mode start; the controller's own allocator (dcd_dwc2.c) must fit them. */
#define PC_EXTDESC_FIFO_WORDS 992
typedef struct { uint16_t rx, tx_ep0, tx_msc, tx_hid, tx_fb, total; } pc_extdesc_fifo_t;
void pc_extdesc_fifo(const pc_extdesc_cfg_t *cfg, const pc_extdesc_info_t *info, pc_extdesc_fifo_t *out);

#ifdef __cplusplus
}
#endif
