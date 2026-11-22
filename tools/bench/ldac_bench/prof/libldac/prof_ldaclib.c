// SPDX-License-Identifier: LGPL-2.1-or-later
/* LDAC_PROF build only (idf.py -DLDAC_PROF=1): the same libldac sources as components/libldac/src/ldaclib.c
 * (fixed point, same include order), with cycle timers wrapped around the encoder stages. The wrappers only call the
 * real functions, so the bitstream is unchanged (main.c prints a checksum per case to compare with the normal build).
 * The shared component itself is not touched. */
#include "esp_cpu.h"
#include "ldac.h"

uint32_t ldac_prof_cyc[8];   /* set_pcm, mdct, frame_status, norm, alloc, quant_spec, quant_res, pack */
#define PROF_T0() uint32_t t0_ = esp_cpu_get_cycle_count()
#define PROF_T1(i) ldac_prof_cyc[i] += esp_cpu_get_cycle_count() - t0_

#include "tables_ldac.c"
#include "tables_sigproc_fixp_ldac.c"
#include "setpcm_fixp_ldac.c"
#include "func_fixp_ldac.c"
#include "bitalloc_sub_ldac.c"
#include "memory_ldac.c"

static void W_set_input_pcm_ldac(SFINFO *s, char *p[], LDAC_SMPL_FMT_T f, int n) { PROF_T0(); set_input_pcm_ldac(s, p, f, n); PROF_T1(0); }
static void W_proc_mdct_ldac(SFINFO *s, int n);
static int W_ana_frame_status_ldac(SFINFO *s, int n);
static int W_encode_ldac(SFINFO *, int, int, int, int, int, int, int);
static int W_pack_raw_data_frame_ldac(SFINFO *, STREAM *, int *, int *);
#define set_input_pcm_ldac W_set_input_pcm_ldac
#define proc_mdct_ldac W_proc_mdct_ldac
#define ana_frame_status_ldac W_ana_frame_status_ldac
#define encode_ldac W_encode_ldac
#define pack_raw_data_frame_ldac W_pack_raw_data_frame_ldac
#include "ldaclib_api.c"
#undef set_input_pcm_ldac
#undef proc_mdct_ldac
#undef ana_frame_status_ldac
#undef encode_ldac
#undef pack_raw_data_frame_ldac

#include "mdct_fixp_ldac.c"
#include "sigana_fixp_ldac.c"
#include "quant_fixp_ldac.c"
#include "bitalloc_ldac.c"
#include "pack_ldac.c"

static void W_norm_spectrum_ldac(AC *a) { PROF_T0(); norm_spectrum_ldac(a); PROF_T1(3); }
static int W_alloc_bits_ldac(AB *b) { PROF_T0(); int r = alloc_bits_ldac(b); PROF_T1(4); return r; }
static void W_quant_spectrum_ldac(AC *a) { PROF_T0(); quant_spectrum_ldac(a); PROF_T1(5); }
static void W_quant_residual_ldac(AC *a) { PROF_T0(); quant_residual_ldac(a); PROF_T1(6); }
#define norm_spectrum_ldac W_norm_spectrum_ldac
#define alloc_bits_ldac W_alloc_bits_ldac
#define quant_spectrum_ldac W_quant_spectrum_ldac
#define quant_residual_ldac W_quant_residual_ldac
#include "encode_ldac.c"
#undef norm_spectrum_ldac
#undef alloc_bits_ldac
#undef quant_spectrum_ldac
#undef quant_residual_ldac

static void W_proc_mdct_ldac(SFINFO *s, int n) { PROF_T0(); proc_mdct_ldac(s, n); PROF_T1(1); }
static int W_ana_frame_status_ldac(SFINFO *s, int n) { PROF_T0(); int r = ana_frame_status_ldac(s, n); PROF_T1(2); return r; }
static int W_encode_ldac(SFINFO *s, int a, int b, int c, int d, int e, int f, int g) { return encode_ldac(s, a, b, c, d, e, f, g); }
static int W_pack_raw_data_frame_ldac(SFINFO *s, STREAM *p, int *l, int *u) { PROF_T0(); int r = pack_raw_data_frame_ldac(s, p, l, u); PROF_T1(7); return r; }
