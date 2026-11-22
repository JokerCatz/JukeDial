// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_viz.h"
#include "pc/pc_voice.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Software renderer of the spectrum page (spec 2026-11-11 section 3): draws RGB565 (host byte order) into a caller-supplied
 * buffer and returns the rectangle that changed. No heap, no LVGL; single-precision only (device FPU). The caller sets
 * `fresh` (pc_vd_set_colors does) when it changes anything but the input values; kind, variant and bar width are also
 * remembered in `tag`/`extra` and a change there redraws everything (prev_a/prev_b belong entirely to the style). */
typedef enum { PC_VD_BARS, PC_VD_RING, PC_VD_SCOPE, PC_VD_FALL, PC_VD_PHASE, PC_VD_VOICES, PC_VD_ROWS, PC_VD_ROLL, PC_VD_LYRICS, PC_VD_KINDS } pc_vd_kind_t;
/* ROWS (voice rows) and ROLL (6 s piano roll) are drawn here from the voice snapshot (pc_voices_t); LYRICS has no picture: the page shows the LRC text with LVGL labels (pc_vd_size gives 0 x 0) */
const char *pc_vd_kind_name(pc_vd_kind_t k);            /* "bars" "ring" "scope" "fall" "phase" "voices" "rows" "roll" "lyrics" ("vu" is gone: unknown) */
pc_vd_kind_t pc_vd_kind_parse(const char *name);        /* unknown or NULL -> PC_VD_BARS */
#ifndef PC_VD_ROUND_SCREEN
#define PC_VD_ROUND_SCREEN 1     /* 1: the looks shaped for a round screen are offered (bars 圓鏡 and 拱形, the full-size waterfall); a square-screen build sets 0 */
#endif
int pc_vd_variants_on(pc_vd_kind_t k, bool round);      /* round: 6 4 3 3 2 3 1 2 2; square: 4 4 3 3 2 3 1 2 2 (rows 1, roll: colour per voice / by pitch, lyrics: 3 or 5 lines) */
int pc_vd_variants(pc_vd_kind_t k);                     /* = pc_vd_variants_on(k, PC_VD_ROUND_SCREEN) */
const char *pc_vd_var_label(pc_vd_kind_t k, int var, bool en);   /* out of range -> the first one */
void pc_vd_size(pc_vd_kind_t k, int var, int *w, int *h);        /* image size of this variant (the full-size waterfall is pc_vd_size_fall) */
#define PC_VD_RING_SZ 340                               /* the ring picture (kind ring) and the circular scope (scope variant 2) are square images of this size */
#define PC_VD_CIRCLE_SZ 340
#define PC_VD_RING_IN 60                                /* the ring's inner and outer radius (the bars grow between them) */
#define PC_VD_RING_OUT 168
#define PC_VD_MAX_PX (340 * 340)                        /* the largest image (ring, circular scope); the page allocates this much */

/* Colour mapping of bars and ring (spec 2026-11-12): how a bar takes its colours from the ramp. HEIGHT = by distance from the
 * foot (the default), TIP = flame at the top (hottest colour at the tip, fading to the ramp start over 42% of full length),
 * BAND = one colour per bar by frequency band, LEVEL = one colour per bar by its height. Other kinds ignore it. */
typedef enum { PC_VD_MAP_HEIGHT, PC_VD_MAP_TIP, PC_VD_MAP_BAND, PC_VD_MAP_LEVEL, PC_VD_MAPS } pc_vd_map_t;
const char *pc_vd_map_name(pc_vd_map_t m);              /* "height" "tip" "band" "level" (the settings value); out of range -> "height" */
pc_vd_map_t pc_vd_map_parse(const char *name);          /* unknown or NULL -> PC_VD_MAP_HEIGHT */
const char *pc_vd_map_label(pc_vd_map_t m, bool en);    /* out of range -> Height */
bool pc_vd_kind_has_map(pc_vd_kind_t k);                /* PC_VD_BARS, PC_VD_RING and PC_VD_VOICES */

typedef struct { int x, y, w, h; } pc_vd_rect_t;        /* w == 0: nothing changed */
typedef struct {
    uint16_t *px; int w, h;                             /* RGB565, row major, w pixels per row */
    uint16_t lut[256], bg, fg, dim, quiet;              /* 256-step ramp and the theme colours; quiet = the waterfall's level 0 (the page background) */
    bool fresh;                                         /* true: the next frame redraws everything and returns the whole image */
    int16_t prev_a[320], prev_b[320];                   /* each style's memory of the last frame */
    uint8_t fall_hist[170][64];                         /* the full-size waterfall: the level (0..255) of every column for the 170 newest rows, [0] the newest (cleared with the picture) */
    uint16_t fall_col[256];                             /* ... and the colour of each level (fall_color), built when the style starts */
    int16_t lim[48];                                    /* the round bar looks: full length of each column, computed when the style starts */
    int16_t tag, extra;                                 /* kind/variant and bar width of the last frame: a change redraws everything */
    uint8_t peaks_off;                                  /* 1: no peak mark (the falling caps) in bars, ring and voices; 0 (the default, also after pc_vd_init) draws them; set through pc_vd_set_peaks */
    uint8_t order;                                      /* pc_vd_order_t of the ring kind; set through pc_vd_set_order */
    uint16_t ph_peak;                                   /* the phase scope's recent peak of L +- R (auto gain) */
    uint8_t map;                                        /* pc_vd_map_t; set through pc_vd_set_map */
} pc_vd_t;
void pc_vd_init(pc_vd_t *d, uint16_t *px, int w, int h);                   /* default colours, image cleared, fresh */
/* Paints the whole image with the page background bg, for every kind (k is kept for the callers). pc_vd_init
 * paints with the DEFAULT theme's bg (its colours are only known after pc_vd_set_colors), so a caller that shows the image before the first
 * frame is drawn (an lv_image over this buffer) calls this after set_colors: the first thing on screen is then the page's own colour, not a
 * near-black block. Does not touch `fresh` (the first frame still redraws everything). */
void pc_vd_blank(pc_vd_t *d, pc_vd_kind_t k);
void pc_vd_set_colors(pc_vd_t *d, pc_ramp_t ramp, const pc_theme_t *t);    /* rebuilds lut; sets fresh (NULL theme = default) */
void pc_vd_set_peaks(pc_vd_t *d, bool on);                                 /* the falling peak marks of bars, ring and voices (every variant): default on; fresh only when the value changes */
void pc_vd_set_map(pc_vd_t *d, pc_vd_map_t m);                             /* out of range = HEIGHT; fresh only when the value changes */

/* level/peak: 64 values 0..1 (NaN and negatives = 0, above 1 = 1). bar_w: bar width px (gap is 2); invalid -> 7.
 * Variants 0..3 on the 300x200 picture; the round-screen looks 4 (Lens) and 5 (Arch, bass in the middle) on the PC_VD_RING_SZ square picture
 * (another size: nothing drawn; a build with PC_VD_ROUND_SCREEN 0 reads 4 and 5 as 0). Both have up to 48 columns spanning about +-157 px, and each
 * column's full length follows the circle (Lens: 0.92 * sqrt(170^2 - dx^2) either side of the centre row; Arch: 0.92 * the room between the
 * baseline at picture row 213 and the arc of radius 176); the lengths are computed when a style starts and kept in `lim`. */
pc_vd_rect_t pc_vd_bars(pc_vd_t *d, int var, const float *level, const float *peak, int bar_w);

/* Ring map: pixels of the annulus grouped by wedge (64, wedge 0 starts straight down and runs clockwise on screen) and
 * by 1 px distance bucket, so a wedge's bar is a prefix of its list. Memory is the caller's (PSRAM), 2-byte aligned. */
size_t pc_vd_ringmap_bytes(int w, int h);                                  /* what the caller allocates (254126 for 340x340; the map holds every pixel of the image at most, 2 bytes each) */
/* gap_px 0, 2 or 4 (anything else = 0): every pixel closer than gap_px / 2 px to a wedge boundary line is left out of the map, so neighbouring wedges are
 * a constant gap_px apart (like the bars' 2 px gap); 0 is the plain map. The map remembers it (pc_vd_ringmap_gap). cap too small -> false, nothing written. */
bool pc_vd_ringmap_build(void *map, size_t cap, int w, int h, float r_in, float r_out, int gap_px);
int pc_vd_ringmap_gap(const void *map);                                    /* the gap the map was built with (0, 2, 4); NULL or not a map: 0 */
/* Which band a ring position shows (the ring kind; voices ignore it). n = the variant's positions: 64, or 32 for the mirrored variant (pairs of bands).
 * SEQ: band b at position b. ZIGZAG: b at b / 2 when b is even, n - 1 - (b - 1) / 2 when odd (wedge 0 starts straight down: the bass at the bottom, the bands
 * alternate left and right, the treble meets at the top). SPREAD: b at (b * s) % n, s = 39 for n = 64 and 19 for n = 32 (odd, so a permutation; neighbours in
 * frequency land far apart). The colour map BAND follows the band, not the position. */
typedef enum { PC_VD_ORD_SEQ, PC_VD_ORD_ZIGZAG, PC_VD_ORD_SPREAD, PC_VD_ORDS } pc_vd_order_t;
const char *pc_vd_order_name(pc_vd_order_t o);            /* "seq" "zigzag" "spread"; out of range -> "seq" */
pc_vd_order_t pc_vd_order_parse(const char *name);        /* unknown or NULL -> PC_VD_ORD_SEQ */
const char *pc_vd_order_label(pc_vd_order_t o, bool en);  /* 順序 交錯 打散 / Sequential Alternate Scatter */
void pc_vd_set_order(pc_vd_t *d, pc_vd_order_t o);        /* out of range = SEQ; fresh only when the value changes */
int pc_vd_order_pos(pc_vd_order_t o, int band, int n);    /* the position of band (0..n-1), n 64 or 32 (any other n: band) */
int pc_vd_order_band(pc_vd_order_t o, int pos, int n);    /* its inverse: the band shown at pos */
pc_vd_rect_t pc_vd_ring(pc_vd_t *d, int var, const void *map, const float *level, const float *peak);
/* mono: n samples -1..1 (newest last; NaN = 0, out of range clamped). Starts at the first negative-to-positive zero crossing
 * among the last 2*cols samples and takes cols points (none found: the last cols). cols: variants 0 and 1 (line, filled): 300
 * points centred in a 320x160 image (columns 10..309, 2 px line, row 79/80 = zero); variant 2 (circle, PC_VD_CIRCLE_SZ square): a full turn of
 * 256 points, radius 99.2 + sample * 56.7 (the old 70 + sample * 40 at 240 px, scaled by 340 / 240), centre 169.5. Fewer than cols samples: the missing ones count as 0. n < 1, NULL or another image size:
 * nothing is drawn and the rectangle is empty. */
pc_vd_rect_t pc_vd_scope(pc_vd_t *d, int var, const float *mono, int n);
/* Waterfall, 256x200: every call scrolls by one 2 px row. Variant 0 new row on top scrolling down, 1 new row at the bottom
 * scrolling up, 2 mirrored (32 segments of two bands by max, bass in the middle, scrolling down). A segment is 4 px wide
 * (64 columns, also when mirrored); colour lut[level * 255]; level 0 is bg; levels 1..47 (of 255) blend from bg to lut[0], the ramp's first colour; 48..255 run the whole ramp (lut[(k-48)*255/207]). A style change or a ramp change clears the picture to bg. level: 64 values. Returns the whole image; another
 * image size or NULL: nothing drawn, empty rectangle. */
pc_vd_rect_t pc_vd_fall(pc_vd_t *d, int var, const float *level);
/* The picture size of the waterfall: the 256x200 box (full false, always when PC_VD_ROUND_SCREEN is 0) or, on a round screen, the full-size 340x340
 * picture. That one is a disc of radius 170: row y of the picture is drawn across its chord only ([170 - hw, 170 + hw), hw = floor(sqrt(170^2 - (y - 169.5)^2)))
 * and the pixels outside stay bg. The 64 bins are by x over the whole width (x * 64 / 340), so the columns stay vertical; a row is 2 px high, the colour
 * rule is the box's, the three variants keep their meaning. Every call shifts the 170 rows of history (`fall_hist`) by one and redraws the rows from it, so a
 * row that moves into a wider chord shows its own data there (no smear), and the corners are never written. */
void pc_vd_size_fall(bool full, int *w, int *h);
/* Phase scope, PC_VD_PHASE_SZ square. lr: interleaved L/R int16, frames of them (the newest 1024 at most; frames <= 0 or NULL:
 * nothing new is drawn, the picture still fades). x = cx + (L - R) * k, y = cy - (L + R) * k with cx = cy = 149.5 and k such
 * that a full-scale mono signal ends 2 px inside the edge (the size and k scale together: k = 295 / 65536 px per level, times an auto gain: the recent peak of L +- R is brought to 3/4 of the edge, never below 1x, at most 12x). Every call first pulls the whole picture towards bg (every colour
 * channel's difference to bg times 3/4, truncated towards zero), then draws the points (variant 0) or lines between neighbouring
 * points (variant 1, at most 128 segments, evenly picked). Returns the whole image; another image size: empty rectangle. */
#define PC_VD_PHASE_SZ 300
pc_vd_rect_t pc_vd_phase(pc_vd_t *d, int var, const int16_t *lr, int frames);
/* Voices (spec amendment 2026-11-12, Task 10): one bar per sound-chip voice. n 1..PC_VOICES_MAX (else nothing is drawn); level/peak: n values 0..1 (NaN and
 * negatives = 0, above 1 = 1; from pc_viz_follow); names: n short labels (pc_voices_t.name; NULL: no names), each drawn under its bar in a 5x7 font at 2x
 * (1x when the widest does not fit). Variants 0 (solid) and 1 (segments): the bars picture, 300x200: the bars grow in the top 180 rows, n bars evenly spread, each at
 * most 40 px wide with at least 6 px between, a peak cap as the bars have; the colour mapping applies as it does to bars. Variant 2: the ring picture and its map
 * (`map` from pc_vd_ringmap_build; NULL or another size: nothing drawn), the 64 wedges split into n groups with the last wedge of each a gap, no names. A change of n or of a name
 * redraws everything. Another image size: nothing drawn, empty rectangle. */
pc_vd_rect_t pc_vd_voices(pc_vd_t *d, int var, const void *map, int n, const float *level, const float *peak, const char (*names)[4]);

/* ---- voice rows and piano roll (visualiser phase 1, 2026-11-17) ----
 * The page keeps one pc_vd_vstate_t (PSRAM, about 10 KB) and calls pc_vd_vstate_feed() once per frame with the snapshot, then draws one of the two views
 * on a 300 x 200 picture. Everything is ASCII (5x7 font): the voice's short name, a key-on lamp (fades over about 0.18 s), the level bar with a held cap,
 * the note name ("C#4", "--" none), the pan (a marker on a 1 px track under the bar; nothing where the chip has no pan) and a mark: M muted, S solo.
 * mstate: per voice pc_vm_state_t (0 on, 1 muted, 2 solo), NULL = all on; sel: the voice the style ring's cursor is on (-1 none): a box round its row.
 * Rows: n <= 8 one column; n <= 16 two columns of 8; n <= 32 two columns of 16. Nothing here allocates; both return the whole image changed. */
#define PC_VD_ROLL_COLS 150                              /* 150 columns x 2 px = 300 px; one column per 40 ms = 6 s */
#define PC_VD_ROLL_MS 40
typedef struct {
    uint8_t n;                                           /* voices the state was fed with (a change clears the roll) */
    uint8_t last_strike[PC_VOICES_MAX];
    uint8_t flash[PC_VOICES_MAX];                        /* 255 at a strike, falls to 0 in about 0.18 s */
    int16_t head;                                        /* the newest roll column */
    uint32_t col_ms;                                     /* the time of the newest column */
    uint8_t note[PC_VD_ROLL_COLS][PC_VOICES_MAX];        /* 0 none, 1..84 = C1..B7 (MIDI 24..107), 255 an unpitched sound */
    uint8_t lv[PC_VD_ROLL_COLS][PC_VOICES_MAX];          /* bit 7 a strike (note start), bits 0..6 the level / 2 */
} pc_vd_vstate_t;
void pc_vd_vstate_reset(pc_vd_vstate_t *st);
void pc_vd_vstate_feed(pc_vd_vstate_t *st, const pc_voices_t *v, uint32_t now_ms, uint32_t dt_ms);
pc_vd_rect_t pc_vd_rows(pc_vd_t *d, pc_vd_vstate_t *st, const pc_voices_t *v, const float *level, const float *peak, const uint8_t *mstate, int sel);
pc_vd_rect_t pc_vd_roll(pc_vd_t *d, int var, const pc_vd_vstate_t *st, const pc_voices_t *v, const uint8_t *mstate, int sel);
#ifdef __cplusplus
}
#endif
