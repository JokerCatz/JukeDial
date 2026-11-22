// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Sample kits (instrument spec 12.4 / decision 13.4), two formats:
 *  kit.txt  one mapping a line, "note=file.wav" (note: 0..127 or a name like C2 / F#3 / Bb1, C4 = 60);
 *           "pad1=36" .. "pad8=" put a note on a pad; '#' starts a comment. Samples play to their end
 *           (one-shot: drums).
 *  .sfz     subset: <region> with sample= key= lokey= hikey= pitch_keycenter=; <group> / <global> values are
 *           inherited by the regions after them, <control> default_path= is prefixed; loop_mode=one_shot is
 *           kept (drum SFZs use it), every other opcode is ignored. Regions stop on note-off with a short fade.
 * Sample names keep their sub-folders; '\' becomes '/'. */
#define PC_KIT_REGIONS 128
#define PC_KIT_PATH 96
typedef struct { char file[PC_KIT_PATH]; uint8_t lokey, hikey, center; bool one_shot; } pc_kit_region_t;
typedef struct { pc_kit_region_t r[PC_KIT_REGIONS]; int n; int16_t pad[8]; } pc_kit_def_t;   /* pad: note, -1 */

int  pc_kit_note(const char *s);                  /* "36" / "C2" / "c#4" / "Eb-1" -> 0..127, else -1 */
int  pc_kit_parse_txt(const char *text, pc_kit_def_t *k);   /* regions read; pads not named take defaults */
int  pc_kit_parse_sfz(const char *text, pc_kit_def_t *k);
/* pads without an assignment take the regions' key centres not already on a pad, lowest first */
void pc_kit_default_pads(pc_kit_def_t *k);
/* the region that plays note (the last one listed wins where they overlap: later lines refine), -1 */
int  pc_kit_find(const pc_kit_def_t *k, int note);
int  pc_kit_pad_of(const pc_kit_def_t *k, int note);       /* the pad holding note, -1 */

/* WAV: 16-bit PCM, 1 or 2 channels, 8..96 kHz (the engine resamples). hdr = the file's first bytes. */
typedef struct { uint32_t rate, data_off, data_len; uint16_t channels; } pc_wav_t;
enum { PC_WAV_OK = 0, PC_WAV_NOT_WAV = -1, PC_WAV_FORMAT = -2, PC_WAV_SHORT = -3 };
int  pc_wav_parse(const uint8_t *hdr, uint32_t n, pc_wav_t *w);   /* PC_WAV_SHORT: give it more of the file */
/* data_len clamped to what the file holds (a streamed 0xFFFFFFFF size, a cut file); PC_WAV_SHORT: no frame */
int  pc_wav_fit(pc_wav_t *w, uint64_t file_size);
#ifdef __cplusplus
}
#endif
