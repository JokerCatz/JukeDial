// SPDX-License-Identifier: LGPL-2.1-or-later
#include <string.h>
#include "pc/pc_nowinfo.h"
#include "pc/pc_thumbbg.h"
#include "pc/pc_castq.h"
#include "pc/pc_rate.h"
#include "pc_test.h"

static char b[1024];
#define NI(...) { __VA_ARGS__, .nocard = NULL }   /* the brief's positional fields, the appended ones zero */

static void test_ni_wired_96k(void)
{
    pc_nowinfo_t ni = NI(PC_NI_WIRED, "FLAC", 96000, 24, 2900, 96000, PC_PIPE_NATIVE, 4, 1, "高取樣率需要 CPU", 96000, 32, NULL, 0);
    CHECK_EQ_INT(pc_nowinfo_text(&ni, true, b, sizeof b), 5);
    CHECK_STR(b, "來源：FLAC 96 kHz・24 bit・2.9 Mbps\n\n處理：96 kHz 原生\n\n音效：4 個執行・1 個暫停（高取樣率需要 CPU）\n\n輸出：耳機孔 96 kHz・24 bit\n\n狀況：underrun 0");
}

static void test_ni_halved_sink(void)
{
    pc_nowinfo_t ni = NI(PC_NI_A2DP, "FLAC", 96000, 24, 0, 48000, PC_PIPE_SINK, 0, 0, NULL, 0, 0, "AAC", 0);
    CHECK_EQ_INT(pc_nowinfo_text(&ni, true, b, sizeof b), 4);
    CHECK_STR(b, "來源：FLAC 96 kHz・24 bit\n\n處理：48 kHz（從 96 kHz 減半：藍牙）\n\n音效：關\n\n輸出：藍牙 AAC");
    CHECK_EQ_INT(pc_nowinfo_text(&ni, false, b, sizeof b), 4);
    CHECK_STR(b, "Source: FLAC 96 kHz · 24 bit\n\nProcessing: 48 kHz (halved from 96 kHz: Bluetooth)\n\nEffects: off\n\nOutput: Bluetooth AAC");
}

static void test_ni_mp3_en(void)
{
    pc_nowinfo_t ni = NI(PC_NI_WIRED, "MP3", 44100, 0, 320, 44100, PC_PIPE_NATIVE, 2, 0, NULL, 44100, 16, NULL, 0);
    CHECK_EQ_INT(pc_nowinfo_text(&ni, false, b, sizeof b), 5);
    CHECK_STR(b, "Source: MP3 44.1 kHz · 320 kbps\n\nProcessing: 44.1 kHz native\n\nEffects: 2 running\n\nOutput: headphone jack 44.1 kHz · 16 bit\n\nStatus: underrun 0");
}

static void test_ni_idle(void)
{
    pc_nowinfo_t ni = NI(PC_NI_WIRED, NULL, 0, 0, 0, 44100, PC_PIPE_NATIVE, 0, 0, NULL, 44100, 32, NULL, 0);
    CHECK_EQ_INT(pc_nowinfo_text(&ni, true, b, sizeof b), 2);
    CHECK_STR(b, "音效：關\n\n輸出：耳機孔 44.1 kHz・24 bit");
    CHECK_EQ_INT(pc_nowinfo_text(&ni, false, b, sizeof b), 2);
    CHECK_STR(b, "Effects: off\n\nOutput: headphone jack 44.1 kHz · 24 bit");
}

static void test_ni_rate_fmt(void)
{
    char r[24];
    const struct { uint32_t hz; const char *s; } t[] = { { 44100, "44.1 kHz" }, { 48000, "48 kHz" }, { 176400, "176.4 kHz" }, { 352800, "352.8 kHz" }, { 8000, "8 kHz" }, { 11025, "11.025 kHz" } };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) { pc_nowinfo_rate(t[i].hz, r, sizeof r); CHECK_STR(r, t[i].s); }
}

static void test_ni_truncate(void)
{
    pc_nowinfo_t ni = NI(PC_NI_WIRED, "FLAC", 96000, 24, 2900, 96000, PC_PIPE_NATIVE, 4, 1, "高取樣率需要 CPU", 96000, 32, NULL, 0);
    for (size_t n = 1; n < 120; n++) {
        char t[130];
        memset(t, 'x', sizeof t);
        pc_nowinfo_text(&ni, true, t, n);
        size_t len = strlen(t);
        CHECK(len < n);                                       /* NUL-terminated inside n */
        int pending = 0;                                      /* every multi-byte sequence is complete */
        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)t[i];
            if (pending) { CHECK((c & 0xC0) == 0x80); pending--; }
            else if (c >= 0xF0) pending = 3;
            else if (c >= 0xE0) pending = 2;
            else if (c >= 0xC0) pending = 1;
            else CHECK(c < 0x80);
        }
        CHECK_EQ_INT(pending, 0);
    }
    char t[20];
    pc_nowinfo_text(&ni, true, t, sizeof t);                  /* n = 20: 19 bytes fit */
    CHECK_STR(t, "來源：FLAC 96 kH");
    pc_nowinfo_text(&ni, true, t, 0);                         /* no room: nothing written, no crash */
}

static void test_ni_clock_two_meanings(void)
{
    /* the DAC has no clock for that rate: the plain word; the clock was refused this boot (hw_cap below the setting): says so */
    pc_nowinfo_t a = NI(PC_NI_WIRED, "FLAC", 192000, 24, 0, 96000, PC_PIPE_CLOCK, 0, 0, NULL, 96000, 32, NULL, 0, false, 96000, 96000);
    pc_nowinfo_text(&a, true, b, sizeof b);
    CHECK(strstr(b, "處理：96 kHz（從 192 kHz 減半：時脈）") != NULL);
    pc_nowinfo_t c = a;
    c.pipe_rate = 48000; c.out_rate = 48000; c.hw_cap = 48000; c.cap = 96000;
    pc_nowinfo_text(&c, true, b, sizeof b);
    CHECK(strstr(b, "處理：48 kHz（從 192 kHz 減半：時脈（本次開機降為 48 kHz））") != NULL);
    pc_nowinfo_text(&c, false, b, sizeof b);
    CHECK(strstr(b, "Processing: 48 kHz (halved from 192 kHz: clock (lowered to 48 kHz this boot))") != NULL);
    pc_nowinfo_text(&a, false, b, sizeof b);
    CHECK(strstr(b, "Processing: 96 kHz (halved from 192 kHz: clock)") != NULL);
}

static void test_ni_status_line(void)
{
    pc_nowinfo_t ni = NI(PC_NI_WIRED, "FLAC", 44100, 16, 0, 44100, PC_PIPE_NATIVE, 0, 0, NULL, 0, 0, NULL, 3);
    ni.released = true;                                       /* the channel is released with no sink on: said on the one 狀況 line, no rate on 輸出 */
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "\n\n輸出：耳機孔\n\n狀況：underrun 3・I2S 已釋放") != NULL);
    pc_nowinfo_text(&ni, false, b, sizeof b);
    CHECK(strstr(b, "\n\nStatus: underrun 3 · I2S released") != NULL);
    ni.fmt = NULL; ni.src_rate = 0; ni.underruns = 0;         /* idle but released: the line stays (a fault, not a play state) */
    CHECK_EQ_INT(pc_nowinfo_text(&ni, true, b, sizeof b), 3);
    CHECK(strstr(b, "\n\n狀況：I2S 已釋放") != NULL);
}

/* hires T8c: covers are not decoded while a source above 48 kHz plays; the 狀況 line says so, also on a Bluetooth output (no underrun there) */
static void test_ni_covers_paused(void)
{
    pc_nowinfo_t ni = NI(PC_NI_WIRED, "FLAC", 96000, 24, 0, 96000, PC_PIPE_NATIVE, 0, 0, NULL, 96000, 32, NULL, 2);
    ni.covers_paused = true;
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "\n\n狀況：underrun 2・封面建置暫停（高取樣率）") != NULL);
    pc_nowinfo_text(&ni, false, b, sizeof b);
    CHECK(strstr(b, "\n\nStatus: underrun 2 · covers paused (hi-res)") != NULL);
    pc_nowinfo_t bt = NI(PC_NI_A2DP, "FLAC", 96000, 24, 0, 48000, PC_PIPE_SINK, 0, 0, NULL, 0, 0, "AAC", 0);
    bt.covers_paused = true;
    CHECK_EQ_INT(pc_nowinfo_text(&bt, true, b, sizeof b), 5);
    CHECK(strstr(b, "\n\n輸出：藍牙 AAC\n\n狀況：封面建置暫停（高取樣率）") != NULL);
    bt.covers_paused = false;                                 /* not paused: no 狀況 line on Bluetooth, as before */
    CHECK_EQ_INT(pc_nowinfo_text(&bt, true, b, sizeof b), 4);
    ni.covers_paused = false;
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "封面") == NULL);
}

static void test_ni_nocard_speaker_cast(void)
{
    pc_nowinfo_t n = NI(PC_NI_WIRED, NULL, 0, 0, 0, 44100, PC_PIPE_NATIVE, 0, 0, NULL, 44100, 32, NULL, 0);
    CHECK_EQ_INT(pc_nowinfo_text(&n, true, b, sizeof b), 2);   /* no reason yet (still mounting at boot): the 來源 line is left out */
    CHECK(strstr(b, "來源") == NULL);
    n.nocard = "nofs";
    CHECK_EQ_INT(pc_nowinfo_text(&n, true, b, sizeof b), 3);
    CHECK(strncmp(b, "來源：無卡（nofs）\n\n音效：關", 4) == 0);
    CHECK(strstr(b, "來源：無卡（nofs）\n\n") == b);
    pc_nowinfo_text(&n, false, b, sizeof b);
    CHECK(strstr(b, "Source: no card (nofs)\n\n") == b);
    n.nocard = "stuck";                                        /* answered but cannot be set up: not "no card" */
    pc_nowinfo_text(&n, false, b, sizeof b);
    CHECK(strstr(b, "Source: card not responding (power off and on, or re-insert)\n\n") == b);
    CHECK(strstr(b, "no card") == NULL);
    pc_nowinfo_t s = NI(PC_NI_SPEAKER, "SBC", 44100, 0, 0, 44100, PC_PIPE_NATIVE, 0, 0, NULL, 44100, 32, NULL, 2);
    pc_nowinfo_text(&s, true, b, sizeof b);
    CHECK(strstr(b, "來源：藍牙（手機）\n\n處理：44.1 kHz 原生") == b);
    CHECK(strstr(b, "輸出：耳機孔 44.1 kHz・24 bit\n\n狀況：underrun 2") != NULL);
    pc_nowinfo_text(&s, false, b, sizeof b);
    CHECK(strstr(b, "Source: Bluetooth (phone)\n\n") == b);
    pc_nowinfo_t c = NI(PC_NI_CAST, "FLAC", 96000, 24, 0, 48000, PC_PIPE_SINK, 1, 0, NULL, 0, 0, "48k 立體聲", 0);
    pc_nowinfo_text(&c, true, b, sizeof b);
    CHECK(strstr(b, "輸出：廣播 48k 立體聲") != NULL && strstr(b, "狀況") == NULL);
    pc_nowinfo_text(&c, false, b, sizeof b);
    CHECK(strstr(b, "Output: broadcast 48k stereo") == NULL);   /* the link text is the caller's */
    CHECK(strstr(b, "Output: broadcast 48k 立體聲") != NULL);
}

static void test_ni_speaker_title(void)
{
    /* AVRCP 1.6 step 1: the phone's title after the source word, once it is known; nothing more when it is not (or empty) */
    pc_nowinfo_t s = NI(PC_NI_SPEAKER, "SBC", 44100, 0, 0, 44100, PC_PIPE_NATIVE, 0, 0, NULL, 44100, 32, NULL, 0);
    s.peer_title = "Paper Planes";
    pc_nowinfo_text(&s, true, b, sizeof b);
    CHECK(strstr(b, "來源：藍牙（手機）・Paper Planes\n\n") == b);
    pc_nowinfo_text(&s, false, b, sizeof b);
    CHECK(strstr(b, "Source: Bluetooth (phone) · Paper Planes\n\n") == b);
    s.peer_title = "";
    pc_nowinfo_text(&s, true, b, sizeof b);
    CHECK(strstr(b, "來源：藍牙（手機）\n\n") == b);
    s.peer_title = NULL;
    pc_nowinfo_text(&s, true, b, sizeof b);
    CHECK(strstr(b, "來源：藍牙（手機）\n\n") == b);
    pc_nowinfo_t w = NI(PC_NI_WIRED, "FLAC", 44100, 16, 0, 44100, PC_PIPE_NATIVE, 0, 0, NULL, 44100, 32, NULL, 0);
    w.peer_title = "Paper Planes";                            /* only the speaker mode's line carries it */
    pc_nowinfo_text(&w, true, b, sizeof b);
    CHECK(strstr(b, "Paper Planes") == NULL);
    s.peer_title = "Paper Planes"; s.fmt = NULL;              /* nothing plays: no 來源 line at all */
    pc_nowinfo_text(&s, true, b, sizeof b);
    CHECK(strstr(b, "來源") == NULL);
}

static void test_ni_fx_lines(void)
{
    pc_nowinfo_t ni = NI(PC_NI_WIRED, "MP3", 44100, 0, 1500, 44100, PC_PIPE_NATIVE, 0, 3, "電池供電", 44100, 32, NULL, 0);
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "來源：MP3 44.1 kHz・1.5 Mbps") == b);
    CHECK(strstr(b, "音效：0 個執行・3 個暫停（電池供電）") != NULL);
    ni.fx_why = NULL;
    pc_nowinfo_text(&ni, false, b, sizeof b);
    CHECK(strstr(b, "Effects: 0 running · 3 paused\n\n") != NULL);
}

static void test_cast_q_label(void)
{
    CHECK_STR(pc_cast_q_label(0, true), "24k 立體聲");   CHECK_STR(pc_cast_q_label(0, false), "24k stereo");
    CHECK_STR(pc_cast_q_label(2, true), "48k 高位元率"); CHECK_STR(pc_cast_q_label(2, false), "48k high");
    CHECK_STR(pc_cast_q_label(1, true), "48k 立體聲");   CHECK_STR(pc_cast_q_label(1, false), "48k stereo");
    CHECK_STR(pc_cast_q_label(3, false), "48k stereo");     /* out of range reads as the default */
}

/* hires T8e: the background pass on the 狀況 line, with its numbers: building (n/total, %) or paused with the reason; done, manual or no pass:
 * nothing (only the hi-res pause of the requests, as before) */
static void test_ni_covers_progress(void)
{
    pc_nowinfo_t ni = NI(PC_NI_WIRED, "MP3", 44100, 0, 320, 44100, PC_PIPE_NATIVE, 0, 0, NULL, 44100, 32, NULL, 0);
    ni.covers_state = PC_TBG_ST_BUILDING; ni.covers_done = 23; ni.covers_total = 70;
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "\n\n狀況：underrun 0・封面建置中 23/70（32%）") != NULL);
    pc_nowinfo_text(&ni, false, b, sizeof b);
    CHECK(strstr(b, "\n\nStatus: underrun 0 · covers building 23/70 (32%)") != NULL);
    ni.covers_state = PC_TBG_ST_HELD; ni.covers_why = PC_TBG_UNDERRUN;
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "\n\n狀況：underrun 0・封面建置暫停（播放不順）23/70") != NULL);
    pc_nowinfo_text(&ni, false, b, sizeof b);
    CHECK(strstr(b, "\n\nStatus: underrun 0 · covers paused (playback stutter) 23/70") != NULL);
    ni.covers_why = PC_TBG_HIRES; ni.covers_paused = true;   /* held for hi-res: one fragment, with the numbers */
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "\n\n狀況：underrun 0・封面建置暫停（高取樣率）23/70") != NULL);
    CHECK(strstr(strstr(b, "封面") + 3, "封面") == NULL);
    ni.covers_state = PC_TBG_ST_DONE;                        /* the pass is done: the requests' hi-res pause as before */
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "\n\n狀況：underrun 0・封面建置暫停（高取樣率）") != NULL && strstr(b, "23/70") == NULL);
    ni.covers_paused = false;
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "封面") == NULL);
    ni.covers_state = PC_TBG_ST_MANUAL;                      /* manual: not a pause, nothing to say */
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "封面") == NULL);
    ni.covers_state = PC_TBG_ST_DONE; ni.covers_tidy = true;  /* fix 0: everything in, a compaction waits for a stop: said */
    pc_nowinfo_text(&ni, true, b, sizeof b);
    CHECK(strstr(b, "\n\n狀況：underrun 0・封面完成・整理待停止") != NULL);
    pc_nowinfo_text(&ni, false, b, sizeof b);
    CHECK(strstr(b, "\n\nStatus: underrun 0 · covers done · tidy-up waits for a stop") != NULL);
    ni.covers_tidy = false;
    pc_nowinfo_t idle = NI(PC_NI_WIRED, NULL, 0, 0, 0, 44100, PC_PIPE_NATIVE, 0, 0, NULL, 44100, 32, NULL, 0);
    idle.covers_state = PC_TBG_ST_BUILDING; idle.covers_done = 1; idle.covers_total = 3;   /* nothing plays: the 狀況 line for the covers alone */
    CHECK_EQ_INT(pc_nowinfo_text(&idle, true, b, sizeof b), 3);
    CHECK(strstr(b, "\n\n狀況：封面建置中 1/3（33%）") != NULL);
}

int main(void)
{
    RUN(test_ni_wired_96k);
    RUN(test_ni_halved_sink);
    RUN(test_ni_mp3_en);
    RUN(test_ni_idle);
    RUN(test_ni_rate_fmt);
    RUN(test_ni_truncate);
    RUN(test_ni_clock_two_meanings);
    RUN(test_ni_status_line);
    RUN(test_ni_covers_paused);
    RUN(test_ni_covers_progress);
    RUN(test_ni_nocard_speaker_cast);
    RUN(test_ni_speaker_title);
    RUN(test_ni_fx_lines);
    RUN(test_cast_q_label);
    return 0;
}
