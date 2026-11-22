// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* On-device soak log (2026-11-09: long runs without the PC awake). Only when /sdcard/.soak exists: every 60 s
 * one line is appended to /sdcard/_player/logs/soak.txt (open, append, close: a power cut loses at most a line).
 * The boot line carries the reset reason, so crashes and brownouts show up too. LVGL context. */
void soak_log_start(int reset_reason);
