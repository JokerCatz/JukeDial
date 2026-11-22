// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_preset.h"
#include "pc_test.h"

static char *slurp(const char *name)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", FIXTURES, name);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return buf;
}

static void test_preset_basic(void)
{
    pc_preset_t p;
    CHECK_EQ_INT(pc_preset_parse(slurp("preset_basic.toml"), &p), 0);
    CHECK_STR(p.name, "Warm room");
    CHECK_EQ_INT(p.nmusic, 2);
    CHECK_STR(p.music[0].kind, "equalizer");
    CHECK(p.music[0].supported && p.music[0].enabled);
    CHECK_EQ_INT(p.music[0].neq, 2);
    CHECK_NEAR(p.music[0].eq_preamp_db, -6.2, 1e-4);
    CHECK_EQ_INT(p.music[0].eq[1].type, 'L');
    CHECK_NEAR(p.music[0].eq[1].fc, 80, 1e-4);
    CHECK_STR(p.music[1].kind, "tone");
    CHECK_NEAR(p.music[1].mix, 0.5, 1e-6);
    CHECK_NEAR(pc_node_param(&p.music[1], "warmth", 0), 0.4, 1e-6);
    CHECK_NEAR(pc_node_param(&p.music[1], "depth", 0), -0.2, 1e-6);
    CHECK_NEAR(pc_node_param(&p.music[1], "punch", 9), 9, 1e-6);   /* absent -> default */
    CHECK_EQ_INT(p.nmaster, 1);
    CHECK_STR(p.master[0].kind, "limiter");
    CHECK_NEAR(pc_node_param(&p.master[0], "ceiling_db", -1), -1.5, 1e-6);
    CHECK_EQ_INT(p.nsends, 1);
    CHECK_EQ_INT(p.sends[0].from, 1);
    CHECK_EQ_INT(p.sends[0].to, -1);
    CHECK_NEAR(p.sends[0].level, 0.3, 1e-6);
    CHECK_STR(p.sends[0].node.kind, "freeverb");
    CHECK_NEAR(pc_node_param(&p.sends[0].node, "room", 0.5), 0.7, 1e-6);
    CHECK_EQ_INT(p.nunsup, 0);
}

static void test_preset_unsupported(void)
{
    pc_preset_t p;
    CHECK_EQ_INT(pc_preset_parse(slurp("preset_unsupported.toml"), &p), 0);
    CHECK_EQ_INT(p.nmusic, 4);
    CHECK(!p.music[0].supported);              /* tape */
    CHECK(p.music[1].supported);               /* gain */
    CHECK(!p.music[2].supported);              /* bass with only_for */
    CHECK(p.music[3].supported && !p.music[3].enabled);
    CHECK_EQ_INT(p.nunsup, 2);
    CHECK_STR(p.unsupported[0], "tape");
    CHECK_STR(p.unsupported[1], "bass (only_for)");
}

/* a room in the music list is accepted like a send (the old "a room only works as a send" rule is gone, T3) */
static void test_preset_music_freeverb(void)
{
    pc_preset_t p;
    CHECK_EQ_INT(pc_preset_parse("format = 1\n[[music]]\nkind = \"freeverb\"\n[[music]]\nkind = \"tape\"\n", &p), 0);
    CHECK(p.music[0].supported);
    CHECK(!p.music[1].supported);
    CHECK_EQ_INT(p.nunsup, 1);
}

static void test_autoeq(void)
{
    pc_eq_band_t b[10];
    float pre = 0;
    CHECK_EQ_INT(pc_autoeq_parse(slurp("autoeq.txt"), b, 10, &pre), 3);
    CHECK_NEAR(pre, -6.2, 1e-4);
    CHECK_EQ_INT(b[0].type, 'P'); CHECK_NEAR(b[0].fc, 105, 1e-4); CHECK_NEAR(b[0].gain_db, -2.3, 1e-4); CHECK_NEAR(b[0].q, 0.7, 1e-4);
    CHECK_EQ_INT(b[1].type, 'L'); CHECK_NEAR(b[1].gain_db, 5.5, 1e-4);
    CHECK_EQ_INT(b[2].type, 'H'); CHECK_NEAR(b[2].fc, 10000, 1e-3);
    CHECK_EQ_INT(pc_autoeq_parse("Filter 1: ON PK Fc 1 Hz Gain 1 dB Q 1\n", b, 0, &pre), 0);   /* max 0 */
}

static void test_preset_bad_toml(void)
{
    pc_preset_t p;
    CHECK(pc_preset_parse("format = 1\n[[music]\nkind=", &p) < 0);
    CHECK(pc_preset_parse("", &p) < 0);                        /* no format */
}

static void test_preset_format_2(void)
{
    pc_preset_t p;
    CHECK(pc_preset_parse("format = 2\nname = \"x\"\n", &p) < 0);
}

int main(void)
{
    RUN(test_preset_basic);
    RUN(test_preset_unsupported);
    RUN(test_preset_music_freeverb);
    RUN(test_autoeq);
    RUN(test_preset_bad_toml);
    RUN(test_preset_format_2);
    return 0;
}
