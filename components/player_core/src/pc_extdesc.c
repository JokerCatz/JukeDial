// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extdesc.h"
#include "pc/pc_rate.h"
#include <string.h>

static const uint32_t k_cand[PC_EXTDESC_MAX_RATES] = { 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };

uint32_t pc_extdesc_candidate(int i) { return (i >= 0 && i < PC_EXTDESC_MAX_RATES) ? k_cand[i] : 0; }

int pc_extdesc_rates(uint32_t max_rate, uint16_t drop_mask, uint32_t out[PC_EXTDESC_MAX_RATES])
{
    int n = 0;
    for (int i = 0; i < PC_EXTDESC_MAX_RATES; i++)
        if (k_cand[i] <= max_rate && !(drop_mask & (1u << i)) && pc_mixer_rate_ok(k_cand[i])) out[n++] = k_cand[i];
    return n;
}

uint16_t pc_extdesc_pkt(uint32_t rate, int bytes)
{
    return (uint16_t)(((rate + 7999) / 8000 + 1) * (uint32_t)bytes * 2);
}

typedef struct { uint8_t *b; size_t cap, n; bool over; } w_t;
static void put(w_t *w, const uint8_t *p, size_t n)
{
    if (w->n + n > w->cap) { w->over = true; return; }
    memcpy(w->b + w->n, p, n);
    w->n += n;
}
#define PUT(w, ...) do { const uint8_t _d[] = { __VA_ARGS__ }; put((w), _d, sizeof _d); } while (0)
#define LO(x) ((uint8_t)((x) & 0xFF))
#define HI(x) ((uint8_t)(((x) >> 8) & 0xFF))

#define UAC_AC_CLOCK 1
#define UAC_AC_IT 2
#define UAC_AC_FU 3
#define UAC_AC_OT 4
#define AC_BODY_LEN (9 + 8 + 17 + 18 + 12)

int pc_extdesc_build(const pc_extdesc_cfg_t *cfg, uint8_t *buf, size_t cap, pc_extdesc_info_t *info)
{
    pc_extdesc_info_t in;
    memset(&in, 0, sizeof in);
    in.msc_itf = in.hid_itf = in.ac_itf = in.as_itf = 0xFF;
    uint8_t ni = 0;
    if (cfg->msc) in.msc_itf = ni++;
    in.hid_itf = ni++;
    if (cfg->uac) {
        in.n_rates = (uint8_t)pc_extdesc_rates(cfg->max_rate, cfg->drop_mask, in.rates);
        if (in.n_rates == 0 || !(cfg->bits_mask & 7)) return -2;
        in.ac_itf = ni++;
        in.as_itf = ni++;
        static const uint8_t bits[3] = { 16, 24, 32 };
        for (int i = 0; i < 3; i++)
            if (cfg->bits_mask & (1u << i)) {
                in.alt_bits[in.n_alt] = bits[i];
                in.alt_max_pkt[in.n_alt] = pc_extdesc_pkt(in.rates[in.n_rates - 1], bits[i] / 8);
                in.n_alt++;
            }
    }
    in.n_itf = ni;

    w_t w = { buf, cap, 0, false };
    PUT(&w, 9, 0x02, 0, 0, ni, 1, 0, 0x80, 50);   /* wTotalLength patched below; bus powered, 100 mA */
    if (cfg->msc) {
        PUT(&w, 9, 0x04, in.msc_itf, 0, 2, 0x08, 0x06, 0x50, cfg->str_msc);
        PUT(&w, 7, 0x05, PC_EXTDESC_EP_MSC_OUT, 0x02, LO(512), HI(512), 0);
        PUT(&w, 7, 0x05, PC_EXTDESC_EP_MSC_IN, 0x02, LO(512), HI(512), 0);
    }
    PUT(&w, 9, 0x04, in.hid_itf, 0, 1, 0x03, 0x00, 0x00, cfg->str_hid);
    PUT(&w, 9, 0x21, 0x11, 0x01, 0, 1, 0x22, LO(cfg->hid_report_len), HI(cfg->hid_report_len));
    PUT(&w, 7, 0x05, PC_EXTDESC_EP_HID_IN, 0x03, 16, 0, 4);
    if (cfg->uac) {
        PUT(&w, 8, 0x0B, in.ac_itf, 2, 0x01, 0x00, 0x20, cfg->str_audio);          /* IAD */
        PUT(&w, 9, 0x04, in.ac_itf, 0, 0, 0x01, 0x01, 0x20, cfg->str_audio);       /* AudioControl */
        PUT(&w, 9, 0x24, 0x01, 0x00, 0x02, 0x01, LO(AC_BODY_LEN), HI(AC_BODY_LEN), 0x00);   /* header: ADC 2.0, desktop speaker */
        PUT(&w, 8, 0x24, 0x0A, UAC_AC_CLOCK, 0x03, 0x03, 0, 0);                    /* clock source: internal programmable, rate read/write */
        PUT(&w, 17, 0x24, 0x02, UAC_AC_IT, 0x01, 0x01, 0, UAC_AC_CLOCK, 2, 3, 0, 0, 0, 0, 0, 0, 0);   /* USB streaming, 2 ch (L,R) */
        PUT(&w, 18, 0x24, 0x06, UAC_AC_FU, UAC_AC_IT, 0x0F, 0, 0, 0, 0x0F, 0, 0, 0, 0x0F, 0, 0, 0, 0);  /* feature unit: mute + volume, master + 2 */
        PUT(&w, 12, 0x24, 0x03, UAC_AC_OT, 0x01, 0x03, 0, UAC_AC_FU, UAC_AC_CLOCK, 0, 0, 0);      /* speaker */
        PUT(&w, 9, 0x04, in.as_itf, 0, 0, 0x01, 0x02, 0x20, 0);                    /* AudioStreaming alt 0: no endpoints */
        for (int a = 0; a < in.n_alt; a++) {
            uint8_t bits = in.alt_bits[a], slot = bits / 8;
            PUT(&w, 9, 0x04, in.as_itf, (uint8_t)(a + 1), 2, 0x01, 0x02, 0x20, 0);
            PUT(&w, 16, 0x24, 0x01, UAC_AC_IT, 0, 0x01, 1, 0, 0, 0, 2, 3, 0, 0, 0, 0);
            PUT(&w, 6, 0x24, 0x02, 0x01, slot, bits);
            PUT(&w, 7, 0x05, PC_EXTDESC_EP_AUD_OUT, 0x05, LO(in.alt_max_pkt[a]), HI(in.alt_max_pkt[a]), 1);   /* iso, async */
            PUT(&w, 8, 0x25, 0x01, 0, 0, 0, 0, 0);
            PUT(&w, 7, 0x05, PC_EXTDESC_EP_AUD_FB, 0x11, 4, 0, 4);                 /* iso feedback; bInterval 4 (8 ms refresh, the TinyUSB uac2 HS convention): with 1 the DWC2 loses ~1/3 of the iso OUT packets (peer test: 5000 -> 8000 packets/s) */
        }
    }
    if (w.over) return -1;
    buf[2] = LO(w.n);
    buf[3] = HI(w.n);
    in.total_len = (uint16_t)w.n;
    if (info) *info = in;
    return (int)w.n;
}

bool pc_extdesc_combo_msc(int combo) { return combo != PC_EXTCOMBO_SOUND; }
bool pc_extdesc_combo_uac(int combo) { return combo != PC_EXTCOMBO_DRIVE; }
int pc_extdesc_combo_next(int combo) { return combo == PC_EXTCOMBO_ALL ? PC_EXTCOMBO_DRIVE : combo == PC_EXTCOMBO_DRIVE ? PC_EXTCOMBO_SOUND : PC_EXTCOMBO_ALL; }
uint16_t pc_extdesc_combo_pid(int combo) { return combo == PC_EXTCOMBO_DRIVE ? 0x4002 : combo == PC_EXTCOMBO_SOUND ? 0x4005 : 0x4004; }

void pc_extdesc_fifo(const pc_extdesc_cfg_t *cfg, const pc_extdesc_info_t *info, pc_extdesc_fifo_t *o)
{
    uint32_t maxout = 0;                                   /* bytes of the largest OUT packet */
    if (cfg->msc) maxout = 512;
    if (cfg->uac && info) for (int a = 0; a < info->n_alt; a++) if (info->alt_max_pkt[a] > maxout) maxout = info->alt_max_pkt[a];
    o->rx = (uint16_t)(13 + 1 + 2 * (maxout / 4 + 1) + 2 * 16);   /* dcd_dwc2.c calc_device_grxfsiz: setup packets + global NAK + 2 x (largest packet in words + 1) + one status word per endpoint */
    o->tx_ep0 = 16;
    o->tx_msc = cfg->msc ? 128 : 0;                        /* 512 B / 4 */
    o->tx_hid = 4;                                         /* 16 B / 4 */
    o->tx_fb = cfg->uac ? 1 : 0;                           /* the 4 byte feedback word */
    o->total = (uint16_t)(o->rx + o->tx_ep0 + o->tx_msc + o->tx_hid + o->tx_fb);
}
