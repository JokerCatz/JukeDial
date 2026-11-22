// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes the decoder backends that need codec libraries: GME (chip music), the esp_audio_codec simple decoder (ogg, aac, m4a), libxmp, libmpcdec, libtta. */
/* Behaviour: none; the tables exist so mp3_ctl and chip_ctl link, and every open fails (no sound is produced in the simulator). */
/* Empty: all of it. */
#include "backend_gme.h"
#include "backend_sdec.h"

static int open_fail(void **h, const pc_reader_t *r, uint32_t start_off) { (void)h; (void)r; (void)start_off; return -1; }
#define NO_BACKEND { .open = open_fail }
const pc_backend_t backend_gme = NO_BACKEND;
const pc_backend_t backend_aac = NO_BACKEND, backend_ogg = NO_BACKEND, backend_m4a = NO_BACKEND;
const pc_backend_t pc_backend_xmp = NO_BACKEND, pc_backend_mpc = NO_BACKEND, pc_backend_tta = NO_BACKEND, pc_backend_hvl = NO_BACKEND, pc_backend_sid = NO_BACKEND, pc_backend_opl = NO_BACKEND;

int backend_gme_probe(const void *data, size_t size, int *tracks, char *system, size_t ssys, char *game, size_t sgame)
{
    (void)data; (void)size; (void)tracks; (void)system; (void)ssys; (void)game; (void)sgame;
    return -1;
}
int backend_gme_track_names(const void *data, size_t size, char (*names)[64], int max) { (void)data; (void)size; (void)names; (void)max; return 0; }
void backend_gme_kss_fm(int on) { (void)on; }

int pc_sid_check(const pc_reader_t *r) { (void)r; return -1; }
int pc_opl_check(const pc_reader_t *r) { (void)r; return -1; }
/* Amiga tone: the simulator has no libxmp; the setting and the UI row are real, the backend call is a no-op */
void pc_backend_xmp_set_amiga(int mode) { (void)mode; }
bool pc_backend_xmp_amiga_active(void) { return false; }
