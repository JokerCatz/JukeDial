// SPDX-License-Identifier: LGPL-2.1-or-later
/* viz_dump <out dir> [maps]: with "maps" it writes only maps_0_<w>x<h>.rgb565, the colour-mapping sheet (rows = height, tip, band,
 * level; columns = bars solid, bars mirror, ring out in the fire ramp, then the same in rainbow). Without: draws every implemented spectrum-page style (fixed input, neon theme, fire ramp) and writes
 * <kind>_<variant>_<w>x<h>.rgb565 (host byte order). tools/simtools/viz_png.py turns the directory into PNGs. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc/pc_vizdraw.h"

static pc_vd_t D;

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: viz_dump <out dir>\n"); return 2; }
    float lv[64], pk[64];
    for (int i = 0; i < 64; i++) { lv[i] = 0.15f + 0.8f * fabsf(sinf(i * 0.21f)); pk[i] = lv[i] + 0.08f; }
    static float wave[600];                                        /* three periods and a little high frequency */
    for (int i = 0; i < 600; i++) wave[i] = 0.7f * sinf(6.2831853f * 3.0f * (float)i / 600.0f) + 0.15f * sinf((float)i * 1.9f);
    size_t mcap = pc_vd_ringmap_bytes(PC_VD_RING_SZ, PC_VD_RING_SZ);
    void *map = malloc(mcap);
    if (!map || !pc_vd_ringmap_build(map, mcap, PC_VD_RING_SZ, PC_VD_RING_SZ, PC_VD_RING_IN, PC_VD_RING_OUT, 0)) { fprintf(stderr, "ring map failed\n"); return 1; }
    uint16_t *px = malloc(PC_VD_MAX_PX * 2);
    if (!px) return 1;
    if (argc > 2 && !strcmp(argv[2], "maps")) {            /* the colour-mapping sheet: 4 maps (rows) x {bars solid, bars mirror, ring out} x {fire, rainbow} */
        enum { GAP = 8, CW = 300 + 300 + PC_VD_RING_SZ + 3 * GAP, SW = 2 * CW + GAP, RH = PC_VD_RING_SZ + GAP, SH = 4 * RH + GAP };
        uint16_t *sheet = malloc((size_t)SW * SH * 2);
        if (!sheet) return 1;
        for (int i = 0; i < SW * SH; i++) sheet[i] = 0x2965;
        static const pc_ramp_t ramp[2] = { PC_RAMP_FIRE, PC_RAMP_RAINBOW };
        static const struct { int kind, var, w, h; } col[3] = { { 0, 0, 300, 200 }, { 0, 2, 300, 200 }, { 1, 0, PC_VD_RING_SZ, PC_VD_RING_SZ } };
        for (int r = 0; r < 2; r++) for (int m = 0; m < PC_VD_MAPS; m++) {
            int x0 = GAP + r * CW;
            for (int c = 0; c < 3; c++) {
                pc_vd_init(&D, px, col[c].w, col[c].h);
                pc_vd_set_colors(&D, ramp[r], pc_theme_find("neon"));
                pc_vd_set_map(&D, (pc_vd_map_t)m);
                if (col[c].kind == 0) pc_vd_bars(&D, col[c].var, lv, pk, 7); else pc_vd_ring(&D, col[c].var, map, lv, pk);
                for (int y = 0; y < col[c].h; y++)
                    memcpy(sheet + (size_t)(GAP + m * RH + y) * SW + x0, px + (size_t)y * col[c].w, (size_t)col[c].w * 2);
                x0 += col[c].w + GAP;
            }
        }
        char path[512];
        snprintf(path, sizeof path, "%s/maps_0_%dx%d.rgb565", argv[1], SW, SH);
        FILE *f = fopen(path, "wb");
        if (!f || fwrite(sheet, 2, (size_t)SW * SH, f) != (size_t)SW * SH) { fprintf(stderr, "write %s failed\n", path); return 1; }
        fclose(f);
        printf("%s\n", path);
        return 0;
    }
    for (int k = 0; k < PC_VD_KINDS; k++) {
        for (int v = 0; v < pc_vd_variants((pc_vd_kind_t)k); v++) {
            int w, h;
            pc_vd_size((pc_vd_kind_t)k, v, &w, &h);
            pc_vd_init(&D, px, w, h);
            pc_vd_set_colors(&D, PC_RAMP_FIRE, pc_theme_find("neon"));
            if (k == PC_VD_BARS) pc_vd_bars(&D, v, lv, pk, 7);
            else if (k == PC_VD_RING) pc_vd_ring(&D, v, map, lv, pk);
            else if (k == PC_VD_SCOPE) pc_vd_scope(&D, v, wave, 600);
            else if (k == PC_VD_VOICES) {                         /* the five voices of an NES file, one of them resting */
                static const char nm[16][4] = { "S1", "S2", "T", "N", "D" };
                static const float vl[5] = { 0.85f, 0.55f, 0.95f, 0.3f, 0.0f }, vp[5] = { 0.92f, 0.7f, 0.95f, 0.5f, 0.2f };
                pc_vd_voices(&D, v, map, 5, vl, vp, nm);
            }
            else if (k == PC_VD_PHASE) {                         /* a wide stereo figure, 14 frames of 256 samples: the older ones fade */
                static int16_t lr[2 * 256];
                for (int t = 0; t < 14; t++) {
                    for (int i = 0; i < 256; i++) {
                        const float a = 6.2831853f * (float)(t * 256 + i) / 1024.0f * 3.0f;
                        lr[2 * i] = (int16_t)(18000.0f * sinf(a)); lr[2 * i + 1] = (int16_t)(14000.0f * sinf(a * 1.0f + 0.9f) + 4000.0f * sinf(a * 5.0f));
                    }
                    pc_vd_phase(&D, v, lr, 256);
                }
            }
            else for (int t = 0; t < 100; t++) {                  /* a moving peak and a steady bass, 100 rows */
                float fl[64];
                for (int i = 0; i < 64; i++) {
                    float dd = (float)i - (8.0f + 0.4f * (float)t);
                    fl[i] = 0.7f * expf(-dd * dd / 18.0f) + (i < 6 ? 0.5f : 0.0f) + 0.1f * fabsf(sinf((float)(i + t) * 0.5f));
                    if (fl[i] < 0.12f) fl[i] = 0;
                }
                pc_vd_fall(&D, v, fl);
            }
            char path[512];
            snprintf(path, sizeof path, "%s/%s_%d_%dx%d.rgb565", argv[1], pc_vd_kind_name((pc_vd_kind_t)k), v, w, h);
            FILE *f = fopen(path, "wb");
            if (!f || fwrite(px, 2, (size_t)w * h, f) != (size_t)w * h) { fprintf(stderr, "write %s failed\n", path); return 1; }
            fclose(f);
            printf("%s\n", path);
        }
    }
    free(px); free(map);
    return 0;
}
