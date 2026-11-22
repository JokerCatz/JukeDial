// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* test tone for the Bluetooth codec checks (console `tone HZ`); LVGL task */
int test_tone_play(int hz);     /* 0 stops; -1 if it could not start */
