// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_btcodec.h"
#include "pc/pc_i18n.h"
#include "pc/pc_lowrate.h"
#include "pc/pc_rate.h"
#ifdef __cplusplus
extern "C" {
#endif

/* BTAUDIO: the output chain of the track that plays, wired or wireless, as one status struct and one text formatter.
 * Every stage that can come out below the best possible says WHY: the binding constraint (a tag) and its limit. The places that DECIDE a stage
 * (audio_svc plan_set, bt_svc negotiation, speed / effects gating) fill pc_outstat_t through the planners below; the settings live lines, the
 * Bluetooth / wired info lines and the details page's 輸出 block are printed by the formatter here (three languages), never a second copy.
 * Pure: no RTOS, no BSP, no state. Options are PREFERENCES / CEILINGS: the actual value is the best the preference, the sink and this build all allow. */

typedef enum {
    PC_WHY_NONE,      /* equal to the source (原始) */
    PC_WHY_FILE,      /* 檔案: the source itself (rate / bits / channels / format) */
    PC_WHY_SETTING,   /* 設定: a user setting (hard limit, halve threshold, dac_max_rate, codec preference, LDAC quality, link rate) */
    PC_WHY_SINK,      /* 耳機: the sink's capabilities (A2DP capability exchange) */
    PC_WHY_DAC,       /* DAC: the board's DAC range (CS43131 32-384k, PCM5102A tiers) */
    PC_WHY_BUILD,     /* 韌體: this build (no LDAC; < 16 kHz refused) */
    PC_WHY_SIGNAL,    /* 訊號: the radio at run time (LDAC / aptX adaptive bit rate stepped down) */
    PC_WHY_BUSY,      /* 忙碌: CPU honest-degrade (effects paused) */
    PC_WHY_MODE,      /* 模式: bt_mode / broadcast (effects limited, speed off above 48k) */
    PC_WHY_BATTERY,   /* 電量: reserved for the later battery rules */
    PC_WHY_N
} pc_why_t;
const char *pc_why_tag(int why, pc_lang_t l, bool cjk);        /* "原始" "檔案" "設定" "耳機" "DAC" "韌體" "訊號" "忙碌" "模式" "電量" (and 简中 / English) */

/* how a stage's limit is worded and printed */
typedef enum {
    PC_LIM_NONE,
    PC_LIM_HALVE,       /* limit = rate: "折半 192k" (FakePod: dac_max_rate is the halve threshold) */
    PC_LIM_CEIL,        /* rate: "有線上限 48k" (round board: dac_max_rate is the wired ceiling) */
    PC_LIM_HARD,        /* rate: "硬上限 192k" (dac_hard_rate) */
    PC_LIM_MINRATE,     /* rate: "最低 32k，升頻" */
    PC_LIM_CLOCK,       /* rate: "DAC 時脈上限 96k" */
    PC_LIM_BURST,       /* rate: "格式讀取上限 96k" */
    PC_LIM_BELOW,       /* rate: "低於 16k" */
    PC_LIM_MAXRATE,     /* rate: "上限 48k" */
    PC_LIM_MAXCODEC,    /* pc_btc_t: "最高 AAC" */
    PC_LIM_NOCODEC,     /* pc_btc_t: "此版不含 LDAC" */
    PC_LIM_PREFCODEC,   /* pc_btc_t: "偏好 AAC" */
    PC_LIM_MAXBITS,     /* bits: "上限 16-bit" */
    PC_LIM_ABR,         /* kbps: "自動降速" (the value is printed from -> to) */
    PC_LIM_ABOVE,       /* rate: "> 48k" */
    PC_LIM_TEXT         /* the stage's note (already worded by the decider, the screen's language) */
} pc_lim_t;

/* a decided value, its reason and the limit that binds; also = every other reason that binds too (bit 1 << pc_why_t) */
typedef struct { uint32_t value, limit; uint8_t why, lim; uint16_t also; } pc_stage_t;

typedef enum { PC_OS_NONE, PC_OS_WIRED, PC_OS_A2DP, PC_OS_CAST } pc_os_kind_t;
typedef enum { PC_PROC_NONE, PC_PROC_PASS, PC_PROC_HALVED, PC_PROC_UPSAMPLED, PC_PROC_REFUSED, PC_PROC_RESAMPLED } pc_proc_t;
typedef enum { PC_CHAN_STEREO = 2, PC_CHAN_JOINT = 3 } pc_chan_t;

typedef struct {
    uint8_t kind;                 /* pc_os_kind_t; NONE = nothing plays / no output chain */
    /* the source */
    const char *fmt;              /* the backend's name ("FLAC"), static; NULL = nothing plays */
    uint32_t src_rate; uint8_t src_bits;
    /* what we do */
    uint8_t proc;                 /* pc_proc_t */
    pc_stage_t rate;              /* the pipeline's output rate; value = out rate (for PC_PROC_REFUSED: 0, the reason says why) */
    pc_stage_t bits;              /* wired: the DAC word (24 valid, or 16); A2DP: the link's bits */
    pc_stage_t slot;              /* FakePod: the I2S slot width 16 / 24 / 32 (value 0: not shown) */
    pc_stage_t speed;             /* value = percent chosen, why MODE when it is off for the mode (note) */
    pc_stage_t fx;                /* value = effects running; why BUSY when some are paused (note) */
    char speed_note[48], fx_note[48];
    int dac_db_x10; bool dac_db_valid;   /* FakePod: the DAC's digital volume in 0.1 dB (<= 0); valid false: not a FakePod or muted */
    bool dac_db_mute;
    /* Bluetooth headphones */
    bool conn;                    /* a sink is connected (else the live text says 未連線) */
    pc_stage_t codec;             /* value = pc_btc_t in use; limit = the codec that binds */
    pc_stage_t lrate, lbits, kbps;/* link rate, link bits, bit rate (0 = unknown); kbps: limit = the configured top, value = now */
    uint8_t chan;                 /* pc_chan_t */
    uint8_t best_codec; uint32_t best_rate; uint8_t best_bits;   /* 耳機最高: the sink's best (codec, its rate, its bits); best_codec PC_BTC_NONE = unknown */
    /* Auracast */
    uint32_t lc3_rate, lc3_kbps;
} pc_outstat_t;

/* ---- the wired decision ---- */
typedef struct {
    uint32_t src_rate; uint8_t src_bits;
    const pc_dac_caps_t *caps;    /* the board's DAC (pc_dac_caps_round / _fakepod) */
    uint32_t cap, hard, hw_cap;   /* dac_max_rate (halve threshold / wired ceiling), dac_hard_rate (0: none), this boot's clock ceiling */
    pc_pipe_t plan;               /* pc_pipe_plan's answer, after the LOWRATE fix (pipe 0 = refused) */
    uint32_t up_from;             /* nonzero: the resampling stage makes plan.pipe from this source rate (pc_dac_fix's *from) */
    bool refused_hard;            /* refused: above dac_hard_rate */
    int dac_bits;                 /* cfg dac_bits: 16 or 32 */
    int slot_mode;                /* cfg dac_slot24_from (pc_slot24_mode), FakePod only */
    uint8_t slot_real;            /* FIX4 I2: the slot width the BSP channel is open with (16 / 24 / 32); 0 = no channel: the setting's width is shown */
} pc_os_wired_in_t;
void pc_os_wired_plan(const pc_os_wired_in_t *in, pc_outstat_t *os);   /* proc, rate, bits, slot; every other field untouched */

/* ---- the Bluetooth decision ---- */
#define PC_BT_PREF_SBC 0
#define PC_BT_PREF_AAC 1
#define PC_BT_PREF_APTX 2
#define PC_BT_PREF_LDAC 3
#define PC_BT_BUILD_MAX_RATE 48000    /* the endpoints this build registers carry 44.1 / 48 kHz at most (bt_svc.c hp_register_one) */
#define PC_BT_BUILD_MAX_BITS 16       /* the encoders' ring is int16 */
unsigned pc_bt_pref_mask(int pref);                  /* codecs a preference allows: SBC {SBC}, AAC {AAC, SBC}, aptX {aptX HD, aptX, AAC, SBC}, LDAC all (a ceiling) */
unsigned pc_bt_build_mask(bool ldac);                /* codecs this build can run: all with the vendor stack, else AAC + SBC */
uint32_t pc_bt_rate_pick(uint32_t limit);            /* the link rates {44100, 48000, 88200, 96000}: the highest not above limit; 44100 at least */
typedef struct {
    bool connected, build_ldac, wifi_on;
    unsigned sink_codecs;                            /* mask (1 << pc_btc_t) of the sink's codecs; 0 with connected: capabilities not reported (the codec in use is then the sink's best) */
    uint32_t sink_rate[PC_BTC_N]; uint8_t sink_bits[PC_BTC_N];   /* per codec; 0 = not reported (no limit from the sink) */
    int pref_codec, pref_bits, pref_chan, ldac_kbps; uint32_t pref_rate;   /* cfg bt_codec, bt_bits (16 / 24), bt_chan (0 stereo, 1 joint), ldac_kbps, bt_rate */
    int live_kbps;                                   /* LDAC: the bit rate the adaptive step is at now (0 = unknown: the configured top) */
    int step_rung;                                   /* aptX / SBC step: 0 = the negotiated codec; > 0 = stepped down (codec in use is then in step_codec) */
    int step_codec;                                  /* pc_btc_t in use while stepped, else PC_BTC_NONE */
    uint32_t src_rate; uint8_t src_bits;             /* the playing source (0: none / not asked): a link rate or word at or above it is not limited by anything (reason PC_WHY_NONE) */
    /* FIX4 I3: the NEGOTIATED link (the stack's configuration of this connection) and the codecs that could not start this boot. The planner still gives the reasons; the values are the link's. */
    bool neg; uint8_t neg_codec; uint32_t neg_rate; uint8_t neg_chan;   /* neg: codec in use, its link rate, pc_chan_t */
    unsigned failed;                                 /* mask (1 << pc_btc_t) of codecs left out this boot: the sink is taken not to have them */
} pc_os_bt_in_t;
void pc_os_bt_plan(const pc_os_bt_in_t *in, pc_outstat_t *os);   /* kind, conn, codec, lrate, lbits, kbps, chan, best_*; not connected: kind A2DP, conn false */

/* ---- FakePod I2S slot width (dac_slot24_from) ---- */
enum { PC_SLOT24_OFF, PC_SLOT24_352, PC_SLOT24_192, PC_SLOT24_ALL, PC_SLOT24_N };   /* 關 / 352.8k and up / 192k and up / every rate */
int pc_slot24_mode(int v);                           /* out of range: off */
uint8_t pc_slot_bits(int mode, uint32_t rate, int dac_bits);   /* 24 at or above the mode's rate (dac_bits 32 only), 16 for dac_bits 16, else 32 */
uint32_t pc_slot24_from(int mode);                   /* the mode's rate (0 for off, 1 for all) */
uint32_t pc_slot_bclk(uint32_t rate, uint8_t slot_bits);   /* 2 * slot_bits * rate (384k, 24-bit: 18.432 MHz) */
void pc_pack_s24(const int32_t *s32, uint8_t *out, size_t n);   /* n samples (top 24 bits of each int32) as packed little-endian 3-byte samples; in place is fine (out may equal s32) */

/* ---- text ---- */
/* a rate as "44.1k" "384k" "352.8k" */
void pc_os_rate_text(char *b, size_t n, uint32_t hz);
/* The reason suffix of a stage: "（設定：折半 192k）"; "（原始）" for PC_WHY_NONE when with_orig; "" otherwise. also: the other binding tags are appended "・另：韌體 耳機". */
int pc_os_why_text(char *b, size_t n, const pc_stage_t *s, const char *note, bool with_orig, bool with_also, pc_lang_t l, bool cjk);
/* the settings pages' live lines (one line, the main reason only) */
int pc_os_live_bt(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk);      /* "正在傳輸：LDAC 96k / 24-bit / 立體聲 / 990 kbps（…）" or "未連線" */
int pc_os_live_wired(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk);   /* "輸入 384k → 輸出 192k・折半（設定：折半 192k）・DAC -6.0 dB" */
/* the Bluetooth page's info lines: "耳機最高：AAC 48k / 16-bit" and "實際：AAC 44.1k / 16-bit" (未連線 when nothing is connected) */
int pc_os_info_best(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk);
int pc_os_info_actual(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk);
int pc_os_info_next(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk);   /* FIX4 I3: "下次連線：LDAC 48k / 16-bit" (a stored preference not applied yet) */
/* the build note ("此版韌體不含 LDAC") when the preference asks for a codec this build lacks; "" otherwise */
int pc_os_build_note(char *b, size_t n, int pref_codec, bool build_ldac, pc_lang_t l, bool cjk);
/* the details page's 輸出 block: one line per stage, "\n" between lines, each stage with its reason (all the binding ones); returns the line count; 0 lines: nothing to show */
int pc_os_block(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk);
/* ICON-B: the same block with an inline icon string in front of each line's label (icons[PC_OSI_*]; a NULL entry or a NULL table: none). The strings are the UI's (UI_ICON_L_*). */
enum { PC_OSI_OUTPUT, PC_OSI_SOURCE, PC_OSI_PROC, PC_OSI_BITS, PC_OSI_SLOT, PC_OSI_VOLUME, PC_OSI_STATE, PC_OSI_CODEC, PC_OSI_RATE, PC_OSI_CHAN, PC_OSI_BITRATE, PC_OSI_SPEED, PC_OSI_FX, PC_OSI_N };
int pc_os_block_icons(char *b, size_t n, const pc_outstat_t *os, pc_lang_t l, bool cjk, const char *const *icons);

#ifdef __cplusplus
}
#endif
