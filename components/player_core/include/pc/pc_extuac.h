// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 外接模式 T4: the UAC2 control plane of the sound card (spec 2026-11-15-external-mode.md section 3) as a pure function pair: what the computer asks the
 * clock source and the feature unit (the entities pc_extdesc declares) and what we answer. No TinyUSB, no RTOS. The firmware (ext_uac.c) and the simulator
 * feed it the bytes of the class requests. Never above 0 dB: the volume range ends at 0 dB and a SET above it is clamped (T5 multiplies the stream by it;
 * the real level is the device volume and the fixed limiter behind it). */
#define PC_EXTUAC_ENT_CLOCK 1       /* entity ids of pc_extdesc.c */
#define PC_EXTUAC_ENT_FU    3
#define PC_EXTUAC_CS_SAM_FREQ 0x01  /* clock source controls */
#define PC_EXTUAC_CS_VALID    0x02
#define PC_EXTUAC_FU_MUTE   0x01    /* feature unit controls */
#define PC_EXTUAC_FU_VOLUME 0x02
#define PC_EXTUAC_REQ_CUR   0x01
#define PC_EXTUAC_REQ_RANGE 0x02
#define PC_EXTUAC_VOL_MIN   (-90 * 256)   /* 1/256 dB */
#define PC_EXTUAC_VOL_MAX   0
#define PC_EXTUAC_VOL_RES   256
#define PC_EXTUAC_MAX_RATES 8

typedef struct {
    uint32_t rates[PC_EXTUAC_MAX_RATES];   /* the advertised list (pc_extdesc_info_t.rates) */
    uint8_t n_rates;
    uint32_t freq;                         /* the clock source's current rate */
    uint8_t mute[3];                       /* master, left, right */
    int16_t vol[3];                        /* 1/256 dB, <= 0 */
} pc_extuac_t;

void pc_extuac_init(pc_extuac_t *u, const uint32_t *rates, int n);   /* 48000 when advertised (else the first), unmuted, 0 dB */
/* GET: writes the answer into out (cap bytes); returns its length, or -1 = the request is not ours (the caller stalls). entity / control selector / channel as in the setup packet. */
int pc_extuac_get(const pc_extuac_t *u, uint8_t entity, uint8_t cs, uint8_t ch, uint8_t request, uint8_t *out, int cap);
/* SET CUR: data/len of the data stage. true = accepted (changed values stored, a volume above 0 dB clamped to 0 dB; a rate outside the list is refused). */
bool pc_extuac_set(pc_extuac_t *u, uint8_t entity, uint8_t cs, uint8_t ch, const uint8_t *data, int len);
/* the stream's linear gain from the master and channel volumes and mutes, <= 1.0 by construction (T5 multiplies the samples by it) */
float pc_extuac_gain(const pc_extuac_t *u, int ch);   /* ch 0 left, 1 right */

#ifdef __cplusplus
}
#endif
