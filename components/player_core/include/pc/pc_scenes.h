// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_zip.h"
#ifdef __cplusplus
extern "C" {
#endif
/* White-noise scenes from sleepmixer's default_scenes.json (spec 4.3 / 5.5). */
#define PC_SCENE_MAX_LAYERS 16   /* per scene; at most 3 play at once (pc_layerset) */
typedef struct { const pc_zip_entry_t *entry; float volume; char sha[65]; } pc_layer_t;
typedef struct { char id[64]; char name[64]; char en[64]; int nlayers; pc_layer_t layer[PC_SCENE_MAX_LAYERS]; int missing; bool custom; } pc_scene_t;
/* resolved_sounds > PC_SCENE_MAX_LAYERS -> dropped; a layer is "<sha>.ogg|.mp3|.flac" in ents, else (absent or .m4a) missing++;
 * missing += len(unresolved_sound_ids); no playable layer -> dropped; name = zh_tw_name, else en_name.
 * *out is malloc'd (free it). -EINVAL on bad JSON. */
int pc_scenes_parse(const char *json, const pc_zip_entry_t *ents, int nents, bool custom, pc_scene_t **out, int *n);
int pc_scenes_save_custom(const pc_scene_t *s, int n, const char *path);   /* same JSON shape, tmp + rename */

/* Sounds of library.muk's seed.json (spec 5.6, custom scenes): one per playable "<sha>.<ext>" (ogg/mp3/flac) in ents.
 * cats: bit k = category k of the cats[] table (first-appearance order, at most PC_SOUND_MAX_CATS); 0 = uncategorised.
 * *out is sorted by sha (pc_sound_find) and malloc'd. -EINVAL on bad JSON. */
#define PC_SOUND_MAX_CATS 32
typedef struct { const pc_zip_entry_t *entry; char sha[65]; char name[64]; char en[64]; uint32_t cats; } pc_sound_t;
typedef struct { char name[PC_SOUND_MAX_CATS][32]; char en[PC_SOUND_MAX_CATS][32]; int n; } pc_sound_cats_t;   /* en: seed.json "category_en" (parallel to "category"), "" if none */
/* The name to show: the English one (en_name / category_en) in English, else the zh-TW name through TZ() (簡中 from data.tsv). en may be "". */
const char *pc_pick_name(const char *tw, const char *en);
int pc_sounds_parse(const char *json, const pc_zip_entry_t *ents, int nents, pc_sound_t **out, int *n, pc_sound_cats_t *cats);
const pc_sound_t *pc_sound_find(const pc_sound_t *s, int n, const char *sha);
#ifdef __cplusplus
}
#endif
