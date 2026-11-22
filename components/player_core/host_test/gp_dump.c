// SPDX-License-Identifier: LGPL-2.1-or-later
/* gp_dump pack.zip [preview.ppm [width]]: checks a glyph pack with the device parser, prints its tree, and draws every
 * node (wrapped to width, centred, 2 lines max, both sizes) into a PPM the way app/main/ui/glyph_text.c blits it. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc/pc_glyphpack.h"

static uint8_t *img;
static int IW, IH;
static void blend(int x, int y, int r, int g, int b, int a)
{
    if (x < 0 || y < 0 || x >= IW || y >= IH) return;
    uint8_t *p = img + 3 * (y * IW + x);
    p[0] = (uint8_t)((r * a + p[0] * (255 - a)) / 255);
    p[1] = (uint8_t)((g * a + p[1] * (255 - a)) / 255);
    p[2] = (uint8_t)((b * a + p[2] * (255 - a)) / 255);
}

static int draw_text(const pc_gp_t *gp, int size, const uint16_t *ids, uint32_t n, int x0, int y0, int width)
{
    pc_gp_line_t L[2];
    pc_gp_size_t sz;
    pc_gp_size(gp, size, &sz);
    int nl = pc_gp_layout(gp, size, ids, n, width, 2, L);
    static uint8_t buf[256 * 256 * 3];
    for (int k = 0; k < nl; k++) {
        int pen = x0 + (width - L[k].width) / 2, top = y0 + k * sz.line_h;
        for (uint32_t i = 0; i <= L[k].len; i++) {
            uint16_t id = i < L[k].len ? ids[L[k].start + i] : (L[k].ellipsis ? pc_gp_ellipsis(gp) : PC_GP_NONE);
            pc_gp_glyph_t g;
            if (pc_gp_glyph(gp, size, id, &g)) continue;
            if (pc_gp_glyph_read(gp, size, id, buf, sizeof buf) == (int)g.bytes)
                for (int y = 0; y < g.h; y++)
                    for (int x = 0; x < g.w; x++) {
                        int px = pen + g.x + x, py = top + g.y + y, k2 = y * g.w + x;
                        if (g.fmt == PC_GP_FMT_A8) blend(px, py, 0xF0, 0xE8, 0xD8, buf[k2]);
                        else if (g.fmt == PC_GP_FMT_RGB565A8) {
                            unsigned v = buf[2 * k2] | buf[2 * k2 + 1] << 8;
                            blend(px, py, (v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3, buf[2 * g.w * g.h + k2]);
                        }
                    }
            pen += g.adv;
        }
    }
    return nl * sz.line_h;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: gp_dump pack.zip [preview.ppm [width]]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    pc_reader_t r;
    pc_gp_t *gp;
    if (!f || pc_reader_file(&r, f)) { perror(argv[1]); return 1; }
    int rc = pc_gp_open(&r, NULL, &gp);
    if (rc) { printf("%s: not a pack, rc=%d\n", argv[1], rc); return 1; }
    printf("%s: kind %d, %d sizes, %u glyphs, %u images, %u nodes, index block %u B\n", argv[1], pc_gp_kind(gp), pc_gp_nsizes(gp),
           (unsigned)pc_gp_glyph_count(gp), (unsigned)pc_gp_image_count(gp), (unsigned)pc_gp_node_count(gp), (unsigned)pc_gp_block_bytes(gp));
    uint32_t bytes[PC_GP_MAX_SIZES] = { 0 };
    for (int s = 0; s < pc_gp_nsizes(gp); s++)
        for (uint32_t g = 0; g < pc_gp_glyph_count(gp); g++) { pc_gp_glyph_t m; pc_gp_glyph(gp, s, g, &m); bytes[s] += m.bytes; }
    for (int s = 0; s < pc_gp_nsizes(gp); s++) { pc_gp_size_t z; pc_gp_size(gp, s, &z); printf("  size %d: %u px, line %u, bitmaps %u B\n", s, z.px, z.line_h, (unsigned)bytes[s]); }
    for (uint32_t i = 0; i < pc_gp_node_count(gp); i++) {
        pc_gp_node_t n;
        pc_gp_node(gp, i, &n);
        printf("  node %u: %u glyphs, %u children from %u, weight %u\n", (unsigned)i, n.len, n.nchild, (unsigned)n.first_child, n.weight);
    }
    if (argc >= 3) {
        int width = argc >= 4 ? atoi(argv[3]) : 200, nn = (int)pc_gp_node_count(gp);
        IW = 2 * (width + 20);
        IH = 0;
        pc_gp_size_t z0, z1;
        pc_gp_size(gp, 0, &z0);
        pc_gp_size(gp, pc_gp_nsizes(gp) - 1, &z1);
        IH = nn * (2 * (z1.line_h > z0.line_h ? z1.line_h : z0.line_h) + 6) + 10;
        img = calloc((size_t)IW * IH, 3);
        for (int i = 0; i < IW * IH; i++) { img[3 * i] = 0x18; img[3 * i + 1] = 0x1D; img[3 * i + 2] = 0x24; }
        int y = 5;
        for (int i = 0; i < nn; i++) {
            pc_gp_node_t n;
            pc_gp_node(gp, (uint32_t)i, &n);
            int h0 = draw_text(gp, 0, n.text, n.len, 10, y, width);
            int h1 = draw_text(gp, pc_gp_nsizes(gp) - 1, n.text, n.len, width + 30, y, width);
            y += (h0 > h1 ? h0 : h1) + 6;
        }
        FILE *o = fopen(argv[2], "wb");
        fprintf(o, "P6 %d %d 255\n", IW, y);
        fwrite(img, 3, (size_t)IW * y, o);
        fclose(o);
        free(img);
    }
    pc_gp_close(gp);
    fclose(f);
    return 0;
}
