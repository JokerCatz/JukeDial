// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_outstat.h"
#include "pc_test.h"
#include <stdio.h>
#include <string.h>

/* BTAUDIO: the output chain, its reasons and its three-language text. The cases are the owner's examples (2026-11-17). */
#define TW PC_LANG_TW, true
#define EN PC_LANG_EN, false

static pc_os_wired_in_t wi(const pc_dac_caps_t *c, uint32_t src, uint32_t cap, pc_pipe_t p)
{
    pc_os_wired_in_t w;
    memset(&w, 0, sizeof w);
    w.src_rate = src; w.src_bits = 24; w.caps = c; w.cap = cap; w.hard = 384000; w.hw_cap = 384000; w.plan = p; w.dac_bits = 32;
    return w;
}
static void wired_text(const pc_os_wired_in_t *w, char *b, size_t n, bool tw)
{
    pc_outstat_t os;
    memset(&os, 0, sizeof os);
    os.fmt = "FLAC";
    pc_os_wired_plan(w, &os);
    if (tw) pc_os_live_wired(b, n, &os, TW); else pc_os_live_wired(b, n, &os, EN);
}

static void test_wired_examples(void)
{
    char b[256];
    /* FLAC 384k on FakePod, defaults: passthrough, source */
    pc_os_wired_in_t w = wi(&pc_dac_caps_fakepod, 384000, 384000, (pc_pipe_t){ 384000, 0, PC_PIPE_NATIVE });
    pc_outstat_t os;
    memset(&os, 0, sizeof os);
    pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.proc, PC_PROC_PASS); CHECK_EQ_INT(os.rate.why, PC_WHY_NONE);
    wired_text(&w, b, sizeof b, true);
    CHECK(strstr(b, "直通") && strstr(b, "（原始）"));
    /* halve 192: 384k -> 192k (setting: halve 192k) */
    w = wi(&pc_dac_caps_fakepod, 384000, 192000, (pc_pipe_t){ 192000, 1, PC_PIPE_CAP });
    memset(&os, 0, sizeof os); pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.proc, PC_PROC_HALVED); CHECK_EQ_INT(os.rate.why, PC_WHY_SETTING); CHECK_EQ_INT(os.rate.limit, 192000);
    wired_text(&w, b, sizeof b, true);
    CHECK(strstr(b, "384k") && strstr(b, "192k") && strstr(b, "（設定：折半 192k）"));
    wired_text(&w, b, sizeof b, false);
    CHECK(strstr(b, " (setting: halve at 192k)"));
    /* round default: a 96k file at the 48k ceiling */
    w = wi(&pc_dac_caps_round, 96000, 48000, (pc_pipe_t){ 48000, 1, PC_PIPE_CAP });
    wired_text(&w, b, sizeof b, true);
    CHECK(strstr(b, "（設定：有線上限 48k）"));
    /* MP3 22.05k on FakePod: upsampled, DAC minimum 32k */
    w = wi(&pc_dac_caps_fakepod, 22050, 384000, (pc_pipe_t){ 44100, 0, PC_PIPE_NATIVE });
    w.up_from = 22050;
    memset(&os, 0, sizeof os); pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.proc, PC_PROC_UPSAMPLED); CHECK_EQ_INT(os.rate.why, PC_WHY_DAC); CHECK_EQ_INT(os.rate.limit, 32000);
    wired_text(&w, b, sizeof b, true);
    CHECK(strstr(b, "升頻") && strstr(b, "（DAC：最低 32k，升頻）"));
    /* 11.025k: refused by the firmware (below 16k) */
    w = wi(&pc_dac_caps_fakepod, 11025, 384000, (pc_pipe_t){ 0, 0, PC_PIPE_NATIVE });
    memset(&os, 0, sizeof os); pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.proc, PC_PROC_REFUSED); CHECK_EQ_INT(os.rate.why, PC_WHY_BUILD); CHECK_EQ_INT(os.rate.limit, 16000);
    wired_text(&w, b, sizeof b, true);
    CHECK(strstr(b, "無法播放") && strstr(b, "（韌體：低於 16k）"));
    /* a source above the hard limit */
    w = wi(&pc_dac_caps_fakepod, 384000, 384000, (pc_pipe_t){ 0, 0, PC_PIPE_NATIVE });
    w.hard = 192000; w.refused_hard = true;
    memset(&os, 0, sizeof os); pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.rate.why, PC_WHY_SETTING); CHECK_EQ_INT(os.rate.lim, PC_LIM_HARD);
    /* the DAC's clock ceiling after a refused clock */
    w = wi(&pc_dac_caps_round, 192000, 384000, (pc_pipe_t){ 96000, 1, PC_PIPE_CLOCK });
    w.hw_cap = 96000;
    memset(&os, 0, sizeof os); pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.rate.why, PC_WHY_DAC); CHECK_EQ_INT(os.rate.limit, 96000);
    /* dac_bits 16 against a 24-bit file: the setting binds the word */
    w = wi(&pc_dac_caps_round, 48000, 48000, (pc_pipe_t){ 48000, 0, PC_PIPE_NATIVE });
    w.dac_bits = 16;
    memset(&os, 0, sizeof os); pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.bits.value, 16); CHECK_EQ_INT(os.bits.why, PC_WHY_SETTING);
}

static void test_slot(void)
{
    CHECK_EQ_INT(pc_slot_bits(PC_SLOT24_OFF, 384000, 32), 32);
    CHECK_EQ_INT(pc_slot_bits(PC_SLOT24_352, 352800, 32), 24);
    CHECK_EQ_INT(pc_slot_bits(PC_SLOT24_352, 192000, 32), 32);
    CHECK_EQ_INT(pc_slot_bits(PC_SLOT24_192, 192000, 32), 24);
    CHECK_EQ_INT(pc_slot_bits(PC_SLOT24_192, 176400, 32), 32);
    CHECK_EQ_INT(pc_slot_bits(PC_SLOT24_ALL, 44100, 32), 24);
    CHECK_EQ_INT(pc_slot_bits(PC_SLOT24_ALL, 44100, 16), 16);          /* the 16-bit A/B path keeps its own slot */
    CHECK_EQ_INT(pc_slot24_mode(9), PC_SLOT24_OFF);
    CHECK_EQ_INT(pc_slot_bclk(384000, 24), 18432000);                  /* 25 % under the slave limit of 24.576 MHz */
    CHECK_EQ_INT(pc_slot_bclk(384000, 32), 24576000);
    /* the plan carries it with its reason */
    pc_os_wired_in_t w = wi(&pc_dac_caps_fakepod, 384000, 384000, (pc_pipe_t){ 384000, 0, PC_PIPE_NATIVE });
    w.slot_mode = PC_SLOT24_352;
    pc_outstat_t os;
    memset(&os, 0, sizeof os);
    pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.slot.value, 24); CHECK_EQ_INT(os.slot.why, PC_WHY_SETTING);
    char b[256];
    pc_os_live_wired(b, sizeof b, &os, TW);
    CHECK(strstr(b, "24-bit 槽"));
    pc_os_block(b, sizeof b, &os, TW);
    CHECK(strstr(b, "槽寬：24-bit（設定）"));
    w.slot_mode = PC_SLOT24_OFF;
    memset(&os, 0, sizeof os); pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.slot.value, 32); CHECK_EQ_INT(os.slot.why, PC_WHY_NONE);
}

static void test_pack24(void)
{
    int32_t a[4] = { 0x12345600, (int32_t)0x80000000, 0x7FFFFF00, -256 };
    uint8_t o[12];
    pc_pack_s24(a, o, 4);
    const uint8_t want[12] = { 0x56, 0x34, 0x12, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF };
    CHECK(!memcmp(o, want, 12));
    pc_pack_s24(a, (uint8_t *)a, 4);                                   /* in place gives the same bytes */
    CHECK(!memcmp(a, want, 12));
}

static pc_os_bt_in_t bi(unsigned sink, bool ldac)
{
    pc_os_bt_in_t in;
    memset(&in, 0, sizeof in);
    in.connected = true; in.build_ldac = ldac; in.sink_codecs = sink;
    in.pref_codec = PC_BT_PREF_LDAC; in.pref_rate = 96000; in.pref_bits = 24; in.ldac_kbps = 990;
    for (int i = 0; i < PC_BTC_N; i++) { in.sink_rate[i] = 48000; in.sink_bits[i] = i <= PC_BTC_APTX_HD ? 24 : 16; }
    in.step_codec = PC_BTC_NONE;
    return in;
}

static void test_bt_caps(void)
{
    pc_outstat_t os;
    char b[256];
    /* LDAC chosen, AAC-only buds: AAC (headset: max AAC) */
    pc_os_bt_in_t in = bi((1u << PC_BTC_AAC) | (1u << PC_BTC_SBC), true);
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.codec.value, PC_BTC_AAC); CHECK_EQ_INT(os.codec.why, PC_WHY_SINK); CHECK_EQ_INT(os.codec.limit, PC_BTC_AAC);
    CHECK_EQ_INT(os.best_codec, PC_BTC_AAC);
    pc_os_info_best(b, sizeof b, &os, TW);
    CHECK_STR(b, "耳機最高：AAC 48k / 16-bit");
    pc_os_info_actual(b, sizeof b, &os, TW);
    CHECK_STR(b, "實際：AAC 48k / 16-bit");
    pc_os_live_bt(b, sizeof b, &os, TW);
    CHECK(strstr(b, "正在傳輸：AAC") && strstr(b, "（耳機：最高 AAC）"));
    /* a 44.1k preference caps the rate: setting */
    in.pref_rate = 44100;
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.lrate.value, 44100); CHECK_EQ_INT(os.lrate.why, PC_WHY_SETTING);
    pc_os_info_actual(b, sizeof b, &os, TW);
    CHECK_STR(b, "實際：AAC 44.1k / 16-bit");
    /* not connected */
    in.connected = false;
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    pc_os_live_bt(b, sizeof b, &os, TW); CHECK_STR(b, "未連線");
    pc_os_info_best(b, sizeof b, &os, TW); CHECK_STR(b, "耳機最高：未連線");
    pc_os_info_actual(b, sizeof b, &os, EN); CHECK_STR(b, "Actual: not connected");
    /* a non-LDAC build with the LDAC preference: capped to AAC, the firmware says why */
    in = bi((1u << PC_BTC_LDAC) | (1u << PC_BTC_AAC) | (1u << PC_BTC_SBC), false);
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.codec.value, PC_BTC_AAC); CHECK_EQ_INT(os.codec.why, PC_WHY_BUILD); CHECK_EQ_INT(os.codec.limit, PC_BTC_LDAC);
    pc_os_build_note(b, sizeof b, PC_BT_PREF_LDAC, false, TW); CHECK_STR(b, "此版韌體不含 LDAC");
    pc_os_build_note(b, sizeof b, PC_BT_PREF_LDAC, true, TW); CHECK_STR(b, "");
    pc_os_build_note(b, sizeof b, PC_BT_PREF_AAC, false, TW); CHECK_STR(b, "");
    pc_os_build_note(b, sizeof b, PC_BT_PREF_LDAC, false, EN); CHECK_STR(b, "This firmware has no LDAC");
    /* a codec preference below what both sides have: the setting binds and says so */
    in = bi((1u << PC_BTC_LDAC) | (1u << PC_BTC_AAC) | (1u << PC_BTC_SBC), true);
    in.pref_codec = PC_BT_PREF_AAC;
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.codec.value, PC_BTC_AAC); CHECK_EQ_INT(os.codec.why, PC_WHY_SETTING);
    CHECK_EQ_INT(os.best_codec, PC_BTC_LDAC);                          /* the headset's own best is still LDAC */
    /* the sink's capabilities unknown (not reported): no limit from the sink */
    in = bi(0, true);
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.codec.value, PC_BTC_LDAC); CHECK_EQ_INT(os.codec.why, PC_WHY_NONE);
}

static void test_bt_bits_rate_chan(void)
{
    pc_outstat_t os;
    /* LDAC build, LDAC sink at 96k / 24-bit: this build still carries 48k and 16-bit (firmware) */
    pc_os_bt_in_t in = bi(1u << PC_BTC_LDAC, true);
    in.sink_rate[PC_BTC_LDAC] = 96000; in.sink_bits[PC_BTC_LDAC] = 24;
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.codec.value, PC_BTC_LDAC); CHECK_EQ_INT(os.codec.why, PC_WHY_NONE);
    CHECK_EQ_INT(os.lrate.value, 48000); CHECK_EQ_INT(os.lrate.why, PC_WHY_BUILD);
    CHECK_EQ_INT(os.lbits.value, 16); CHECK_EQ_INT(os.lbits.why, PC_WHY_BUILD);
    CHECK_EQ_INT(os.best_rate, 96000); CHECK_EQ_INT(os.best_bits, 24);
    /* the sink and the build both bind the rate: the sink (tightest tie) is the reason, the build is listed too */
    in = bi(1u << PC_BTC_LDAC, true);
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.lrate.why, PC_WHY_SINK); CHECK(os.lrate.also & (1u << PC_WHY_BUILD));
    /* the source decides what counts as missing: a 44.1k / 16-bit file is not limited by a 48k / 16-bit link, a 96k / 24-bit one is */
    in = bi((1u << PC_BTC_AAC) | (1u << PC_BTC_SBC), true);
    in.src_rate = 44100; in.src_bits = 16;
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.lrate.why, PC_WHY_NONE); CHECK_EQ_INT(os.lbits.why, PC_WHY_NONE);
    in.src_rate = 96000; in.src_bits = 24;
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.lrate.why, PC_WHY_SINK); CHECK_EQ_INT(os.lbits.why, PC_WHY_SINK);
    /* joint stereo is an SBC mode */
    in = bi(1u << PC_BTC_SBC, true);
    in.pref_chan = 1;
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.chan, PC_CHAN_JOINT);
    in = bi((1u << PC_BTC_SBC) | (1u << PC_BTC_AAC), true);
    in.pref_chan = 1;
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.chan, PC_CHAN_STEREO);                             /* AAC: plain stereo */
    CHECK_EQ_INT(pc_bt_rate_pick(48000), 48000); CHECK_EQ_INT(pc_bt_rate_pick(50000), 48000); CHECK_EQ_INT(pc_bt_rate_pick(10), 44100);
    CHECK_EQ_INT(pc_bt_rate_pick(96000), 96000);
}

static void test_ldac_kbps(void)
{
    pc_outstat_t os;
    char b[256];
    pc_os_bt_in_t in = bi(1u << PC_BTC_LDAC, true);
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.kbps.value, 990); CHECK_EQ_INT(os.kbps.why, PC_WHY_NONE);
    in.live_kbps = 660;                                                /* the adaptive rate stepped down */
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.kbps.value, 660); CHECK_EQ_INT(os.kbps.why, PC_WHY_SIGNAL); CHECK_EQ_INT(os.kbps.limit, 990);
    pc_os_live_bt(b, sizeof b, &os, TW);
    CHECK(strstr(b, "990→660 kbps") && strstr(b, "（訊號：自動降速）"));
    in.live_kbps = 0; in.ldac_kbps = 492;                              /* the setting */
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.kbps.value, 492); CHECK_EQ_INT(os.kbps.why, PC_WHY_SETTING);
    in.ldac_kbps = 990; in.wifi_on = true;                             /* Wi-Fi on: 660 at most, a radio reason */
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.kbps.value, 660); CHECK_EQ_INT(os.kbps.why, PC_WHY_SIGNAL);
}

static void test_block_and_modes(void)
{
    pc_outstat_t os;
    char b[1024];
    pc_os_wired_in_t w = wi(&pc_dac_caps_fakepod, 384000, 192000, (pc_pipe_t){ 192000, 1, PC_PIPE_CAP });
    memset(&os, 0, sizeof os);
    os.fmt = "FLAC"; os.src_bits = 24;
    pc_os_wired_plan(&w, &os);
    os.dac_db_valid = true; os.dac_db_x10 = -60;
    CHECK_EQ_INT(pc_os_block(b, sizeof b, &os, TW), 6);               /* 輸出 來源 處理 位元 槽寬 音量 */
    CHECK(strstr(b, "輸出：有線\n來源：FLAC 384k / 24-bit\n處理：折半 384k→192k（設定：折半 192k）"));
    CHECK(strstr(b, "音量：DAC -6.0 dB"));
    /* speed off by the mode and effects paused: each says why, in the screen's language */
    os.speed.value = 125; os.speed.why = PC_WHY_MODE; os.speed.lim = PC_LIM_ABOVE; os.speed.limit = 48000;
    os.fx.value = 2; os.fx.why = PC_WHY_BUSY; os.fx.lim = PC_LIM_NONE;
    pc_os_block(b, sizeof b, &os, TW);
    CHECK(strstr(b, "倍速：關（模式：> 48k）") && strstr(b, "音效：暫停（忙碌）"));
    pc_os_block(b, sizeof b, &os, EN);
    CHECK(strstr(b, "Speed: off (mode: > 48k)") && strstr(b, "Effects: paused (busy)"));
    pc_os_block(b, sizeof b, &os, PC_LANG_CN, true);
    CHECK(strstr(b, "倍速：关（模式：> 48k）") && strstr(b, "音效：暂停（忙碌）"));
    /* a Bluetooth block lists every stage with its reason; not connected says so; nothing: no lines */
    pc_os_bt_in_t in = bi((1u << PC_BTC_AAC) | (1u << PC_BTC_SBC), true);
    memset(&os, 0, sizeof os);
    os.fmt = "MP3"; os.src_rate = 44100;
    pc_os_bt_plan(&in, &os);
    pc_os_block(b, sizeof b, &os, TW);
    CHECK(strstr(b, "輸出：藍牙耳機") && strstr(b, "編碼：AAC（耳機：最高 AAC") && strstr(b, "取樣率：48k") && strstr(b, "位元：16-bit"));
    memset(&os, 0, sizeof os);
    CHECK_EQ_INT(pc_os_block(b, sizeof b, &os, TW), 0);
    /* a tiny buffer is cut, never overrun, always terminated */
    char t[8];
    memset(&os, 0, sizeof os); os.kind = PC_OS_WIRED; os.fmt = "FLAC";
    pc_os_block(t, sizeof t, &os, TW);
    CHECK(strlen(t) < sizeof t);
    CHECK_STR(pc_why_tag(PC_WHY_DAC, TW), "DAC");
    CHECK_STR(pc_why_tag(PC_WHY_BATTERY, TW), "電量");
}

/* FIX4: the real slot width, the negotiated Bluetooth link, a bare passthrough never claimed while speed / effects run, a crossfade-only gate */
static void test_fix4(void)
{
    char b[256];
    pc_os_wired_in_t w = wi(&pc_dac_caps_fakepod, 384000, 384000, (pc_pipe_t){ 384000, 0, PC_PIPE_NATIVE });
    pc_outstat_t os;
    w.slot_mode = PC_SLOT24_ALL; w.slot_real = 32;                       /* the setting says 24, the channel runs 32: 32 is shown */
    memset(&os, 0, sizeof os); pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.slot.value, 32);
    w.slot_real = 24;
    memset(&os, 0, sizeof os); pc_os_wired_plan(&w, &os);
    CHECK_EQ_INT(os.slot.value, 24);
    w.slot_real = 0; w.slot_mode = PC_SLOT24_OFF;
    memset(&os, 0, sizeof os); os.fmt = "FLAC"; pc_os_wired_plan(&w, &os);
    pc_os_live_wired(b, sizeof b, &os, TW);
    CHECK(strstr(b, "直通（原始）"));                                    /* nothing else runs: the plain words */
    os.speed.value = 125; os.fx.value = 3;
    pc_os_live_wired(b, sizeof b, &os, TW);
    CHECK(!strstr(b, "・直通") && strstr(b, "取樣率直通") && strstr(b, "倍速 125%") && strstr(b, "音效 3"));
    pc_os_block(b, sizeof b, &os, TW);
    CHECK(strstr(b, "處理：取樣率直通") && strstr(b, "倍速：125%") && strstr(b, "音效：3 個執行中"));
    pc_os_live_wired(b, sizeof b, &os, EN);
    CHECK(strstr(b, "rate passthrough") && strstr(b, "speed 125%"));
    /* a crossfade alone, gated off (speed 100 %): the line is there */
    memset(&os, 0, sizeof os); os.fmt = "FLAC"; pc_os_wired_plan(&w, &os);
    os.speed.value = 100; os.speed.why = PC_WHY_MODE; os.speed.lim = PC_LIM_TEXT; snprintf(os.speed_note, sizeof os.speed_note, "x");
    pc_os_block(b, sizeof b, &os, TW);
    CHECK(strstr(b, "淡入淡出：關"));
    /* Bluetooth: the link's codec / rate / channel mode, never the prediction; a failed codec is not on offer */
    pc_os_bt_in_t in = bi((1u << PC_BTC_LDAC) | (1u << PC_BTC_AAC) | (1u << PC_BTC_SBC), true);
    in.failed = 1u << PC_BTC_LDAC;
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.codec.value, PC_BTC_AAC); CHECK_EQ_INT(os.codec.why, PC_WHY_SINK);
    in.failed = 0; in.neg = true; in.neg_codec = PC_BTC_SBC; in.neg_rate = 44100; in.neg_chan = PC_CHAN_JOINT; in.pref_chan = 0;
    memset(&os, 0, sizeof os); pc_os_bt_plan(&in, &os);
    CHECK_EQ_INT(os.codec.value, PC_BTC_SBC); CHECK_EQ_INT(os.lrate.value, 44100); CHECK_EQ_INT(os.chan, PC_CHAN_JOINT);
    pc_os_info_actual(b, sizeof b, &os, TW);
    CHECK(strstr(b, "SBC 44.1k"));
    pc_os_info_next(b, sizeof b, &os, TW);
    CHECK(strstr(b, "下次連線：SBC"));
}

int main(void)
{
    RUN(test_wired_examples);
    RUN(test_slot);
    RUN(test_pack24);
    RUN(test_bt_caps);
    RUN(test_bt_bits_rate_chan);
    RUN(test_ldac_kbps);
    RUN(test_block_and_modes);
    RUN(test_fix4);
    return 0;
}
