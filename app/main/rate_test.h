// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_ratetest.h"
/* The rate test (hires T11, H18): plays each tier's two tones as mixers at the tier's own rate and lets the owner say by a tap that both were heard; the
 * result is the wired ceiling (dac_max_rate). Board and simulator; LVGL task only. The library track that was playing is resumed once at the end. */
bool rate_test_start(void);       /* false: refused (a Bluetooth sink carries the sound: prints "APP ratetest refused: bluetooth"; rate_test_refused() says so). Already running: true, nothing changes. */
void rate_test_tap(void);         /* "I heard both" (the tier's tone 2 has started); ignored otherwise */
void rate_test_stop(void);        /* leave early: the tone stops, nothing is stored, the library track resumes; no-op when nothing runs */
const pc_ratetest_t *rate_test_state(void);   /* NULL until the first start; the last run's state stays (DONE) until the next start */
bool rate_test_refused(void);     /* the last start was refused for the Bluetooth sink */
/* The page makes the run, never the page's re-creation (a theme change rebuilds every page, also one under another page): whoever opens the page calls
 * rate_test_want() first; the page's create takes it (rate_test_want_take) and starts the test; a create without it only shows the state. */
void rate_test_want(void);
bool rate_test_want_take(void);
void rate_test_poll(void);        /* every 50 ms (the page's LVGL timer; the test runs only while its page is up): tones, timeout, fault watch, end; callable by tests */
void rate_test_show(void);        /* console "ratetest show": APP ratetest state ... */
