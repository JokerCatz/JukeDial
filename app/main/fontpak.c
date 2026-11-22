// SPDX-License-Identifier: LGPL-2.1-or-later
#include "fontpak.h"
#include <stdio.h>
#include <string.h>
#include "esp_partition.h"
#include "esp_timer.h"
#include "pc/pc_fontpak.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYSTEM_H

static pc_fontpak_t s_pak;      /* ~420 bytes */
static const uint8_t *s_base;
static bool s_ok[PC_FONTPAK_MAX];

bool fontpak_init(void)
{
    static bool done;
    if (done) return s_base != NULL;
    done = true;
    int64_t t0 = esp_timer_get_time();
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fonts");
    if (!p) { printf("APP fontpak none (no partition)\n"); return false; }
    uint8_t head[PC_FONTPAK_HDR + PC_FONTPAK_MAX * PC_FONTPAK_ENTRY];
    int r = esp_partition_read(p, 0, head, sizeof head);
    if (r == ESP_OK) r = pc_fontpak_parse(head, sizeof head, p->size, &s_pak);
    if (r != ESP_OK) { printf("APP fontpak none (rc=%d)\n", r); return false; }
    esp_partition_mmap_handle_t h;
    const void *m = NULL;
    esp_err_t e = esp_partition_mmap(p, 0, s_pak.image_size, ESP_PARTITION_MMAP_DATA, &m, &h);
    if (e != ESP_OK) { printf("APP fontpak mmap %s\n", esp_err_to_name(e)); return false; }
    int good = 0;
    for (int i = 0; i < s_pak.count; i++) {      /* a write cut short must not reach FreeType */
        s_ok[i] = pc_fontpak_check(&s_pak.e[i], (const uint8_t *)m + s_pak.e[i].offset);
        good += s_ok[i];
        printf("APP fontpak %s size=%lu crc=%s\n", s_pak.e[i].name, (unsigned long)s_pak.e[i].size, s_ok[i] ? "ok" : "BAD");
    }
    s_base = m;   /* stays mapped for good */
    printf("APP fontpak files=%d good=%d image=%lu check_ms=%lld\n", s_pak.count, good, (unsigned long)s_pak.image_size,
           (esp_timer_get_time() - t0) / 1000);
    return good > 0;
}

bool fontpak_get(const char *name, const uint8_t **data, uint32_t *size)
{
    if (!s_base) return false;
    const pc_fontpak_entry_t *e = pc_fontpak_find(&s_pak, name);
    if (!e || !s_ok[e - s_pak.e]) return false;
    *data = s_base + e->offset;
    *size = e->size;
    return true;
}

/* -Wl,--wrap=FT_Stream_Open (main/CMakeLists.txt); every other path goes to FreeType's own stdio stream */
FT_Error __real_FT_Stream_Open(FT_Stream stream, const char *path);
FT_Error __wrap_FT_Stream_Open(FT_Stream stream, const char *path)
{
    if (!path || strncmp(path, FONTPAK_PREFIX, sizeof FONTPAK_PREFIX - 1)) return __real_FT_Stream_Open(stream, path);
    const uint8_t *d;
    uint32_t n;
    if (!stream || !fontpak_get(path + sizeof FONTPAK_PREFIX - 1, &d, &n)) return FT_Err_Cannot_Open_Resource;
    stream->base = (unsigned char *)d;   /* a memory stream: FreeType reads the mapped bytes in place, no buffers */
    stream->size = n;
    stream->pos = 0;
    stream->cursor = stream->limit = NULL;
    stream->descriptor.pointer = NULL;
    stream->pathname.pointer = (char *)path;
    stream->read = NULL;
    stream->close = NULL;
    return FT_Err_Ok;
}
