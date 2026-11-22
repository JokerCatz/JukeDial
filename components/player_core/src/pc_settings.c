// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_settings.h"
#include "pc/pc_castq.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static const pc_setting_t S[] = {
    { "brightness", 10, 100, 60, false },      /* below 10 the screen looks dead */
    { "volume_max", 20, 100, 100, false },
    { "haptic_on", 0, 1, 1, false },           /* the device's on/off switch (settings page); the strength and the length below are web-only */
    { "haptic_mv", 640, 1920, 1280, false },   /* 1920 mV is the driver's limit; off is haptic_on */
    { "haptic_ms", 0, 30, 8, false },
    { "home_hold_ms", 2000, 4000, 3000, false },   /* CF2: how long a press on a content page (or on any Back) must be held to go home: 2000 / 3000 / 4000 (default 3000); web only, read at each press */
    { "cover_smooth", 0, 1, 0, false },
    { "cast_q", 0, 2, PC_CAST_Q_DEFAULT, true },               /* applies when the broadcast (re)starts */
    { "bt_mode", 0, 4, 0, true },              /* 4 = Auracast listening */
    { "hp_absvol", 0, 1, 1, false },           /* headphones: one volume through AVRCP absolute volume; the next connection takes it */
    { "rg", 0, 2, 1, false },                  /* ReplayGain: 0 off, 1 track, 2 album (the track playing follows at once) */
    { "rg_untag", -12, 6, 0, false },          /* dB for files without a ReplayGain tag */
    { "toysfx", 0, 1, 1, false },              /* toy sound effects (sfx.h): 0 off, 1 on; the next sfx_play reads it */
    { "lang", 0, 2, 0, false },                /* i18n A: 0 繁中, 1 簡中, 2 English; the screens are built again at once (ctl.c) */
    { "cover_auto", 0, 1, 1, false },          /* hires T8e 封面快取: 1 自動 (the background pass builds covers by itself), 0 手動 (only 立即建置) */
    /* settings-move: the details moved off the device (web page) and the keys that never had a control (the web "advanced" block) */
    { "album_sec", 10, 300, 30, false },       /* album wall: seconds per photo, 10 / 30 / 60 / 300 */
    { "album_fade", 0, 1, 1, false },          /* album wall: cross-fade (1) or cut (0) */
    { "gapless", 0, 1, 1, false },             /* advanced: near-gapless joins; read per track */
    { "vocab_new", 0, 1000, 10, false },       /* new vocabulary cards a day; read when a session starts */
    { "dac_bits", 16, 32, 32, true },          /* advanced: wired word length (16 = the undithered A/B path); read at boot */
    { "dac_hard_rate", 48000, 384000, 384000, false },   /* FakePod: the highest source rate accepted (48 / 96 / 192 / 384 kHz); above it the file is refused before it is read; the halve threshold is dac_max_rate */
    { "dac_hpf", 0, 1, 1, true },              /* advanced, FakePod CS43131: the DC-blocking high-pass (DS 7.5.2: on by default, no real effect above 20 Hz; 0 = A/B); read at boot */
    { "dac_fs", 0, 2, 0, true },               /* advanced, FakePod CS43131: full-scale output 0 = 1.0 Vrms, 1 = 1.41 Vrms, 2 = 1.73 Vrms experimental (OUT_FS + HV_EN, DS 7.5.1 / 7.5.10; the UI row takes 2 only after a disclaimer tap); read at boot */
    { "dac_amute", 0, 1, 1, true },            /* advanced, FakePod CS43131: PCM auto-mute after 8192 silent samples (DS 7.5.5; 0 = off, we mute ourselves); read at boot */
    { "ldac_kbps", 330, 990, 990, false },     /* advanced: LDAC top rate 330 / 492 / 660 / 990 (JC_LDAC builds); applies now */
    { "bt_codec", 0, 3, 3, false },            /* BTAUDIO: headphones codec ceiling 0 SBC / 1 AAC / 2 aptX / 3 LDAC (the best the sink and this build also allow); applies at the next connection */
    { "bt_rate", 44100, 96000, 96000, false }, /* BTAUDIO: link rate ceiling 44.1 / 48 / 96 kHz */
    { "bt_bits", 16, 24, 24, false },          /* BTAUDIO: link bit depth ceiling 16 / 24 */
    { "bt_chan", 0, 1, 0, false },             /* BTAUDIO: channel mode 0 stereo / 1 joint stereo (SBC) */
    { "dac_slot24_from", 0, 3, 0, false },     /* BTAUDIO, FakePod: I2S slot 24-bit from 0 off / 1 352.8k / 2 192k / 3 every rate; read at each rate switch (default off until the board test) */
    { "cstep", 0, 1, 1, false },               /* advanced: connection-parameter ladder; read at the next connection */
    { "rotate", 0, 1, 0, true },               /* screen: 0 upright (today), 1 turned 180 degrees (pc_rotate.h); read at boot, kept on the card so a PC can fix a bad value */
    { "exp_fmt", 0, 1, 0, false },             /* format tiers: 1 = the experimental readers play (some DOS OPL players, .ice); 0 = listed by name only, never read by the scan, refused with a reason; read at every scan / open */
    { "cpu_mhz", 160, 320, 320, false },       /* EXPERIMENTAL CPU clock 320 (default, the tested speed) / 240 / 160 MHz (app/main/cpu_clock.c); applies at once after boot; boot and a crash go back to 320 */
    { "amiga_mix", 0, 3, 0, false },           /* Amiga tone: 0 off, 1 A500, 2 A500 + LED, 3 A1200 (not behind exp_fmt since 2026-11-18); a playing module takes it within one block */
    { "splash", 0, 1, 1, false },              /* screen: boot picture (boot_front.c / splash.c): 1 shows the card's splash picture, 0 never; read at boot, web only */
    { "font_src", 0, 2, 0, true },             /* advanced: 0 auto, 1 flash only, 2 card only; read at boot */
};

const pc_setting_t *pc_settings(int *n) { *n = (int)(sizeof S / sizeof S[0]); return S; }

/* keys that take a few values, not a range: any other number in the range is refused */
static bool step_ok(const char *key, int v)
{
    if (!strcmp(key, "album_sec")) return v == 10 || v == 30 || v == 60 || v == 300;
    if (!strcmp(key, "home_hold_ms")) return v == 2000 || v == 3000 || v == 4000;
    if (!strcmp(key, "dac_bits")) return v == 16 || v == 32;
    if (!strcmp(key, "dac_hard_rate")) return v == 48000 || v == 96000 || v == 192000 || v == 384000;
    if (!strcmp(key, "bt_rate")) return v == 44100 || v == 48000 || v == 96000;
    if (!strcmp(key, "bt_bits")) return v == 16 || v == 24;
    if (!strcmp(key, "cpu_mhz")) return v == 160 || v == 240 || v == 320;
    if (!strcmp(key, "ldac_kbps")) return v == 330 || v == 492 || v == 660 || v == 990;
    return true;
}

int pc_setting_check(const char *key, const char *val, int *out)
{
    for (size_t i = 0; i < sizeof S / sizeof S[0]; i++) {
        if (strcmp(S[i].key, key)) continue;
        char *end;
        long v = strtol(val, &end, 10);
        if (end == val || *end || v < S[i].min || v > S[i].max || !step_ok(key, (int)v)) return -1;
        *out = (int)v;
        return 0;
    }
    return -1;
}
