// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* hires T8e: the cover pack's background pass (thumb_svc.c; the simulator's fake uses the same rules). Owner 2026-11-14: covers build in the
 * background rather than be unavailable, while stopped AND while a source of <= 48 kHz plays, at low priority, yielding to the audio; held only
 * for a reason the 封面快取 page and 現況 show (H14: never silent). Pure: no RTOS, no state of its own. */

/* why the pass does not run now (0 = it runs). Order = precedence (the first that holds is the one shown). */
enum {
    PC_TBG_RUN = 0,
    PC_TBG_NO_CARD,      /* no card mounted */
    PC_TBG_MANUAL,       /* setting 封面快取 = 手動 and no 立即建置 asked: not a pause, the page's state 手動 */
    PC_TBG_USER,         /* the page's 暫停 */
    PC_TBG_SCAN,         /* a library scan runs */
    PC_TBG_HIRES,        /* a source above 48 kHz is loaded (pc_thumb_hires_pause) */
    PC_TBG_OPENING,      /* a source is opening: its rate is not known yet (pc_thumb_opening) */
    PC_TBG_UNDERRUN,     /* an underrun while a source was loaded: quiet for PC_TBG_QUIET_MS first; after PC_TBG_GIVEUP of them only while stopped */
    PC_TBG_LOW_MEM,      /* internal RAM too low for the decoder (the low-internal gate) */
    PC_TBG_STOP_NEEDED,  /* waits for a stop: the card's free space is read only while nothing is loaded (its first read can scan the FAT), the compaction too */
    PC_TBG_FULL,         /* writes stopped: the card's free-space margin or a full card */
    PC_TBG_WRITE,        /* writes stopped: a write failed, the pack could not be opened, or its 1 GiB cap */
    PC_TBG_TABLE_FULL,   /* writes stopped: the pack's table is full (compacted into a bigger one after the pass) */
    PC_TBG_N
};
#define PC_TBG_QUIET_MS 30000u       /* after an underrun the pass waits this long without another one */
#define PC_TBG_GIVEUP 3u             /* this many underrun holds in one boot: the pass runs only while nothing is loaded */
#define PC_TBG_BREATHER_IDLE_MS 250u /* between two pictures while nothing is loaded */
#define PC_TBG_BREATHER_PLAY_MS 1000u   /* ... while a source is loaded: the audio's card reads come first */

typedef struct {
    bool card, scanning;
    bool manual, requested;     /* setting 手動; 立即建置 asked (a pass the user started: runs in either mode until it ends) */
    bool user_paused;
    bool loaded;                /* a source is loaded (playing, paused, between tracks): the pass yields to it */
    uint32_t src_rate;          /* audio_svc_info().src_rate (0: nothing loaded) */
    bool opening;
    uint32_t underrun_age_ms;   /* since the last underrun seen (pc_tbg_under_age), UINT32_MAX: none */
    uint32_t underrun_holds;    /* underrun holds this boot (pc_tbg_under_t.holds) */
    bool space_known;           /* the card's free space was read (only while nothing is loaded) */
    bool low_mem;               /* the low-internal gate, asked only before a decode that needs it */
} pc_tbg_in_t;
int pc_tbg_rule(const pc_tbg_in_t *in);
uint32_t pc_tbg_breather_ms(bool loaded);

/* underruns charged to the pass (fix round 1, review I1). The count = audio_svc_info().underruns_src (since the source started) + the Bluetooth
 * sink's counted dry blocks (pc_tbg_sink_block). arm: right before a pass step (its baseline is taken then, so a rise while no step ran - a
 * rescan's card load, a pause - never counts); check: after the step and once more at the next wake (the breather window); a rise of the same gen
 * while armed, with the source past its first PC_TBG_START_MS (an output reconfigure at a start is not the pass's fault), is charged: true.
 * Every check re-baselines; disarm ends the window. holds: how often a quiet period started (only charged underruns). */
#define PC_TBG_START_MS 1000
typedef struct { uint32_t gen, count; int64_t last_ms; uint32_t holds; bool armed, any; } pc_tbg_under_t;
void pc_tbg_under_arm(pc_tbg_under_t *u, uint32_t gen, uint32_t count);
bool pc_tbg_under_check(pc_tbg_under_t *u, uint32_t gen, uint32_t count, int32_t pos_ms, int64_t now_ms);
void pc_tbg_under_disarm(pc_tbg_under_t *u);
/* fix round 2 (re-review N1): the window opens only for a step the gate let through (rule RUN: it is about to touch the card), else it closes;
 * a step that did no work closes it before the wait (pc_tbg_under_step_end) */
void pc_tbg_under_step(pc_tbg_under_t *u, int rule, uint32_t gen, uint32_t count);
void pc_tbg_under_step_end(pc_tbg_under_t *u, bool worked);
/* the Bluetooth sink's blocks (bt_svc's three paths): a dry block counts only after PC_TBG_SINK_STEADY_MS of fed blocks (a stream that starts, or
 * resumes after a pause, fills its ring first: review M4). true = count this dry block */
#define PC_TBG_SINK_STEADY_MS 1000
typedef struct { int64_t since; uint32_t sess; bool fed; } pc_tbg_sink_t;
/* sess: the sink session (bumped when a sink starts, re-review N2): a new one starts unfed (its first dry blocks are the ring filling) */
bool pc_tbg_sink_block(pc_tbg_sink_t *s, uint32_t sess, bool dry, int64_t now_ms);
/* fix round 1 (review I2): the progress may read the pack's table from the card only when no hold forbids card access (hi-res, a source opening)
 * and someone needs it (the page or 現況 asked, or a pass that is not held by 手動 runs) */
bool pc_tbg_may_read_table(bool hires, bool opening, bool wanted, bool pass_active);
uint32_t pc_tbg_under_age(const pc_tbg_under_t *u, int64_t now_ms);                         /* ms since the last one, UINT32_MAX none */

/* the 封面快取 page's state and the 現況 fragment */
enum { PC_TBG_ST_DONE, PC_TBG_ST_BUILDING, PC_TBG_ST_HELD, PC_TBG_ST_MANUAL };
typedef struct {
    uint8_t state, why;         /* why: PC_TBG_* while HELD (else RUN) */
    uint32_t done, total;       /* covers in the pack (or marked as not a picture) / tracks known to have a cover (fix 0: not the unchecked ones) */
    bool manual, user_paused;   /* the setting, the page's 暫停 */
    uint32_t unchecked;         /* fix 0 (C5): tracks without an embedded picture whose folder was not looked at yet (may or may not have a cover) */
    bool tidy;                  /* fix 0 (C4): every cover is in, a compaction of the pack waits for a stop */
} pc_tbg_prog_t;
/* running: a pass or a compaction is under way; why: pc_tbg_rule now (or the stopped writes' reason); MANUAL is a state, not a pause */
int pc_tbg_state(bool running, int why);
int pc_tbg_pct(uint32_t done, uint32_t total);            /* 0..100 (floor); 100 when total is 0 */
const char *pc_tbg_why_name(int why);                     /* console word: "run" "no_card" "manual" "user" "library_scan" "hires" ... */
const char *pc_tbg_why_text(int why, bool zh);            /* the page's reason: 高取樣率 / hi-res playing ... */
int pc_tbg_why_of(const char *name);                      /* thumb_svc's reason words (gate, write gate, stopped writes) -> PC_TBG_*; unknown: PC_TBG_WRITE */
void pc_tbg_state_text(const pc_tbg_prog_t *p, bool zh, char *b, size_t n);   /* 建置中 / 暫停：<why> / 完成 / 手動 */
void pc_tbg_count_text(const pc_tbg_prog_t *p, bool zh, char *b, size_t n);   /* 23 / 70・32%・剩 47 首[・未檢查 5 首] ; 沒有封面 when total is 0 */
/* fix 0 (C6, no redundant chrome): the 現況 page shows its 封面快取 row only while there is something to see: building, paused, 手動 with covers
 * left (or unchecked), a compaction pending */
bool pc_tbg_status_row(const pc_tbg_prog_t *p);
#ifdef __cplusplus
}
#endif
