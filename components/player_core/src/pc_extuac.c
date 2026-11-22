// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extuac.h"
#include <math.h>
#include <string.h>

void pc_extuac_init(pc_extuac_t *u, const uint32_t *rates, int n)
{
    memset(u, 0, sizeof *u);
    if (n > PC_EXTUAC_MAX_RATES) n = PC_EXTUAC_MAX_RATES;
    for (int i = 0; i < n; i++) u->rates[i] = rates[i];
    u->n_rates = (uint8_t)n;
    u->freq = n > 0 ? rates[0] : 0;
    for (int i = 0; i < n; i++) if (rates[i] == 48000) u->freq = 48000;
}

static void le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

int pc_extuac_get(const pc_extuac_t *u, uint8_t entity, uint8_t cs, uint8_t ch, uint8_t request, uint8_t *out, int cap)
{
    if (entity == PC_EXTUAC_ENT_CLOCK) {
        if (cs == PC_EXTUAC_CS_SAM_FREQ && request == PC_EXTUAC_REQ_CUR) { if (cap < 4) return -1; le32(out, u->freq); return 4; }
        if (cs == PC_EXTUAC_CS_SAM_FREQ && request == PC_EXTUAC_REQ_RANGE) {    /* wNumSubRanges, then {min, max, res} per rate: each rate is its own point */
            int len = 2 + 12 * u->n_rates;
            if (cap < len) return -1;
            le16(out, u->n_rates);
            for (int i = 0; i < u->n_rates; i++) { le32(out + 2 + 12 * i, u->rates[i]); le32(out + 6 + 12 * i, u->rates[i]); le32(out + 10 + 12 * i, 0); }
            return len;
        }
        if (cs == PC_EXTUAC_CS_VALID && request == PC_EXTUAC_REQ_CUR) { if (cap < 1) return -1; out[0] = 1; return 1; }
        return -1;
    }
    if (entity == PC_EXTUAC_ENT_FU && ch <= 2) {
        if (cs == PC_EXTUAC_FU_MUTE && request == PC_EXTUAC_REQ_CUR) { if (cap < 1) return -1; out[0] = u->mute[ch]; return 1; }
        if (cs == PC_EXTUAC_FU_VOLUME && request == PC_EXTUAC_REQ_CUR) { if (cap < 2) return -1; le16(out, (uint16_t)u->vol[ch]); return 2; }
        if (cs == PC_EXTUAC_FU_VOLUME && request == PC_EXTUAC_REQ_RANGE) {
            if (cap < 8) return -1;
            le16(out, 1); le16(out + 2, (uint16_t)(int16_t)PC_EXTUAC_VOL_MIN); le16(out + 4, (uint16_t)(int16_t)PC_EXTUAC_VOL_MAX); le16(out + 6, PC_EXTUAC_VOL_RES);
            return 8;
        }
    }
    return -1;
}

bool pc_extuac_set(pc_extuac_t *u, uint8_t entity, uint8_t cs, uint8_t ch, const uint8_t *data, int len)
{
    if (entity == PC_EXTUAC_ENT_CLOCK && cs == PC_EXTUAC_CS_SAM_FREQ) {
        if (len != 4) return false;
        uint32_t f = (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
        for (int i = 0; i < u->n_rates; i++) if (u->rates[i] == f) { u->freq = f; return true; }
        return false;
    }
    if (entity == PC_EXTUAC_ENT_FU && ch <= 2) {
        if (cs == PC_EXTUAC_FU_MUTE) { if (len != 1) return false; u->mute[ch] = data[0] ? 1 : 0; return true; }
        if (cs == PC_EXTUAC_FU_VOLUME) {
            if (len != 2) return false;
            int v = (int16_t)(uint16_t)(data[0] | data[1] << 8);
            if (v > PC_EXTUAC_VOL_MAX) v = PC_EXTUAC_VOL_MAX;        /* never above 0 dB */
            if (v < PC_EXTUAC_VOL_MIN) v = PC_EXTUAC_VOL_MIN;
            u->vol[ch] = (int16_t)v;
            return true;
        }
    }
    return false;
}

float pc_extuac_gain(const pc_extuac_t *u, int ch)
{
    if (ch < 0 || ch > 1) return 0.0f;
    if (u->mute[0] || u->mute[1 + ch]) return 0.0f;
    int v = u->vol[0] + u->vol[1 + ch];                              /* 1/256 dB, each <= 0 */
    if (v > 0) v = 0;
    return v <= PC_EXTUAC_VOL_MIN ? 0.0f : powf(10.0f, (float)v / (256.0f * 20.0f));
}
