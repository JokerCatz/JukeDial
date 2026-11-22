// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes the sound card's USB side of the external mode (the hardware half of app/main/ext_uac.c): a fake host that plays a sine through the SAME shared code the board uses:
 * ext_audio_* (the feedback value, the ring, the mixer start), pc_extpath (render, fixed limiter, final clamp), and the fake audio_svc's mixer + post hook; the device volume
 * is applied here the way audio_svc applies it (no sink in this mode: never unity). Console (via `ext uac ...`):
 *   play <rate> <bits> <secs> [ppm [amp]]  the host selects the alternate setting of <bits> and the clock <rate>, then streams <secs> s of a 1 kHz sine of amplitude <amp>
 *                                           (default 1.0 = full scale) at the feedback's pace; the device's clock is <ppm> off (default 0). Prints one "SIM ext uac done ..." line.
 *   stop                                    alt 0
 *   status                                  "SIM ext uac status ..." (ext_audio_status)
 *   fu <1/256 dB> [ch]                      SET CUR volume of the feature unit (master or channel); prints the stream's gain after it
 *   mute <0|1>                              the feature unit's master mute
 * The data path of the host to the device is the pump's: the bytes go straight into ext_audio_feed (the endpoint FIFO is not modelled: its few packets are the pump's "extra" fill). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_svc.h"
#include "esp_timer.h"
#include "ext_audio.h"
#include "ext_usbdesc.h"
#include "pc/pc_extpath.h"
#include "pc/pc_rate.h"
#include "pc/pc_volume.h"
#include "sim.h"
#include "usb_mode.h"

static bool g_inited;
static pc_extdesc_info_t g_info;
static void ensure(void)
{
    if (g_inited || !usb_mode_has_uac()) return;      /* the board starts the audio only in a mix with the sound card */
    static uint8_t d[PC_EXTDESC_MAX_LEN];
    if (ext_usbdesc_build(usb_mode_combo(), d, sizeof d, &g_info, NULL) <= 0) return;
    pc_extuac_init(ext_audio_uac(), g_info.rates, g_info.n_rates);
    ext_audio_set_alts(&g_info);
    ext_audio_start();
    g_inited = true;
}

static void print_status(const char *tag)
{
    ext_audio_status_t s;
    ext_audio_status(&s);
    printf("SIM ext uac %s alt=%d bits=%d rate=%u mixer=%d refused=%d fill=%u target=%u underruns=%u overruns=%u drops=%u limited=%u fb=%u vol=%d\n", tag, s.alt, s.bits, (unsigned)s.rate, s.mixer, s.refused,
           (unsigned)s.fill_frames, (unsigned)s.target_frames, (unsigned)s.underruns, (unsigned)s.overruns, (unsigned)s.drops, (unsigned)s.limited, (unsigned)s.fb, fake_audio_volume());
}

static void put_sample(uint8_t *p, int bits, double v)    /* v in -1..1 */
{
    if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
    if (bits == 16) { int32_t x = (int32_t)lrint(v * 32767.0); p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }
    else if (bits == 24) { int32_t x = (int32_t)lrint(v * 8388607.0); p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); p[2] = (uint8_t)(x >> 16); }
    else { int64_t x = (int64_t)llrint(v * 2147483647.0); p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); p[2] = (uint8_t)(x >> 16); p[3] = (uint8_t)(x >> 24); }
}

static void play(uint32_t rate, int bits, int secs, double ppm, double amp)
{
    ensure();
    if (!usb_mode_has_uac() || !ext_audio_started()) { printf("SIM ext uac refused: no sound card in this mix\n"); return; }
    int alt = bits == 16 ? 1 : bits == 24 ? 2 : bits == 32 ? 3 : 0;
    if (!alt || secs < 1 || secs > 60) { printf("SIM ext uac refused bits=%d secs=%d\n", bits, secs); return; }
    uint8_t f[4] = { (uint8_t)rate, (uint8_t)(rate >> 8), (uint8_t)(rate >> 16), (uint8_t)(rate >> 24) };
    if (!pc_extuac_set(ext_audio_uac(), PC_EXTUAC_ENT_CLOCK, PC_EXTUAC_CS_SAM_FREQ, 0, f, 4)) { printf("SIM ext uac refused rate=%u (not advertised)\n", (unsigned)rate); return; }
    printf("SIM ext uac alt=%d pkt=%d\n", alt, g_info.alt_max_pkt[alt - 1]);
    ext_audio_event(alt, rate);
    ext_audio_pump();
    int vol = fake_audio_volume();
    float vg = pc_volume_gain(vol);
    static float blk[2 * PC_AUDIO_MAX_BLOCK];
    int frames = (int)(rate / 100);
    int bps = bits / 8, fb = 2 * bps;
    static uint8_t pkt[8 * 64];
    double acc = 0, ph = 0, next_block = 0, blk_period = 10000.0 / (1.0 + ppm * 1e-6);
    uint32_t held[40], value = 0;
    int first_sound = -1, blocks = 0;
    uint32_t fill_min = 0xFFFFFFFFu, fill_max = 0, fb_min = 0xFFFFFFFFu, fb_max = 0;
    float peak_in = 0, peak_hook = 0, peak_wire = 0;
    ext_audio_status_t st0;
    ext_audio_status(&st0);
    uint64_t t0 = (uint64_t)esp_timer_get_time(), next_pump = 0;
    value = ext_audio_fb(t0, 0);
    for (int i = 0; i < 40; i++) held[i] = value;
    long steps = (long)secs * 8000;
    for (long uf = 0; uf < steps; uf++) {                        /* one micro-frame (125 us) per step */
        uint64_t t = t0 + (uint64_t)uf * 125u;
        acc += (double)held[uf % 40] / 65536.0;                   /* the host acts on the feedback it read about 5 ms ago */
        uint32_t n = (uint32_t)acc;
        acc -= n;
        if (n > 64) n = 64;
        for (uint32_t k = 0; k < n; k++) {
            double v = amp * sin(ph);
            ph += 2.0 * M_PI * 1000.0 / (double)rate;
            if (v > peak_in) peak_in = (float)v;
            put_sample(pkt + k * (uint32_t)fb, bits, v);
            put_sample(pkt + k * (uint32_t)fb + bps, bits, v);
        }
        if (n) ext_audio_feed(pkt, n * (uint32_t)fb);
        if (t >= next_pump) { ext_audio_pump(); value = ext_audio_fb(next_pump, 0); next_pump += 1000; if (uf >= 16000 && value < fb_min) fb_min = value; if (uf >= 16000 && value > fb_max) fb_max = value; }
        held[uf % 40] = value;
        while (next_block <= (double)(uf * 125 + 125)) {          /* the audio task's block (10 ms of the device's own clock) */
            uint32_t mr = fake_audio_mixer_rate();
            int fr = mr ? (int)(mr / 100) : frames;
            int got = fake_audio_pull(blk, fr, mr ? mr : rate);
            if (mr && got) {
                float pk = 0;
                for (int k = 0; k < 2 * fr; k++) { float a = fabsf(blk[k]); if (a > pk) pk = a; }
                if (pk > 0 && first_sound < 0) first_sound = blocks;
                if (pk > peak_hook) peak_hook = pk;
                if (pk * vg > peak_wire) peak_wire = pk * vg;
                if (pk > 1.0f) printf("SIM ext uac OVER FULL SCALE %f\n", (double)pk);
            }
            ext_audio_status_t s;
            ext_audio_status(&s);
            if (uf >= 16000 && s.fill_frames < fill_min) fill_min = s.fill_frames;   /* statistics from 2 s on: the start (priming, the mixer's start) is not the steady state */
            if (uf >= 16000 && s.fill_frames > fill_max) fill_max = s.fill_frames;
            blocks++;
            next_block += blk_period;
        }
        if (uf % 128 == 127) sim_advance_ms(16);                 /* the virtual clock moves 16 ms per 128 micro-frames (LVGL timers, the mixer's clock check) */
    }
    ext_audio_status_t s;
    ext_audio_status(&s);
    if (fill_min == 0xFFFFFFFFu) fill_min = fb_min = 0;                 /* no statistics (a refused rate: nothing streamed) */
    printf("SIM ext uac done rate=%u bits=%d secs=%d refused=%d underruns=%u overruns=%u drops=%u limited=%u first_sound_block=%d fill_min=%u fill_max=%u target=%u fb_min=%u fb_max=%u fb_nominal=%u"
           " peak_in=%.3f peak_hook=%.4f vol=%d peak_wire=%.5f wire_ok=%d\n", (unsigned)rate, bits, secs, s.refused, (unsigned)(s.underruns - st0.underruns), (unsigned)(s.overruns - st0.overruns),
           (unsigned)(s.drops - st0.drops), (unsigned)s.limited, first_sound, (unsigned)fill_min, (unsigned)fill_max, (unsigned)s.target_frames, (unsigned)fb_min, (unsigned)fb_max,
           (unsigned)pc_extfb_nominal(rate), (double)peak_in, (double)peak_hook, vol, (double)peak_wire, peak_hook <= PC_EXTPATH_CEIL && peak_wire <= PC_EXTPATH_CEIL * vg + 1e-6f);
}

bool fake_ext_uac_cmd(const char *a)
{
    unsigned rate;
    int bits, secs, fu, ch = 0, m;
    double ppm = 0, amp = 1.0;
    ensure();
    if (sscanf(a, "play %u %d %d %lf %lf", &rate, &bits, &secs, &ppm, &amp) >= 3) play(rate, bits, secs, ppm, amp);
    else if (!strcmp(a, "stop")) { ext_audio_event(0, ext_audio_uac()->freq); ext_audio_pump(); print_status("status"); }
    else if (!strcmp(a, "status")) print_status("status");
    else if (sscanf(a, "fu %d %d", &fu, &ch) >= 1) {
        uint8_t v[2] = { (uint8_t)(fu & 0xFF), (uint8_t)((fu >> 8) & 0xFF) };
        bool ok = pc_extuac_set(ext_audio_uac(), PC_EXTUAC_ENT_FU, PC_EXTUAC_FU_VOLUME, (uint8_t)ch, v, 2);
        printf("SIM ext uac fu ok=%d gain_l=%.4f gain_r=%.4f\n", ok, (double)pc_extuac_gain(ext_audio_uac(), 0), (double)pc_extuac_gain(ext_audio_uac(), 1));
    }
    else if (sscanf(a, "mute %d", &m) == 1) {
        uint8_t v[1] = { (uint8_t)(m != 0) };
        bool ok = pc_extuac_set(ext_audio_uac(), PC_EXTUAC_ENT_FU, PC_EXTUAC_FU_MUTE, 0, v, 1);
        printf("SIM ext uac mute ok=%d gain_l=%.4f\n", ok, (double)pc_extuac_gain(ext_audio_uac(), 0));
    }
    else return false;
    return true;
}
