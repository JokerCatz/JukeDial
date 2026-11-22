// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_extkeys.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 外接模式 T2: the HID send queue (spec 2026-11-15-external-mode.md sections 5, 6). Pure: no RTOS, no USB, no malloc, no clock of its own.
 * A key (combo, consumer code, ASCII text, macro) or a knob detent is expanded into events at once; the sender (a task on the board, a timer in the
 * simulator) takes the due ones with pc_extsend_peek / pc_extsend_pop and a millisecond clock. One producer (the LVGL task), one consumer; the
 * indices are published with release / acquire so no lock is needed. A key that does not fit is refused whole (counted in drops), never cut.
 * The struct (about 1.5 KB) belongs in PSRAM. */
#define PC_EXTSEND_CAP      256     /* events; one is always free */
#define PC_EXTSEND_HOLD_MS  12      /* a press stays down this long ... */
#define PC_EXTSEND_GAP_MS   12      /* ... and the next event waits this long after the release */
#define PC_EXTSEND_KNOB_MAX 16      /* detents taken from one knob call (a burst past this is cut) */

typedef enum { PC_XEV_KBD = 0, PC_XEV_CONS, PC_XEV_WAIT, PC_XEV_WHEEL } pc_extsend_kind_t;
/* KBD: keyboard report (RID 2) with modifier bits `mod` and one usage `code` (0 = none: the release). CONS: consumer report (RID 1) with usage `code` (0 = the release).
 * WHEEL: mouse report (RID 3) with only the wheel: `code` is the signed notch count (int16, one notch per detent; positive = up/away); no release.
 * WAIT: nothing is sent. after_ms: the time before the next event may go out. */
typedef struct { uint8_t kind; uint8_t mod; uint16_t code; uint16_t after_ms; } pc_extsend_ev;

typedef struct {
    pc_extsend_ev ev[PC_EXTSEND_CAP];
    uint32_t head;               /* consumer */
    uint32_t tail;               /* producer */
    uint32_t due_ms;             /* consumer: the next event may go at this time */
    uint32_t drops;              /* producer: keys refused for lack of room */
    uint32_t stall_since;        /* consumer: pump state, the time the sink first refused the head event */
    bool stalled;
} pc_extsend_t;

void pc_extsend_init(pc_extsend_t *q);
/* Producer side. true = queued; false = refused (not a sendable key, or no room: drops++). */
bool pc_extsend_key(pc_extsend_t *q, const pc_extkeys_key_t *k);
/* ASCII text (printable, '\n', '\t'; a byte that cannot be typed is skipped), at most (PC_EXTSEND_CAP - 1) / 2 characters in one call; the settings address is typed with this. */
bool pc_extsend_text(pc_extsend_t *q, const char *text);
/* The knob role of a layer (pc_extkeys_knob_t) for `detents` (sign = direction): volume = consumer volume up/down; scroll = mouse wheel notches (down for forward); arrows = Right/Left arrow keys; none = nothing.
 * Returns the detents queued (<= PC_EXTSEND_KNOB_MAX, 0 for none or no room). */
int pc_extsend_knob(pc_extsend_t *q, int knob, int detents);
/* Consumer side. peek: the next event when it is due at `now_ms` (NULL when empty or not yet). pop: it was sent (or skipped): the next waits its after_ms from now_ms. */
const pc_extsend_ev *pc_extsend_peek(pc_extsend_t *q, uint32_t now_ms);
void pc_extsend_pop(pc_extsend_t *q, uint32_t now_ms);
/* The sender's step, one pump call: hands every due event to `sink` (false = the link is not ready: retry at the next call). After PC_EXTSEND_STALL_MS of refusals a PRESS is
 * dropped together with everything up to the next release; a RELEASE (a keyboard report with no key, a consumer report 0) is never dropped, it keeps being retried, so a key
 * that went down never stays down. */
#define PC_EXTSEND_STALL_MS 100
typedef bool (*pc_extsend_sink_fn)(const pc_extsend_ev *e);
void pc_extsend_pump(pc_extsend_t *q, uint32_t now_ms, pc_extsend_sink_fn sink);
/* Before a flush while the link is up: one all-up keyboard report and one consumer release through `sink` (no key or code can stay held), then the flush. sink NULL: just flush. */
void pc_extsend_flush_up(pc_extsend_t *q, pc_extsend_sink_fn sink);
int pc_extsend_count(const pc_extsend_t *q);          /* events waiting */
void pc_extsend_flush(pc_extsend_t *q);               /* consumer side: drop everything waiting (the computer went away) */

#ifdef __cplusplus
}
#endif
