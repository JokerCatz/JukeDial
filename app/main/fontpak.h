// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* The "fonts" flash partition (spec 2026-11-11-flash-font-design.md): font files mapped into the address
 * space (esp_partition_mmap, no RAM copy). FreeType opens them as FONTPAK_PREFIX "<name>": FT_Stream_Open is
 * wrapped at link time and gives FreeType a memory stream on the mapped bytes. */
#include <stdbool.h>
#include <stdint.h>
#define FONTPAK_PREFIX "/flashfont/"
bool fontpak_init(void);   /* map + check every file's CRC once; false = no usable pack */
bool fontpak_get(const char *name, const uint8_t **data, uint32_t *size);
