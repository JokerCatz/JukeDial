// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_hidtype.h"
#include <string.h>

/* US layout: unshifted and shifted characters per keycode */
static const struct { char plain, shifted; uint8_t code; } KEYS[] = {
    { '1', '!', 0x1E }, { '2', '@', 0x1F }, { '3', '#', 0x20 }, { '4', '$', 0x21 }, { '5', '%', 0x22 },
    { '6', '^', 0x23 }, { '7', '&', 0x24 }, { '8', '*', 0x25 }, { '9', '(', 0x26 }, { '0', ')', 0x27 },
    { '-', '_', 0x2D }, { '=', '+', 0x2E }, { '[', '{', 0x2F }, { ']', '}', 0x30 }, { '\\', '|', 0x31 },
    { ';', ':', 0x33 }, { '\'', '"', 0x34 }, { '`', '~', 0x35 }, { ',', '<', 0x36 }, { '.', '>', 0x37 },
    { '/', '?', 0x38 }, { ' ', ' ', 0x2C },
};

static int key(char c, uint8_t out[2])
{
    if (c >= 'a' && c <= 'z') { out[0] = 0; out[1] = (uint8_t)(0x04 + c - 'a'); return 0; }
    if (c >= 'A' && c <= 'Z') { out[0] = PC_HID_SHIFT; out[1] = (uint8_t)(0x04 + c - 'A'); return 0; }
    if (c == '\n') { out[0] = 0; out[1] = PC_HID_ENTER; return 0; }
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) {
        if (c == KEYS[i].plain) { out[0] = 0; out[1] = KEYS[i].code; return 0; }
        if (c == KEYS[i].shifted) { out[0] = PC_HID_SHIFT; out[1] = KEYS[i].code; return 0; }
    }
    return -1;
}

int pc_hid_type(const char *text, uint8_t (*out)[2], int max)
{
    int n = 0;
    for (const char *c = text; *c; c++) {
        if (n >= max || key(*c, out[n])) return -1;
        n++;
    }
    return n;
}
