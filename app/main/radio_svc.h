// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_paths.h"
#include <stddef.h>
#include "pc/pc_radio.h"
/* Network radio (spec 2026-11-09-radio-design.md): http only. LVGL context. */
#define RADIO_LIST PC_CFG_DIR "/radio.txt"
#define RADIO_MAX 64
int  radio_load(void);                    /* reads (or writes the default) station list of the chosen list file; count */
/* several lists on the card: radio.txt (stem "", shown "radio") and radio_<stem>.txt. The one in use is cfg "radio_list", else the language's default (簡中: "cn", else the main one). */
const char *radio_list_stem(void);
void radio_list_path(char *out, size_t cap);        /* the file of the list in use */
int  radio_lists(char (*stems)[PC_RADIO_STEM_MAX], int max);   /* the lists on the card, the main one first; count */
int  radio_list_select(const char *stem);           /* use that list and read it: its count; -1 when there is no such file */
int  radio_count(void);
const pc_station_t *radio_station(int i);
int  radio_play(int i);                   /* 0 ok; <0 not possible (broadcast mode, https, bad index) */
void radio_stop(void);
int  radio_current(void);
/* checks on a worker task whether this device can play url (http only, follows http redirects, sniffs the bytes);
 * done() runs on that task: fmt "mp3" "aac" "ogg" "ts" "hls", or why: "https" "status 404" "format" "wifi" ... ; -2 when another check is running */
int  radio_probe(const char *url, int id, void (*done)(int id, bool ok, const char *fmt, const char *url, const char *why));
int  radio_add_temp(const char *name, const char *url);   /* console tests: index, not saved */                 /* -1 when not playing */
void radio_stats(uint32_t *bytes, uint32_t *reconnects, uint32_t *underruns);   /* since boot: bytes read, streams that ended and reconnected, dry decoder reads of the stream now playing (the broadcast-radio boot's soak line) */
