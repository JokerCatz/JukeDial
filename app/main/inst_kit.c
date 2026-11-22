// SPDX-License-Identifier: LGPL-2.1-or-later
#include "inst_kit.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inst_engine.h"

static inst_kit_t *s_kit;                 /* the one the engine has (owned here) */
static volatile inst_kit_state_t s_state;
static volatile int s_err, s_skipped;
static volatile size_t s_bytes;
static volatile bool s_busy;
static char s_want[INST_KIT_NAME], s_doing[INST_KIT_NAME];

static int cmp(const void *a, const void *b) { return strcmp(a, b); }

int inst_kit_list(char (*names)[INST_KIT_NAME], int max)
{
    int n = 0;
    DIR *d = opendir(INST_KIT_DIR);
    if (!d) return 0;
    struct dirent *e;
    char path[192];
    while ((e = readdir(d)) && n < max) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= INST_KIT_NAME - 8) continue;
        snprintf(path, sizeof path, INST_KIT_DIR "/%s", e->d_name);
        DIR *k = opendir(path);
        if (!k) continue;
        struct dirent *f;
        while ((f = readdir(k)) && n < max) {
            size_t l = strlen(f->d_name);
            if (!strcasecmp(f->d_name, "kit.txt")) snprintf(names[n++], INST_KIT_NAME, "%s", e->d_name);
            else if (l > 4 && !strcasecmp(f->d_name + l - 4, ".sfz") && strlen(e->d_name) + l < INST_KIT_NAME)
                snprintf(names[n++], INST_KIT_NAME, "%s/%.*s", e->d_name, (int)(l - 4), f->d_name);
        }
        closedir(k);
    }
    closedir(d);
    qsort(names, (size_t)n, INST_KIT_NAME, cmp);
    return n;
}

const inst_kit_t *inst_kit_current(void) { return s_kit; }

inst_kit_state_t inst_kit_state(int *err, size_t *bytes, int *skipped)
{
    if (err) *err = s_err;
    if (bytes) *bytes = s_bytes;
    if (skipped) *skipped = s_skipped;
    return s_busy ? INST_KIT_LOADING : s_state;
}

static void kit_free(inst_kit_t *k)
{
    if (!k) return;
    for (int i = 0; i < k->nsmp; i++) heap_caps_free((void *)k->smp[i].data);
    heap_caps_free(k->def);
    heap_caps_free(k);
}

static char *read_text(const char *path, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM);
    size_t n = b ? fread(b, 1, cap, f) : 0;
    fclose(f);
    if (b) b[n] = 0;
    return b;
}

/* one WAV into PSRAM; chunked through a small internal buffer (FATFS -> SDMMC wants DMA-able memory) */
static int load_wav(const char *path, pc_sample_t *s, uint8_t *bounce, size_t bounce_n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -INST_KIT_E_NOTFOUND;
    size_t got = fread(bounce, 1, bounce_n, f);
    pc_wav_t w;
    int r = pc_wav_parse(bounce, (uint32_t)got, &w);
    if (r != PC_WAV_OK) { fclose(f); return -INST_KIT_E_FORMAT; }
    struct stat st;                              /* streamed (size 0xFFFFFFFF) or cut files: what is there */
    if (stat(path, &st) || pc_wav_fit(&w, (uint64_t)st.st_size) != PC_WAV_OK) { fclose(f); return -INST_KIT_E_FORMAT; }
    int16_t *d = heap_caps_malloc(w.data_len, MALLOC_CAP_SPIRAM);
    if (!d) { fclose(f); return -INST_KIT_E_MEMORY; }
    fseek(f, (long)w.data_off, SEEK_SET);
    size_t done = 0;
    while (done < w.data_len) {
        size_t want = w.data_len - done < bounce_n ? w.data_len - done : bounce_n;
        size_t n = fread(bounce, 1, want, f);
        if (!n) break;
        memcpy((uint8_t *)d + done, bounce, n);
        done += n;
    }
    fclose(f);
    s->data = d;
    s->frames = (uint32_t)(done / (w.channels * 2u));
    s->rate = w.rate;
    s->channels = (uint8_t)w.channels;
    return (int)done;
}

static int load(const char *name, inst_kit_t **out)
{
    *out = NULL;
    char dir[160], path[256];
    const char *slash = strchr(name, '/');
    bool sfz = slash != NULL;
    snprintf(dir, sizeof dir, INST_KIT_DIR "/%.*s", sfz ? (int)(slash - name) : (int)strlen(name), name);
    if (sfz) snprintf(path, sizeof path, "%s/%s.sfz", dir, slash + 1);
    else snprintf(path, sizeof path, "%s/kit.txt", dir);
    char *text = read_text(path, 64 * 1024);
    if (!text) return -INST_KIT_E_NOTFOUND;
    inst_kit_t *k = heap_caps_calloc(1, sizeof *k, MALLOC_CAP_SPIRAM);
    if (k) k->def = heap_caps_calloc(1, sizeof *k->def, MALLOC_CAP_SPIRAM);
    if (!k || !k->def) { heap_caps_free(text); kit_free(k); return -INST_KIT_E_MEMORY; }
    int n = sfz ? pc_kit_parse_sfz(text, k->def) : pc_kit_parse_txt(text, k->def);
    heap_caps_free(text);
    if (n <= 0) { kit_free(k); return -INST_KIT_E_EMPTY; }
    snprintf(k->name, sizeof k->name, "%s", name);
    /* distinct files, their sizes first: all or nothing under the cap */
    static int16_t first[PC_KIT_REGIONS];
    size_t total = 0;
    for (int i = 0; i < n; i++) {
        k->smp_of[i] = -1;
        first[i] = (int16_t)i;
        for (int j = 0; j < i; j++) if (!strcmp(k->def->r[j].file, k->def->r[i].file)) { first[i] = first[j]; break; }
        if (first[i] != i) continue;
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir, k->def->r[i].file);
        if (!stat(path, &st)) {                  /* a size of 2 GiB or more arrives negative (32-bit off_t): over the cap, and the sum must not wrap */
            size_t sz = (size_t)st.st_size;
            total += sz > INST_KIT_CAP ? (size_t)INST_KIT_CAP + 1 : sz;
        }
    }
    if (total > INST_KIT_CAP) { s_bytes = total; kit_free(k); return -INST_KIT_E_TOOBIG; }
    uint8_t *bounce = heap_caps_malloc(8192, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!bounce) { kit_free(k); return -INST_KIT_E_MEMORY; }
    int skipped = 0, err = 0;
    size_t bytes = 0;
    for (int i = 0; i < n; i++) {
        if (first[i] != i) { k->smp_of[i] = k->smp_of[first[i]]; continue; }
        snprintf(path, sizeof path, "%s/%s", dir, k->def->r[i].file);
        int r = load_wav(path, &k->smp[k->nsmp], bounce, 8192);
        if (r < 0) {
            skipped++;
            err = -r;
            printf("INST kit skip %s (%d)\n", k->def->r[i].file, -r);   /* one bad file: the rest still loads */
            continue;
        }
        bytes += (size_t)r;
        k->smp_of[i] = (int16_t)k->nsmp++;
    }
    heap_caps_free(bounce);
    k->bytes = bytes;
    s_skipped = skipped;
    if (!k->nsmp) { kit_free(k); return -(err ? err : INST_KIT_E_EMPTY); }
    *out = k;
    return 0;
}

/* what the UI shows, copied under a lock: the kit itself can be freed by this task at any time */
static inst_kit_info_t s_info;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static void publish(const inst_kit_t *k)
{
    inst_kit_info_t i = { 0 };
    i.gen = s_info.gen + 1;
    for (int p = 0; p < 8; p++) i.note[p] = -1;
    if (k) {
        snprintf(i.name, sizeof i.name, "%s", k->name);
        for (int p = 0; p < 8; p++) {
            int n = k->def->pad[p], r = n >= 0 ? pc_kit_find(k->def, n) : -1;
            if (r < 0) continue;
            i.note[p] = (int16_t)n;
            const char *f = strrchr(k->def->r[r].file, '/');
            f = f ? f + 1 : k->def->r[r].file;
            const char *dot = strrchr(f, '.');
            snprintf(i.label[p], sizeof i.label[p], "%.*s", dot ? (int)(dot - f) : (int)strlen(f), f);
            for (char *c = i.label[p]; *c; c++) if (*c == '_' || *c == '-') *c = ' ';   /* the pad label wraps there */
        }
    }
    portENTER_CRITICAL(&s_lock);
    s_info = i;
    portEXIT_CRITICAL(&s_lock);
}
void inst_kit_info(inst_kit_info_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_info;
    portEXIT_CRITICAL(&s_lock);
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        snprintf(s_doing, sizeof s_doing, "%s", s_want);
        publish(NULL);
        inst_engine_set_kit(NULL);                 /* the engine lets go first; then the memory can go */
        kit_free(s_kit);
        s_kit = NULL;
        s_err = 0;
        s_skipped = 0;
        s_bytes = 0;
        if (!s_doing[0]) s_state = INST_KIT_NONE;
        else {
            int64_t t0 = esp_timer_get_time();
            inst_kit_t *k;
            int r = load(s_doing, &k);
            if (r < 0) {
                s_err = -r;
                s_state = INST_KIT_ERROR;
                printf("INST kit %s failed err=%d bytes=%u\n", s_doing, -r, (unsigned)s_bytes);
            } else {
                s_kit = k;
                s_bytes = k->bytes;
                inst_engine_set_kit(k);
                s_state = INST_KIT_READY;
                printf("INST kit %s regions=%d samples=%d bytes=%u skipped=%d ms=%lld psram_free=%u\n", k->name, k->def->n,
                       k->nsmp, (unsigned)k->bytes, s_skipped, (long long)(esp_timer_get_time() - t0) / 1000,
                       (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            }
        }
        publish(s_kit);
        if (strcmp(s_doing, s_want)) continue;     /* asked again meanwhile: go round once more */
        s_busy = false;
        if (!strcmp(s_doing, s_want)) break;
        s_busy = true;
    }
    vTaskDelete(NULL);
}

void inst_kit_load(const char *name)
{
    snprintf(s_want, sizeof s_want, "%s", name ? name : "");
    if (s_busy) return;                            /* the running loader picks the new name up */
    s_busy = true;
    if (xTaskCreatePinnedToCore(task, "inst_kit", 6144, NULL, 2, NULL, 0) != pdPASS) s_busy = false;
}
