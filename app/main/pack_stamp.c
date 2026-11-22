// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pack_stamp.h"
#include <stdio.h>
#include "esp_attr.h"
#include "pc/pc_paths.h"

EXT_RAM_BSS_ATTR static pc_pack_t s_pack;   /* PSRAM: no internal RAM */
EXT_RAM_BSS_ATTR static char s_tmp[256];
static pc_pack_state_t s_state = PC_PACK_OK;   /* until pack_stamp_boot has read the card (the simulator never does): no warning */

void pack_stamp_boot(void)
{
    int rev = -1;
    s_pack.rev = -1; s_pack.version[0] = 0;
    FILE *f = fopen(PC_PACK_TXT, "rb");
    if (f) {
        size_t n = fread(s_tmp, 1, sizeof s_tmp, f);   /* the first lines only: pack= and version= come first */
        fclose(f);
        if (pc_pack_parse(s_tmp, n, &s_pack) == 0) rev = s_pack.rev;
    }
    s_state = pc_pack_state(rev);
    printf("APP pack state=%s rev=%d need=%d version=%s\n", s_state == PC_PACK_OK ? "ok" : s_state == PC_PACK_OLD ? "old" : "missing", rev, PC_PACK_MIN, s_pack.version);
}
pc_pack_state_t pack_stamp_state(void) { return s_state; }
const char *pack_stamp_version(void) { return s_pack.version; }
