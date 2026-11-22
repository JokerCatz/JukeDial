// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Font pack: the font files kept in the "fonts" flash partition (spec 2026-11-11-flash-font-design.md).
 * Layout (little-endian): "JCFP", u16 version, u16 count, u32 dir_crc (CRC32 of bytes 12 .. end of
 * directory), u32 image_size; then count entries of 48 bytes: name[32] (NUL-terminated), u32 offset,
 * u32 size, u32 crc32, u32 reserved. Built by tools/gen/make_fontpak.py. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_crc32.h"

#define PC_FONTPAK_VERSION 1
#define PC_FONTPAK_MAX 8
#define PC_FONTPAK_HDR 16
#define PC_FONTPAK_ENTRY 48
#define PC_FONTPAK_NAME 32

enum {
    PC_FONTPAK_OK = 0,
    PC_FONTPAK_ESHORT = -1,     /* fewer bytes than the header/directory needs */
    PC_FONTPAK_EMAGIC = -2,     /* no pack (e.g. an erased or never written partition) */
    PC_FONTPAK_EVERSION = -3,
    PC_FONTPAK_ECOUNT = -4,
    PC_FONTPAK_ECRC = -5,       /* directory checksum */
    PC_FONTPAK_ENAME = -6,
    PC_FONTPAK_ERANGE = -7,     /* a file or the image outside the partition, or over the directory */
};

typedef struct {
    char name[PC_FONTPAK_NAME];
    uint32_t offset, size, crc;
} pc_fontpak_entry_t;

typedef struct {
    int count;
    uint32_t image_size;
    pc_fontpak_entry_t e[PC_FONTPAK_MAX];
} pc_fontpak_t;

size_t pc_fontpak_dir_len(int count);                             /* header + directory bytes */
/* head = the first len bytes of the partition (at least the directory), part_size = partition size */
int pc_fontpak_parse(const uint8_t *head, size_t len, size_t part_size, pc_fontpak_t *out);
const pc_fontpak_entry_t *pc_fontpak_find(const pc_fontpak_t *p, const char *name);
bool pc_fontpak_check(const pc_fontpak_entry_t *e, const uint8_t *data);   /* data = the file's bytes */

/* sfnt (TrueType/OpenType) table directory length (12 + 16 * numTables) when it fits in n, else -1 */
int pc_sfnt_dir_len(const uint8_t *head, size_t n);
/* same font file: equal sizes and identical table directories (it holds every table's checksum);
 * a and b are the first n bytes of each file */
bool pc_sfnt_same(const uint8_t *a, uint32_t a_size, const uint8_t *b, uint32_t b_size, size_t n);

/* which copy of one font file to use */
typedef enum { PC_FONT_NONE = 0, PC_FONT_TF, PC_FONT_FLASH } pc_font_src_t;
enum { PC_FONT_PREF_AUTO = 0, PC_FONT_PREF_FLASH = 1, PC_FONT_PREF_TF = 2 };   /* cfg font_src */
/* auto: same content -> flash (faster, no card reads); different -> card (tracks the library);
 * one valid copy -> that one. FLASH / TF = only that copy (test switch). */
pc_font_src_t pc_font_pick(int pref, bool tf_ok, bool flash_ok, bool same);
const char *pc_font_src_name(pc_font_src_t s);
