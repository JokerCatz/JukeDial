// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_settings.h"
#include "pc_test.h"
#include <stdio.h>
#include <string.h>

static void test_known_keys(void)
{
    int v;
    CHECK_EQ_INT(pc_setting_check("brightness", "70", &v), 0);
    CHECK_EQ_INT(v, 70);
    CHECK_EQ_INT(pc_setting_check("volume_max", "100", &v), 0);
    CHECK_EQ_INT(pc_setting_check("haptic_mv", "1280", &v), 0);
    CHECK_EQ_INT(pc_setting_check("haptic_mv", "0", &v), -1);          /* off is haptic_on (the device row), not a strength */
    CHECK_EQ_INT(pc_setting_check("haptic_on", "0", &v), 0);
    CHECK_EQ_INT(pc_setting_check("bt_mode", "3", &v), 0);
    CHECK_EQ_INT(pc_setting_check("bt_mode", "4", &v), 0);           /* Auracast listening */
}

static void test_rejects(void)
{
    int v = 123;
    CHECK_EQ_INT(pc_setting_check("brightness", "5", &v), -1);         /* below 10: the screen would look dead */
    CHECK_EQ_INT(pc_setting_check("brightness", "101", &v), -1);
    CHECK_EQ_INT(pc_setting_check("brightness", "7x", &v), -1);
    CHECK_EQ_INT(pc_setting_check("brightness", "", &v), -1);
    CHECK_EQ_INT(pc_setting_check("haptic_mv", "9999", &v), -1);       /* the driver limit */
    CHECK_EQ_INT(pc_setting_check("resume", "mp3 1 1", &v), -1);       /* internal keys are not settings */
    CHECK_EQ_INT(pc_setting_check("bt_mode", "5", &v), -1);
    CHECK_EQ_INT(v, 123);                                              /* untouched on failure */
}

/* ReplayGain: 0 off, 1 track (default), 2 album; rg_untag = dB for files without a tag (0 default) */
static void test_replaygain_keys(void)
{
    int v;
    CHECK_EQ_INT(pc_setting_check("rg", "0", &v), 0);
    CHECK_EQ_INT(pc_setting_check("rg", "2", &v), 0);
    CHECK_EQ_INT(pc_setting_check("rg", "3", &v), -1);
    CHECK_EQ_INT(pc_setting_check("rg_untag", "-6", &v), 0);
    CHECK_EQ_INT(v, -6);
    CHECK_EQ_INT(pc_setting_check("rg_untag", "-13", &v), -1);
    CHECK_EQ_INT(pc_setting_check("rg_untag", "7", &v), -1);
    int n;
    const pc_setting_t *s = pc_settings(&n);
    int def_rg = -1, def_untag = -1;
    for (int i = 0; i < n; i++) {
        if (!strcmp(s[i].key, "rg")) { def_rg = s[i].def; CHECK(!s[i].reboot); }
        if (!strcmp(s[i].key, "rg_untag")) def_untag = s[i].def;
    }
    CHECK_EQ_INT(def_rg, 1);
    CHECK_EQ_INT(def_untag, 0);
}

static void test_dac_hard_rate_key(void)
{
    int v = 0;
    CHECK_EQ_INT(pc_setting_check("dac_hard_rate", "384000", &v), 0); CHECK_EQ_INT(pc_setting_check("dac_hard_rate", "96000", &v), 0);
    CHECK_EQ_INT(pc_setting_check("dac_hard_rate", "100000", &v), -1);
}

static void test_btaudio_keys(void)
{
    int v = 0;
    CHECK_EQ_INT(pc_setting_check("bt_codec", "3", &v), 0); CHECK_EQ_INT(pc_setting_check("bt_codec", "4", &v), -1);
    CHECK_EQ_INT(pc_setting_check("bt_rate", "44100", &v), 0); CHECK_EQ_INT(pc_setting_check("bt_rate", "96000", &v), 0); CHECK_EQ_INT(pc_setting_check("bt_rate", "88200", &v), -1);
    CHECK_EQ_INT(pc_setting_check("bt_bits", "16", &v), 0); CHECK_EQ_INT(pc_setting_check("bt_bits", "20", &v), -1);
    CHECK_EQ_INT(pc_setting_check("bt_chan", "1", &v), 0); CHECK_EQ_INT(pc_setting_check("bt_chan", "2", &v), -1);
    CHECK_EQ_INT(pc_setting_check("dac_slot24_from", "3", &v), 0); CHECK_EQ_INT(pc_setting_check("dac_slot24_from", "4", &v), -1);
    int n, def_slot = -1;
    const pc_setting_t *s = pc_settings(&n);
    for (int i = 0; i < n; i++) if (!strcmp(s[i].key, "dac_slot24_from")) def_slot = s[i].def;
    CHECK_EQ_INT(def_slot, 0);                                          /* off until the board test passes */
}

static void test_hp_absvol_key(void)
{
    int v;
    CHECK_EQ_INT(pc_setting_check("hp_absvol", "0", &v), 0);
    CHECK_EQ_INT(pc_setting_check("hp_absvol", "1", &v), 0);
    CHECK_EQ_INT(pc_setting_check("hp_absvol", "2", &v), -1);
    int n, def = -1;
    const pc_setting_t *s = pc_settings(&n);
    for (int i = 0; i < n; i++)
        if (!strcmp(s[i].key, "hp_absvol")) { def = s[i].def; CHECK(!s[i].reboot); }   /* the next connection takes it: no reboot */
    CHECK_EQ_INT(def, 1);
}

/* the toy sound effects' switch (sfx): 0 off, 1 on (default); the web settings page sets it, no reboot */
static void test_toysfx_key(void)
{
    int v;
    CHECK_EQ_INT(pc_setting_check("toysfx", "0", &v), 0);
    CHECK_EQ_INT(pc_setting_check("toysfx", "1", &v), 0);
    CHECK_EQ_INT(pc_setting_check("toysfx", "2", &v), -1);
    int n, def = -1;
    const pc_setting_t *s = pc_settings(&n);
    for (int i = 0; i < n; i++)
        if (!strcmp(s[i].key, "toysfx")) { def = s[i].def; CHECK(!s[i].reboot); }
    CHECK_EQ_INT(def, 1);
}

/* hires T8e: 封面快取 自動 (1, default) / 手動 (0); the web settings page sets it, no reboot */
static void test_cover_auto_key(void)
{
    int v;
    CHECK_EQ_INT(pc_setting_check("cover_auto", "0", &v), 0);
    CHECK_EQ_INT(pc_setting_check("cover_auto", "1", &v), 0);
    CHECK_EQ_INT(pc_setting_check("cover_auto", "2", &v), -1);
    int n, def = -1;
    const pc_setting_t *s = pc_settings(&n);
    for (int i = 0; i < n; i++)
        if (!strcmp(s[i].key, "cover_auto")) { def = s[i].def; CHECK(!s[i].reboot); }
    CHECK_EQ_INT(def, 1);
}

/* settings-move: every key that left the device or never had a control is in the table with its range, its default and its allowed values */
static void test_moved_keys(void)
{
    static const struct { const char *k; int ok, bad, def; bool reboot; } M[] = {
        { "volume_max", 20, 19, 100, false }, { "rg", 2, 3, 1, false }, { "rg_untag", -12, -13, 0, false }, { "cover_smooth", 1, 2, 0, false },
        { "cover_auto", 0, 2, 1, false }, { "haptic_mv", 640, 639, 1280, false }, { "haptic_ms", 30, 31, 8, false }, { "cast_q", 2, 4, 1, true },
        { "home_hold_ms", 4000, 4001, 3000, false },
        { "album_sec", 300, 301, 30, false }, { "album_fade", 0, 2, 1, false }, { "gapless", 0, 2, 1, false }, { "vocab_new", 50, 1001, 10, false },
        { "dac_bits", 16, 24, 32, true }, { "ldac_kbps", 492, 500, 990, false }, { "cstep", 0, 2, 1, false }, { "font_src", 2, 3, 0, true }, { "rotate", 1, 2, 0, true }, { "splash", 1, 2, 1, false },
    };
    int n, v;
    const pc_setting_t *s = pc_settings(&n);
    for (unsigned k = 0; k < sizeof M / sizeof M[0]; k++) {
        char b[16];
        int found = 0;
        for (int i = 0; i < n; i++)
            if (!strcmp(s[i].key, M[k].k)) { found = 1; CHECK_EQ_INT(s[i].def, M[k].def); CHECK_EQ_INT(s[i].reboot, M[k].reboot); }
        CHECK_EQ_INT(found, 1);
        snprintf(b, sizeof b, "%d", M[k].ok);  CHECK_EQ_INT(pc_setting_check(M[k].k, b, &v), 0);
        snprintf(b, sizeof b, "%d", M[k].bad); CHECK_EQ_INT(pc_setting_check(M[k].k, b, &v), -1);
    }
    CHECK_EQ_INT(pc_setting_check("album_sec", "45", &v), -1);          /* in range, but not one of 10 / 30 / 60 / 300 */
    CHECK_EQ_INT(pc_setting_check("ldac_kbps", "400", &v), -1);
    CHECK_EQ_INT(pc_setting_check("home_hold_ms", "2500", &v), -1);    /* CF2: only 2000 / 3000 / 4000 */
    CHECK_EQ_INT(pc_setting_check("home_hold_ms", "2000", &v), 0);
    CHECK_EQ_INT(pc_setting_check("dac_bits", "20", &v), -1);
}

static void test_table(void)
{
    int n;
    const pc_setting_t *s = pc_settings(&n);
    CHECK(n >= 7);
    bool bt_reboot = false;
    for (int i = 0; i < n; i++) {
        CHECK(s[i].min <= s[i].def && s[i].def <= s[i].max);
        if (!strcmp(s[i].key, "bt_mode")) bt_reboot = s[i].reboot;
    }
    CHECK(bt_reboot);
}

int main(void)
{
    RUN(test_known_keys);
    RUN(test_rejects);
    RUN(test_replaygain_keys);
    RUN(test_hp_absvol_key);
    RUN(test_dac_hard_rate_key);
    RUN(test_btaudio_keys);
    RUN(test_moved_keys);
    RUN(test_toysfx_key);
    RUN(test_cover_auto_key);
    RUN(test_table);
    return 0;
}
