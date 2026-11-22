// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes fontpak (the "fonts" flash partition mapped into memory). */
/* Behaviour: in the player and the instrument there is no flash font, so font.c falls through to <root>/_player/fonts and the Montserrat fallback. In --mode usb
 * (font_init_flash: the flash pack only) the pack is <root>/_player/fonts, mapped read-only like the partition, so the USB mode's screen gets the CJK font as on
 * the board; FreeType then opens FONTPAK_PREFIX "<name>", which shim_font.c maps to the same file. SIM_NOFONT runs have no <root>/_player/fonts: no pack. */
/* Empty: the CRC check (the files are whatever the fonts link holds). */
#include <fcntl.h>
#include "pc/pc_paths.h"
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fontpak.h"
#include "sim.h"

#define NMAP 4
static struct { char name[32]; const uint8_t *data; uint32_t size; } g_map[NMAP];   /* mapped for the process's life, as the partition */

static bool usb(void) { return !strcmp(g_sim_mode, "ext"); }

bool fontpak_init(void) { return usb(); }

bool fontpak_get(const char *name, const uint8_t **data, uint32_t *size)
{
    if (!usb() || !name || strlen(name) >= sizeof g_map[0].name) return false;
    for (int i = 0; i < NMAP; i++)
        if (g_map[i].data && !strcmp(g_map[i].name, name)) { *data = g_map[i].data; *size = g_map[i].size; return true; }
    for (int i = 0; i < NMAP; i++) {
        if (g_map[i].data) continue;
        char p[96];
        snprintf(p, sizeof p, PC_FONTS_DIR "/%s", name);
        int fd = open(p, O_RDONLY);                  /* the wrapped open: under the root */
        if (fd < 0) return false;
        struct stat st;
        void *m = fstat(fd, &st) || st.st_size <= 0 ? MAP_FAILED : mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (m == MAP_FAILED) return false;
        snprintf(g_map[i].name, sizeof g_map[i].name, "%s", name);
        g_map[i].data = m;
        g_map[i].size = (uint32_t)st.st_size;
        *data = g_map[i].data;
        *size = g_map[i].size;
        return true;
    }
    return false;
}
