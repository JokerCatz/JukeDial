// SPDX-License-Identifier: LGPL-2.1-or-later
/* Not a test: prints what pc_id3_parse sees in real files.  usage: id3_dump file.mp3 ... */
#include <stdio.h>
#include <string.h>
#include "pc/pc_id3.h"

int main(int argc, char **argv)
{
    int nodate = 0, nocover = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        pc_reader_t r;
        if (!f || pc_reader_file(&r, f)) { printf("open fail %s\n", argv[i]); if (f) fclose(f); continue; }
        pc_id3_t t;
        int rc = pc_id3_parse(&r, &t);
        printf("rc=%d date=%u track=%u cover=%u title=%s | artist=%s | album=%s | purl=%s | desc=%zu\n", rc, (unsigned)t.date,
               t.track, (unsigned)t.apic_len, t.title, t.artist, t.album, t.purl, t.desc ? strlen(t.desc) : 0);
        if (t.rg.track_cdb != PC_RG_NONE || t.rg.album_cdb != PC_RG_NONE)
            printf("  replaygain track %d cdB peak %u, album %d cdB peak %u\n", t.rg.track_cdb, t.rg.track_peak, t.rg.album_cdb, t.rg.album_peak);
        nodate += t.date == 0;
        nocover += t.apic_len == 0;
        pc_id3_free(&t);
        fclose(f);
    }
    printf("files=%d nodate=%d nocover=%d\n", argc - 1, nodate, nocover);
    return 0;
}
