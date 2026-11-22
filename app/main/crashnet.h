// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
/* Crash safety net: the track being decoded is kept in RTC memory (16 bytes, pc_crashnet.h); a boot after a panic / watchdog reset marks it
 * (persisted in config/crashed.txt on the card). A marked track is not played by itself (resume, next, shuffle, auto-advance) and the lists say so; playing it
 * deliberately clears the mark (a crash again marks it again). Works with the boot-loop guard: this only reads guard_last_reset_was_crash(); safe mode is untouched. */
void crashnet_early(bool crashed);          /* first thing after guard_boot(): takes (and clears) the RTC note; no card needed */
void crashnet_card(bool card_ok);           /* after the card mount: loads the marks, adds the ones just found, saves */
void crashnet_playing(const char *path);    /* a track is being decoded now (also the gapless switch) */
void crashnet_queued(const char *path);     /* the next track is being opened while the current plays; NULL = none */
void crashnet_idle(void);                   /* nothing is being decoded (stop, another source, before a restart) */
bool crashnet_marked(const char *path);     /* the track's mark ("上次播放時當機，已略過") */
void crashnet_clear(const char *path);      /* a deliberate play of the track: the mark goes */
int  crashnet_count(void);
