// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_scenes.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "pc/pc_i18n.h"

static const char *str(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

static const pc_zip_entry_t *find_layer(const pc_zip_entry_t *ents, int n, const char *sha)
{
    static const char *const ext[] = { "ogg", "mp3", "flac" };
    char name[80];
    for (int i = 0; i < 3; i++) {
        snprintf(name, sizeof name, "%s.%s", sha, ext[i]);
        const pc_zip_entry_t *e = pc_zip_find(ents, n, name);
        if (e) return e;
    }
    return NULL;
}

int pc_scenes_parse(const char *json, const pc_zip_entry_t *ents, int nents, bool custom, pc_scene_t **out, int *n)
{
    *out = NULL;
    *n = 0;
    cJSON *root = cJSON_Parse(json);
    const cJSON *arr = root ? cJSON_GetObjectItemCaseSensitive(root, "scenes") : NULL;
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return -EINVAL; }
    int cap = cJSON_GetArraySize(arr);
    pc_scene_t *s = calloc((size_t)(cap ? cap : 1), sizeof *s);
    if (!s) { cJSON_Delete(root); return -ENOMEM; }
    int k = 0;
    const cJSON *sc;
    cJSON_ArrayForEach(sc, arr) {
        const cJSON *res = cJSON_GetObjectItemCaseSensitive(sc, "resolved_sounds");
        if (!cJSON_IsArray(res) || cJSON_GetArraySize(res) > PC_SCENE_MAX_LAYERS) continue;
        pc_scene_t *d = &s[k];
        memset(d, 0, sizeof *d);
        snprintf(d->id, sizeof d->id, "%s", str(sc, "id"));
        const char *zh = str(sc, "zh_tw_name");
        snprintf(d->name, sizeof d->name, "%s", zh[0] ? zh : str(sc, "en_name"));
        snprintf(d->en, sizeof d->en, "%s", str(sc, "en_name"));
        d->custom = custom;
        const cJSON *l;
        cJSON_ArrayForEach(l, res) {
            const char *sha = str(l, "sha256");
            const pc_zip_entry_t *e = strlen(sha) == 64 ? find_layer(ents, nents, sha) : NULL;
            if (!e) { d->missing++; continue; }
            pc_layer_t *y = &d->layer[d->nlayers++];
            y->entry = e;
            snprintf(y->sha, sizeof y->sha, "%s", sha);
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(l, "volume");
            y->volume = cJSON_IsNumber(v) ? (float)v->valuedouble : 1.0f;
        }
        const cJSON *un = cJSON_GetObjectItemCaseSensitive(sc, "unresolved_sound_ids");
        if (cJSON_IsArray(un)) d->missing += cJSON_GetArraySize(un);
        if (d->nlayers) k++;
    }
    cJSON_Delete(root);
    *out = s;
    *n = k;
    return 0;
}

int pc_scenes_save_custom(const pc_scene_t *s, int n, const char *path)
{
    cJSON *root = cJSON_CreateObject(), *arr = cJSON_AddArrayToObject(root, "scenes");
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", s[i].id);
        cJSON_AddStringToObject(o, "zh_tw_name", s[i].name);
        cJSON_AddStringToObject(o, "en_name", s[i].name);
        cJSON *res = cJSON_AddArrayToObject(o, "resolved_sounds");
        for (int j = 0; j < s[i].nlayers; j++) {
            cJSON *l = cJSON_CreateObject();
            cJSON_AddStringToObject(l, "sha256", s[i].layer[j].sha);
            cJSON_AddNumberToObject(l, "volume", s[i].layer[j].volume);
            cJSON_AddItemToArray(res, l);
        }
        cJSON_AddItemToArray(arr, o);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return -ENOMEM;
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    int ok = f && fputs(text, f) >= 0;
    if (f && fclose(f)) ok = 0;
    cJSON_free(text);
    if (!ok) { remove(tmp); return -EIO; }
    remove(path);
    return rename(tmp, path) ? -errno : 0;
}

static int sound_cmp(const void *a, const void *b) { return strcmp(((const pc_sound_t *)a)->sha, ((const pc_sound_t *)b)->sha); }

int pc_sounds_parse(const char *json, const pc_zip_entry_t *ents, int nents, pc_sound_t **out, int *n, pc_sound_cats_t *cats)
{
    *out = NULL;
    *n = 0;
    memset(cats, 0, sizeof *cats);
    cJSON *root = cJSON_Parse(json);
    const cJSON *arr = root ? cJSON_GetObjectItemCaseSensitive(root, "tracks") : NULL;
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return -EINVAL; }
    int cap = cJSON_GetArraySize(arr);
    pc_sound_t *s = calloc((size_t)(cap ? cap : 1), sizeof *s);
    if (!s) { cJSON_Delete(root); return -ENOMEM; }
    int k = 0;
    const cJSON *t;
    cJSON_ArrayForEach(t, arr) {
        const char *sha = str(t, "sha256"), *ext = str(t, "ext");
        if (strlen(sha) != 64 || (strcmp(ext, ".ogg") && strcmp(ext, ".mp3") && strcmp(ext, ".flac"))) continue;
        char fn[80];
        snprintf(fn, sizeof fn, "%s%s", sha, ext);
        const pc_zip_entry_t *e = pc_zip_find(ents, nents, fn);
        if (!e) continue;
        pc_sound_t *d = &s[k++];
        d->entry = e;
        snprintf(d->sha, sizeof d->sha, "%s", sha);
        const char *zh = str(t, "zh_tw_name");
        snprintf(d->name, sizeof d->name, "%s", zh[0] ? zh : str(t, "en_name"));
        snprintf(d->en, sizeof d->en, "%s", str(t, "en_name"));
        const cJSON *c, *cen = cJSON_GetObjectItemCaseSensitive(t, "category_en");
        int ci = -1;
        cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(t, "category")) {
            ci++;
            const cJSON *en1 = cJSON_IsArray(cen) ? cJSON_GetArrayItem(cen, ci) : NULL;
            if (!cJSON_IsString(c) || !c->valuestring[0]) continue;
            int j = 0;
            while (j < cats->n && strcmp(cats->name[j], c->valuestring)) j++;
            if (j == cats->n) {
                if (j >= PC_SOUND_MAX_CATS) continue;
                snprintf(cats->name[j], sizeof cats->name[j], "%s", c->valuestring);
                if (en1 && cJSON_IsString(en1) && en1->valuestring) snprintf(cats->en[j], sizeof cats->en[j], "%s", en1->valuestring);
                cats->n++;
            }
            d->cats |= 1u << j;
        }
    }
    cJSON_Delete(root);
    qsort(s, (size_t)k, sizeof *s, sound_cmp);
    *out = s;
    *n = k;
    return 0;
}

const pc_sound_t *pc_sound_find(const pc_sound_t *s, int n, const char *sha)
{
    pc_sound_t key;
    snprintf(key.sha, sizeof key.sha, "%s", sha);
    return s && n ? bsearch(&key, s, (size_t)n, sizeof *s, sound_cmp) : NULL;
}

const char *pc_pick_name(const char *tw, const char *en)
{
    if (en && en[0] && pc_i18n_cur() == PC_LANG_EN) return en;
    return pc_tz(tw);
}
