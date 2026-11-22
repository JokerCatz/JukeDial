// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_kit.h"

static pc_kit_def_t k;

static void test_note_names(void)
{
    CHECK_EQ_INT(pc_kit_note("60"), 60);
    CHECK_EQ_INT(pc_kit_note("C4"), 60);
    CHECK_EQ_INT(pc_kit_note("c#4"), 61);
    CHECK_EQ_INT(pc_kit_note("Db4"), 61);
    CHECK_EQ_INT(pc_kit_note("C2"), 36);
    CHECK_EQ_INT(pc_kit_note("C-1"), 0);
    CHECK_EQ_INT(pc_kit_note("G9"), 127);
    CHECK_EQ_INT(pc_kit_note("G#9"), -1);
    CHECK_EQ_INT(pc_kit_note("128"), -1);
    CHECK_EQ_INT(pc_kit_note("H2"), -1);
    CHECK_EQ_INT(pc_kit_note(""), -1);
    CHECK_EQ_INT(pc_kit_note("36x"), -1);
}

static void test_txt(void)
{
    const char *t =
        "# demo kit\r\n"
        "36=kick.wav\n"
        "  38 = snare.wav   # backbeat\n"
        "F#2=hats/closed.wav\n"
        "pad1=38\n"
        "pad3=F#2\n"
        "bogus line\n"
        "200=nope.wav\n"
        "39=sub\\clap.wav\n";
    CHECK_EQ_INT(pc_kit_parse_txt(t, &k), 4);
    CHECK_STR(k.r[0].file, "kick.wav");
    CHECK_EQ_INT(k.r[0].lokey, 36);
    CHECK_EQ_INT(k.r[0].hikey, 36);
    CHECK_EQ_INT(k.r[0].center, 36);
    CHECK(k.r[0].one_shot);
    CHECK_STR(k.r[1].file, "snare.wav");
    CHECK_STR(k.r[2].file, "hats/closed.wav");
    CHECK_EQ_INT(k.r[2].lokey, 42);
    CHECK_STR(k.r[3].file, "sub/clap.wav");
    /* pads: 1 = 38, 3 = 42 given; the rest the lowest notes left: 36, 39 */
    CHECK_EQ_INT(k.pad[0], 38);
    CHECK_EQ_INT(k.pad[1], 36);
    CHECK_EQ_INT(k.pad[2], 42);
    CHECK_EQ_INT(k.pad[3], 39);
    CHECK_EQ_INT(k.pad[4], -1);
    CHECK_EQ_INT(pc_kit_find(&k, 38), 1);
    CHECK_EQ_INT(pc_kit_find(&k, 40), -1);
    CHECK_EQ_INT(pc_kit_pad_of(&k, 42), 2);
    CHECK_EQ_INT(pc_kit_pad_of(&k, 50), -1);
}

static void test_sfz(void)
{
    const char *t =
        "// a piano-ish thing\n"
        "<control> default_path=samples\\\n"
        "<global> volume=-3\n"
        "<group> lokey=0 hikey=59 /* lower half */ ampeg_release=0.5\n"
        "<region> sample=low C.wav pitch_keycenter=48\n"
        "<group>\n"
        "<region> sample=mid.wav lokey=60 hikey=71 pitch_keycenter=c4\n"
        "<region>sample=kick.wav key=36 loop_mode=one_shot\n"
        "<region> lokey=72 hikey=127 sample=high.wav\n";
    CHECK_EQ_INT(pc_kit_parse_sfz(t, &k), 4);
    CHECK_STR(k.r[0].file, "samples/low C.wav");     /* spaces in sample names, up to the next opcode */
    CHECK_EQ_INT(k.r[0].lokey, 0);
    CHECK_EQ_INT(k.r[0].hikey, 59);
    CHECK_EQ_INT(k.r[0].center, 48);
    CHECK(!k.r[0].one_shot);
    CHECK_STR(k.r[1].file, "samples/mid.wav");
    CHECK_EQ_INT(k.r[1].lokey, 60);
    CHECK_EQ_INT(k.r[1].center, 60);
    CHECK_EQ_INT(k.r[2].lokey, 36);                   /* key= sets all three */
    CHECK_EQ_INT(k.r[2].hikey, 36);
    CHECK_EQ_INT(k.r[2].center, 36);
    CHECK(k.r[2].one_shot);
    CHECK_EQ_INT(k.r[3].center, 60);                  /* the SFZ default keycenter */
    CHECK_EQ_INT(pc_kit_find(&k, 36), 2);             /* the later, narrower region over the group's 0..59 */
    CHECK_EQ_INT(pc_kit_find(&k, 40), 0);
    CHECK_EQ_INT(pc_kit_find(&k, 100), 3);
    CHECK_EQ_INT(k.pad[0], 36);                       /* default pads: the regions' key centres, lowest first */
    CHECK_EQ_INT(k.pad[1], 48);
    CHECK_EQ_INT(k.pad[2], 60);
    CHECK_EQ_INT(k.pad[3], -1);
    /* a region without a sample is dropped */
    CHECK_EQ_INT(pc_kit_parse_sfz("<region> key=60\n<region> sample=a.wav\n", &k), 1);
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static int wav(uint8_t *b, uint16_t fmt, uint16_t ch, uint32_t rate, uint16_t bits, bool list_chunk)
{
    int o = 0;
    memcpy(b, "RIFF", 4); memcpy(b + 8, "WAVE", 4); o = 12;
    memcpy(b + o, "fmt ", 4); put32(b + o + 4, 16);
    put16(b + o + 8, fmt); put16(b + o + 10, ch); put32(b + o + 12, rate); put32(b + o + 16, rate * ch * bits / 8);
    put16(b + o + 20, (uint16_t)(ch * bits / 8)); put16(b + o + 22, bits); o += 24;
    if (list_chunk) { memcpy(b + o, "LIST", 4); put32(b + o + 4, 5); o += 8 + 6; }   /* odd size: padded */
    memcpy(b + o, "data", 4); put32(b + o + 4, 4000); o += 8;
    put32(b + 4, (uint32_t)(o + 4000 - 8));
    return o;
}

static void test_wav(void)
{
    uint8_t b[128];
    pc_wav_t w;
    int o = wav(b, 1, 1, 48000, 16, false);
    CHECK_EQ_INT(pc_wav_parse(b, (uint32_t)o, &w), PC_WAV_OK);
    CHECK_EQ_INT(w.rate, 48000);
    CHECK_EQ_INT(w.channels, 1);
    CHECK_EQ_INT(w.data_off, 44);
    CHECK_EQ_INT(w.data_len, 4000);
    o = wav(b, 1, 2, 44100, 16, true);
    CHECK_EQ_INT(pc_wav_parse(b, (uint32_t)o, &w), PC_WAV_OK);
    CHECK_EQ_INT(w.channels, 2);
    CHECK_EQ_INT(w.data_off, 44 + 14);
    CHECK_EQ_INT(pc_wav_parse(b, 30, &w), PC_WAV_SHORT);
    wav(b, 3, 1, 48000, 32, false);
    CHECK_EQ_INT(pc_wav_parse(b, 44, &w), PC_WAV_FORMAT);     /* float */
    wav(b, 1, 1, 48000, 24, false);
    CHECK_EQ_INT(pc_wav_parse(b, 44, &w), PC_WAV_FORMAT);     /* 24-bit */
    wav(b, 0xFFFE, 1, 48000, 16, false);
    CHECK_EQ_INT(pc_wav_parse(b, 44, &w), PC_WAV_OK);         /* WAVE_FORMAT_EXTENSIBLE, 16-bit: fine */
    memcpy(b, "RIFX", 4);
    CHECK_EQ_INT(pc_wav_parse(b, 44, &w), PC_WAV_NOT_WAV);
}

/* streamed WAVs (data size 0xFFFFFFFF), truncated files, a data offset past the end, a huge chunk size */
static void test_wav_fit(void)
{
    uint8_t b[128];
    pc_wav_t w;
    int o = wav(b, 1, 2, 48000, 16, false);
    put32(b + o - 4, 0xFFFFFFFFu);
    CHECK_EQ_INT(pc_wav_parse(b, (uint32_t)o, &w), PC_WAV_OK);
    CHECK_EQ_INT(pc_wav_fit(&w, 44 + 1003), PC_WAV_OK);          /* to the end of the file, whole frames */
    CHECK_EQ_INT(w.data_len, 1000);
    CHECK_EQ_INT(pc_wav_parse(b, (uint32_t)o, &w), PC_WAV_OK);
    CHECK_EQ_INT(pc_wav_fit(&w, 44), PC_WAV_SHORT);              /* no data at all */
    CHECK_EQ_INT(pc_wav_parse(b, (uint32_t)o, &w), PC_WAV_OK);
    CHECK_EQ_INT(pc_wav_fit(&w, 30), PC_WAV_SHORT);              /* data starts past the end */
    o = wav(b, 1, 1, 48000, 16, false);
    CHECK_EQ_INT(pc_wav_parse(b, (uint32_t)o, &w), PC_WAV_OK);
    CHECK_EQ_INT(pc_wav_fit(&w, 1u << 20), PC_WAV_OK);           /* a longer file (chunks after data): as is */
    CHECK_EQ_INT(w.data_len, 4000);
    o = wav(b, 1, 1, 48000, 16, true);
    put32(b + 36 + 4, 0xFFFFFFF8u);                              /* LIST size: o + 8 + size wraps round */
    CHECK_EQ_INT(pc_wav_parse(b, (uint32_t)o, &w), PC_WAV_SHORT);
}

int main(void)
{
    RUN(test_wav_fit);
    RUN(test_note_names);
    RUN(test_txt);
    RUN(test_sfz);
    RUN(test_wav);
    return 0;
}
