// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Cover thumbnails (spec 4.5): centre square crop, 240x240 RGB565. */
#define PC_THUMB_SIZE 240
uint32_t pc_thumb_hash(const char *path);   /* FNV-1a 32 */

/* hires T8c: covers are kept in one pack on the card (pc_thumbpak.h). An entry's key: */
uint64_t pc_thumb_key(const char *path, uint32_t apic_off, uint32_t apic_len);           /* the track's embedded picture (FNV-1a 64: path, off, len) */
uint64_t pc_thumb_image_key(const char *image_path, uint32_t size, int64_t mtime);        /* an image file (its own domain: never equal to an APIC key by design) */
/* A track without an embedded picture: of names (the image files of its folder, as listed), the one it shows: "<track base>.jpg/.jpeg/.png"
 * (shared false), else cover.* then folder.* with the same extensions (shared true: every track of the folder uses that entry); any case;
 * .png only when png (the build decodes PNG). -1 = none. */
int pc_thumb_pick_image(const char *track_name, const char *const *names, int n, bool png, bool *shared);
bool pc_thumb_is_image(const char *name, bool png);      /* .jpg / .jpeg (and .png), any case, with a name before the dot */
/* fix round 1 (dedupe): the data entry of an embedded picture is keyed by its bytes (FNV-1a 64 over them and their length, its own
 * domain); each track then has an alias from pc_thumb_key to it, so one album's identical pictures cost one entry */
uint64_t pc_thumb_content_key(const uint8_t *bytes, uint32_t len);
/* review I2: the pack is written only while the card keeps max(64 MB, 5 % of the card) free after the write */
uint64_t pc_thumb_space_margin(uint64_t total);
bool pc_thumb_space_ok(uint64_t total, uint64_t free_bytes, uint64_t need);
/* review I4 (ruling): the background pass and compaction run only while nothing plays, or after PC_THUMB_IDLE_PAUSE_MS of pause; during
 * playback (<= 48 kHz) only the current track's missing cover is made, on demand */
#define PC_THUMB_IDLE_PAUSE_MS 10000
bool pc_thumb_bg_allowed(bool playing, bool paused, uint32_t paused_ms);
/* the audio service's states (aud_state_t, same values; thumb_svc asserts it). Re-review m1: ENDED (between two tracks), FADED and ERROR
 * count like a pause: the background waits 10 s of them, it does not start in the gap before the next track opens */
enum { PC_THUMB_AUD_IDLE, PC_THUMB_AUD_PLAYING, PC_THUMB_AUD_ENDED, PC_THUMB_AUD_ERROR, PC_THUMB_AUD_FADED, PC_THUMB_AUD_PAUSED,
       PC_THUMB_AUD_NEAR, PC_THUMB_AUD_NEXT };
bool pc_thumb_paused_like(int aud_state);
/* fix round 2 (re-review I-1 / I-4): asked at the moment of every card write; NULL = write, else why it is held (nothing is written, the
 * work is tried again later): "hires" a source above 48 kHz is loaded, "opening" a newer play is queued and its rate is not known yet,
 * "playing" background work while something plays, "space_unknown" the free space was not read yet (it is read only while idle) */
const char *pc_thumb_write_gate(bool hires, bool opening, bool background, bool idle, bool space_known);
/* fix round 3 (re-review 2 I-A): "a source is opening" = the audio service's live generation (a play / mixer / queued next sets it, a stop or a
 * fade clears it to 0) is set and differs from the generation it has published (audio_svc_status().gen, also 0 after a stop) */
bool pc_thumb_opening(uint32_t live_gen, uint32_t published_gen);
/* M-B4: no cover is decoded (on demand or by the builder) while a source above 48 kHz plays. The source's own rate (audio_svc_info
 * src_rate, 0 when nothing is loaded), not the pipeline's: the card is read at the source's rate even when the pipeline halves it, and
 * pipe_rate keeps the DAC's last rate after a stop. */
#define PC_THUMB_PAUSE_ABOVE 48000
bool pc_thumb_hires_pause(uint32_t src_rate);
void pc_thumb_crop_scale_rgb888(const uint8_t *src, int w, int h, uint16_t *dst, int dst_size);   /* as pc_thumb_crop_scale, from RGB888 */
/* src w x h RGB565 -> centre square of the short side -> nearest-neighbour dst_size x dst_size */
void pc_thumb_crop_scale(const uint16_t *src, int w, int h, uint16_t *dst, int dst_size);
/* size x size RGB565 rotated clockwise by angle_deci/10 degrees about the centre (nearest neighbour), with the
 * disc baked in: pixels outside the inscribed circle are bg. src and dst must not overlap. (spec 5.7: the
 * now-playing cover is rotated once per step into a plain image, never per frame.) */
/* the cover for a track without one (2026-11-09): a record - grooves in disc/groove, a lighter sheen on two
 * opposite sectors (so the turning shows), a label of radius size/6 in label, a spindle hole in hole */
void pc_thumb_default(uint16_t *dst, int size, uint16_t disc, uint16_t groove, uint16_t sheen, uint16_t label, uint16_t hole);
void pc_thumb_rotate(const uint16_t *src, uint16_t *dst, int size, int angle_deci, uint16_t bg);
/* the same, bilinear, with the disc's rim blended into bg (2026-11-09: picture quality over cost) */
void pc_thumb_rotate_smooth(const uint16_t *src, uint16_t *dst, int size, int angle_deci, uint16_t bg);
/* the rotation straight into a dsize x dsize picture (dsize >= ssize: the square panel's larger cover, no second scaling by the GUI); smooth = bilinear */
void pc_thumb_rotate_scaled(const uint16_t *src, int ssize, uint16_t *dst, int dsize, int angle_deci, uint16_t bg, bool smooth);
#ifdef __cplusplus
}
#endif
