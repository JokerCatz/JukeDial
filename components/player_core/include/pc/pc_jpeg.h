// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* JPEG covers (hires task 8d): which decoder takes a file, and our own baseline decoder for the layouts the chip's decoders refuse.
 *
 * Both decoders on the S31 (the JPEG engine, esp_driver_jpeg, and esp_new_jpeg) look only at the luma's sampling factors - 1x1 (4:4:4),
 * 2x1 (4:2:2), 2x2 (4:2:0) - and assume 1x1 chroma. Anything else is refused ("Not supported JPEG format": luma H < V, e.g. ffmpeg's
 * yuvj444p, which writes 1x2 for all three components) or, with chroma that is not 1x1 (ffmpeg's yuvj422p: luma 2x2, chroma 1x2),
 * decoded as the wrong layout. pc_jpeg_probe reads the frame header and says which one takes the file:
 *   PC_JPEG_NATIVE      baseline (SOF0), 8 bit, gray 1x1 or YCbCr with chroma 1x1 and luma 1x1 / 2x1 / 2x2: the chip's decoders
 *   PC_JPEG_OWN         any other sequential Huffman layout (SOF0/SOF1, 8 bit, 1 or 3 components, one interleaved scan, factors 1..4
 *                       that divide the largest): pc_jpeg_decode
 *   PC_JPEG_PROGRESSIVE SOF2 (not decoded: marked bad with this reason)
 *   PC_JPEG_UNSUPPORTED lossless, hierarchical, arithmetic, 12 bit, CMYK, several scans, odd factors (marked bad with this reason)
 *   PC_JPEG_TOO_BIG     an OWN layout that declares more than PC_JPEG_MAX_SIDE per side or PC_JPEG_MAX_PIXELS (marked bad with this
 *                       reason, never tried again: a few-KB file must not hold the decoder for minutes - review I-1). NATIVE pictures have
 *                       no cap here (fix round 2): the chip's decoders scale while they decode (esp_new_jpeg 1/8), as before 8d; only
 *                       pc_jpeg_decode refuses them above the cap (E_TOO_BIG) when it is handed one
 *   PC_JPEG_BAD         not a JPEG frame header at all (no SOI / no SOF / truncated header, a component id twice)
 */
enum { PC_JPEG_NATIVE, PC_JPEG_OWN, PC_JPEG_PROGRESSIVE, PC_JPEG_UNSUPPORTED, PC_JPEG_BAD, PC_JPEG_TOO_BIG };
/* our decoder's cap: covers in the unusual layouts it takes are album art (at most ~3000 x 3000, rarely a 6000 x 6000 scan); camera and
 * phone photos (48 Mpx included) are common layouts and go to the scaling decoders, uncapped. At shift 3 it reads only the DC of the
 * full-resolution components (no transform), so a big picture is fast. */
#define PC_JPEG_MAX_SIDE 8192
#define PC_JPEG_MAX_PIXELS (6000u * 6000u)
typedef struct {
    int w, h;
    int ncomp;            /* frame components */
    int sof;              /* the SOF marker's low byte (0xC0..0xCF), -1 without one */
    int prec;             /* sample precision */
    int scan_comps;       /* components of the first scan */
    uint8_t hs[4], vs[4]; /* sampling factors per component */
} pc_jpeg_info_t;
int pc_jpeg_probe(const uint8_t *p, size_t n, pc_jpeg_info_t *info);
const char *pc_jpeg_route_name(int route);   /* "native" "own" "progressive" "unsupported" "bad" "too_big" */

/* Sizes from a declared header, in 64 bits (fix round 3: on the 32-bit S31 a crafted 65535 x 32769 header wraps a size_t product below any
 * limit). pc_jpeg_hw_bytes: the JPEG engine's output (padded to its MCU - the luma's factors -, 1 byte a pixel gray, else RGB565), *pw / *ph
 * the padded size when it fits 32 bits. pc_jpeg_min_bytes: the smallest picture any decoder here can make of it (1/8 scale, RGB565); above
 * PC_JPEG_BUDGET_BYTES it can never be made: too_big (marked bad), not "memory" (retried) */
#define PC_JPEG_BUDGET_BYTES (8u << 20)
uint64_t pc_jpeg_hw_bytes(const pc_jpeg_info_t *in, uint32_t *pw, uint32_t *ph);
uint64_t pc_jpeg_min_bytes(const pc_jpeg_info_t *in);

/* the scale our decoder uses: the largest shift 0..3 that keeps the short side >= min_short (0 when the picture is smaller) */
int pc_jpeg_shift_for(int w, int h, int min_short);
/* pc_jpeg_decode results */
enum { PC_JPEG_OK = 0, PC_JPEG_E_FORMAT = -1, PC_JPEG_E_PROGRESSIVE = -2, PC_JPEG_E_UNSUPPORTED = -3, PC_JPEG_E_NOMEM = -4, PC_JPEG_E_TOO_BIG = -5 };
typedef struct { void *(*alloc)(size_t n); void (*free)(void *p); void (*poll)(void); } pc_jpeg_mem_t;   /* NULL: malloc / free / no poll. poll (night3 B): called after every MCU and every converted band, so a CPU-bound caller can yield (pc_yield.h) */
/* A sequential Huffman JPEG (route NATIVE or OWN) -> RGB565 (little endian, as LVGL stores it), (w >> shift) x (h >> shift), each output
 * pixel the mean of its 2^shift square. *out from mem->alloc (the caller frees it with mem->free). Corrupt or missing entropy data after
 * a valid header decodes as far as it goes, the rest grey without a transform, again from the next restart marker (as libjpeg does); data
 * broken already in the first MCU row, or a broken header, is E_FORMAT. E_NOMEM is only ever a failed allocation (sizes are capped). Everything it uses comes from mem
 * (the S31: PSRAM, no internal RAM). */
int pc_jpeg_decode(const uint8_t *p, size_t n, int shift, const pc_jpeg_mem_t *mem, uint16_t **out, int *ow, int *oh);
#ifdef __cplusplus
}
#endif
