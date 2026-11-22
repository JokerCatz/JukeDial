// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "ui.h"
/* GS: the quick-settings page (spec 2026-11-16-gestures.md section 3), ONE shared page for both boards, called by the down flick (ui.c) on any page. It is a page on the stack (no
 * animation: UI rule), so the page under it is covered and told on_suspend (a game pauses) and on_resume when it closes.
 * Contents: the status line (time, output, power source), brightness and volume as `name value - +` rows (steps 10 and 1; the volume stops at volume_max), then three switches: Bluetooth,
 * Wi-Fi (allow / deny: Wi-Fi itself is on demand, the switch only says whether anyone may turn it on) and airplane (Bluetooth off + Wi-Fi denied, one tap; the same tap restores).
 * A switch that this boot mode cannot honour is grey and the reason is the line under the switches (one line, three languages). Closes by the up flick, a tap on the empty page, or 4 s without a touch.
 * Console: "qs" opens, "qs close", "qs dump" prints "APP qs bright=<n> vol=<n> bt=<0|1|-> wifi=<0|1|-> air=<0|1|-> why=<text>", "qs tap bright-|bright+|vol-|vol+|bt|wifi|air". */
extern const ui_screen_t scr_qs;
void ui_qs_show(void);
void ui_qs_close(void);
bool ui_qs_is(const ui_screen_t *s);
void ui_qs_cmd(const char *arg);
