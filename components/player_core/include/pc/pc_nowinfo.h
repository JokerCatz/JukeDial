// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* The 現況 (status) page and the console `status` text (hires T7, spec A4 / H10). Pure: no RTOS, no state; the caller fills pc_nowinfo_t from
 * audio_svc_info(), fx_state_running(), bt_svc_mode(), bt_svc_codec() and the card state. Lines (each only when it has something to say):
 *   來源 Source, 處理 Processing, 音效 Effects, 輸出 Output, 狀況 Status; zh "label：text", en "Label: text". */
typedef enum { PC_NI_WIRED, PC_NI_A2DP, PC_NI_CAST, PC_NI_SPEAKER } pc_ni_out_t;
typedef struct {
    pc_ni_out_t out;
    const char *fmt;                 /* the playing source's format name ("FLAC"); NULL = nothing plays (no 來源 / 處理 / 狀況 line) */
    uint32_t src_rate; uint8_t src_bits; uint32_t kbps;   /* the source's own rate and depth (0 = unknown, the part is left out); kbps: file size * 8 / length, 0 = unknown */
    uint32_t pipe_rate; uint8_t pipe_why;                  /* the pipeline rate and pc_pipe_why_t of the last halving */
    int fx_run, fx_paused; const char *fx_why;             /* effects that run / are paused (fx_state_running); why: the first pause reason, already in the page language */
    uint32_t out_rate; uint8_t out_bits; const char *link; /* the I2S channel (bits 32 = 24 bit shown; 0 rate: released); A2DP: the codec; CAST: the quality text, in the page language */
    uint32_t underruns;
    /* appended after the brief's fields (positional initialisers of the tests stop at underruns) */
    bool released;                   /* the I2S channel is released while no sink is on: said on the 狀況 line */
    uint32_t hw_cap, cap;            /* the DAC clock's ceiling this boot and the wired ceiling of the setting: with pipe_why CLOCK, hw_cap < cap = "refused this boot" */
    const char *nocard;              /* no card is mounted: the mount failure word (nocard|nofs|gpt|err); shown as the 來源 line while nothing plays */
    bool covers_paused;              /* hires T8c: cover decoding paused (a source above 48 kHz plays, thumb_svc_paused): said on the 狀況 line, any output */
    const char *peer_title;          /* speaker mode: the phone's title (AVRCP), NULL or "" = not known; after the 來源 word */
    /* hires T8e: the cover pack's background pass (pc_thumbbg.h, thumb_svc_progress): building or held is said on the 狀況 line with the
     * numbers ("封面建置中 23/70（32%）", "封面建置暫停（<why>）23/70"); done / manual: only the requests' hi-res pause (covers_paused) */
    uint8_t covers_state, covers_why;   /* PC_TBG_ST_*, PC_TBG_* */
    uint32_t covers_done, covers_total;
    bool covers_tidy;                /* fix 0: done, a compaction waits for a stop: "封面完成・整理待停止" */
} pc_nowinfo_t;

/* lines separated by "\n\n" into b[n] (always NUL-terminated, never cut inside a UTF-8 sequence); returns the line count */
int pc_nowinfo_text(const pc_nowinfo_t *ni, bool zh, char *b, size_t n);
void pc_nowinfo_rate(uint32_t hz, char *b, size_t n);        /* "44.1 kHz", "96 kHz", "352.8 kHz" */

#ifdef __cplusplus
}
#endif
