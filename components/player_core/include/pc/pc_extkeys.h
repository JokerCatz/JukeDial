// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 外接模式 T1: _player/config/keys.txt (spec 2026-11-15-external-mode.md section 5). Pure: no RTOS, no file access, no malloc.
 * The caller reads the file (<= PC_EXTKEYS_FILE_MAX bytes) and gives the bytes; the result struct (about 6 KB) belongs in PSRAM.
 * Any error rejects the whole file: the output is then the default media layer and the error list says why (never written back). */
#define PC_EXTKEYS_LAYERS    8
#define PC_EXTKEYS_KEYS      8      /* per layer */
#define PC_EXTKEYS_LAYER_NAME 12    /* bytes, UTF-8 */
#define PC_EXTKEYS_KEY_NAME  23
#define PC_EXTKEYS_TEXT_MAX  64     /* ASCII bytes after escapes */
#define PC_EXTKEYS_MACRO_MAX 16     /* steps */
#define PC_EXTKEYS_WAIT_MAX  2000   /* ms */
#define PC_EXTKEYS_FILE_MAX  8192
#define PC_EXTKEYS_ERRS      8      /* listed; pc_extkeys_res_t.total counts all */

typedef enum { PC_KNOB_VOLUME = 0, PC_KNOB_SCROLL, PC_KNOB_ARROWS, PC_KNOB_NONE } pc_extkeys_knob_t;
typedef enum { PC_XACT_CONSUMER = 0, PC_XACT_COMBO, PC_XACT_TEXT, PC_XACT_MACRO } pc_extkeys_act_t;
typedef enum { PC_XSTEP_COMBO = 0, PC_XSTEP_WAIT } pc_extkeys_step_t;

typedef struct { uint8_t type; uint8_t mod; uint16_t val; } pc_extkeys_step;  /* COMBO: mod + keyboard usage (0 = modifiers only); WAIT: val ms */

typedef struct {
    char name[PC_EXTKEYS_KEY_NAME + 1];
    uint8_t act;                 /* pc_extkeys_act_t */
    uint8_t mod;                 /* COMBO: HID modifier bits (usage page 0x07 0xE0..0xE7 as bits 0..7) */
    uint16_t code;               /* CONSUMER: usage on page 0x0C; COMBO: keyboard usage on page 0x07 (0 = modifiers only) */
    uint8_t n;                   /* TEXT: byte count; MACRO: step count */
    union { char text[PC_EXTKEYS_TEXT_MAX + 1]; pc_extkeys_step steps[PC_EXTKEYS_MACRO_MAX]; } u;
} pc_extkeys_key_t;

typedef struct {
    char name[PC_EXTKEYS_LAYER_NAME + 1];
    uint8_t knob;                /* pc_extkeys_knob_t */
    uint8_t nkeys;
    pc_extkeys_key_t keys[PC_EXTKEYS_KEYS];
} pc_extkeys_layer_t;

typedef struct { uint8_t nlayers; pc_extkeys_layer_t layers[PC_EXTKEYS_LAYERS]; } pc_extkeys_cfg_t;

typedef enum {
    PC_XERR_NONE = 0, PC_XERR_TOO_BIG, PC_XERR_BAD_BYTE, PC_XERR_UNKNOWN_WORD, PC_XERR_NO_LAYER, PC_XERR_TOO_MANY_LAYERS,
    PC_XERR_TOO_MANY_KEYS, PC_XERR_LAYER_NAME, PC_XERR_KEY_NAME, PC_XERR_KNOB, PC_XERR_ACTION, PC_XERR_CONSUMER,
    PC_XERR_COMBO, PC_XERR_TEXT_ASCII, PC_XERR_TEXT_LONG, PC_XERR_TEXT_QUOTE, PC_XERR_MACRO_LONG, PC_XERR_MACRO_STEP,
    PC_XERR_WAIT, PC_XERR_EMPTY_LAYER, PC_XERR_COUNT
} pc_extkeys_err_code;

typedef struct { uint16_t line; uint8_t code; } pc_extkeys_err_t;   /* line 0 = whole file */
typedef struct {
    bool ok;                     /* false: the file was rejected and the default layer is in the output */
    bool from_file;              /* true: the output came from the file */
    uint8_t nerr;                /* entries in err[] (<= PC_EXTKEYS_ERRS) */
    uint16_t total;              /* all errors found (saturating) */
    pc_extkeys_err_t err[PC_EXTKEYS_ERRS];
} pc_extkeys_res_t;

void pc_extkeys_default(pc_extkeys_cfg_t *out);            /* the one 媒體鍵 layer: play/pause, previous, next, mute; knob = volume */
/* buf NULL or len 0, or only comments/blank lines: the default layer, ok, no error. Otherwise parse; any error -> default layer + list. */
void pc_extkeys_parse(const char *buf, size_t len, pc_extkeys_cfg_t *out, pc_extkeys_res_t *res);
const char *pc_extkeys_err_text(int code);                 /* zh-TW reason */
int pc_extkeys_err_fmt(const pc_extkeys_err_t *e, char *buf, size_t n);   /* "行號：原因" (line 0: just the reason) */
int pc_extkeys_ascii_usage(char c, uint8_t *mod);          /* printable ASCII / '\n' / '\t' -> keyboard usage, *mod = 0 or shift (0x02); 0 if none */

#ifdef __cplusplus
}
#endif
