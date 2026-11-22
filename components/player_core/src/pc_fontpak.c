// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_fontpak.h"
#include <string.h>
#include "pc/pc_crc32.h"

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

size_t pc_fontpak_dir_len(int count) { return PC_FONTPAK_HDR + (size_t)count * PC_FONTPAK_ENTRY; }

int pc_fontpak_parse(const uint8_t *h, size_t len, size_t part_size, pc_fontpak_t *out)
{
    if (!h || len < PC_FONTPAK_HDR) return PC_FONTPAK_ESHORT;
    if (memcmp(h, "JCFP", 4)) return PC_FONTPAK_EMAGIC;
    if ((h[4] | h[5] << 8) != PC_FONTPAK_VERSION) return PC_FONTPAK_EVERSION;
    int n = h[6] | h[7] << 8;
    if (n < 1 || n > PC_FONTPAK_MAX) return PC_FONTPAK_ECOUNT;
    size_t dir = pc_fontpak_dir_len(n);
    if (len < dir) return PC_FONTPAK_ESHORT;
    if (pc_crc32(0, h + 12, dir - 12) != rd32(h + 8)) return PC_FONTPAK_ECRC;
    uint32_t image = rd32(h + 12);
    if (image < dir || image > part_size) return PC_FONTPAK_ERANGE;
    memset(out, 0, sizeof *out);
    out->count = n;
    out->image_size = image;
    for (int i = 0; i < n; i++) {
        const uint8_t *e = h + PC_FONTPAK_HDR + i * PC_FONTPAK_ENTRY;
        if (!e[0] || !memchr(e, 0, PC_FONTPAK_NAME)) return PC_FONTPAK_ENAME;
        pc_fontpak_entry_t *o = &out->e[i];
        memcpy(o->name, e, PC_FONTPAK_NAME);
        o->offset = rd32(e + 32);
        o->size = rd32(e + 36);
        o->crc = rd32(e + 40);
        if (o->offset < dir || o->offset > image || o->size > image - o->offset) return PC_FONTPAK_ERANGE;
    }
    return PC_FONTPAK_OK;
}

const pc_fontpak_entry_t *pc_fontpak_find(const pc_fontpak_t *p, const char *name)
{
    for (int i = 0; p && name && i < p->count; i++)
        if (!strcmp(p->e[i].name, name)) return &p->e[i];
    return NULL;
}

bool pc_fontpak_check(const pc_fontpak_entry_t *e, const uint8_t *data) { return e && data && pc_crc32(0, data, e->size) == e->crc; }

int pc_sfnt_dir_len(const uint8_t *h, size_t n)
{
    if (!h || n < 12) return -1;
    uint32_t v = be32(h);
    if (v != 0x00010000u && v != 0x4F54544Fu /* OTTO */ && v != 0x74727565u /* true */) return -1;
    size_t tabs = (size_t)(h[4] << 8 | h[5]), len = 12 + 16 * tabs;
    return tabs && len <= n ? (int)len : -1;
}

bool pc_sfnt_same(const uint8_t *a, uint32_t a_size, const uint8_t *b, uint32_t b_size, size_t n)
{
    int la = pc_sfnt_dir_len(a, n), lb = pc_sfnt_dir_len(b, n);
    return a_size == b_size && la > 0 && la == lb && !memcmp(a, b, (size_t)la);
}

pc_font_src_t pc_font_pick(int pref, bool tf_ok, bool flash_ok, bool same)
{
    if (pref == PC_FONT_PREF_FLASH) return flash_ok ? PC_FONT_FLASH : PC_FONT_NONE;
    if (pref == PC_FONT_PREF_TF) return tf_ok ? PC_FONT_TF : PC_FONT_NONE;
    if (tf_ok && flash_ok) return same ? PC_FONT_FLASH : PC_FONT_TF;
    return tf_ok ? PC_FONT_TF : flash_ok ? PC_FONT_FLASH : PC_FONT_NONE;
}

const char *pc_font_src_name(pc_font_src_t s) { return s == PC_FONT_FLASH ? "flash" : s == PC_FONT_TF ? "tf" : "none"; }
