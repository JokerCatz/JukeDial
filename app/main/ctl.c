// SPDX-License-Identifier: LGPL-2.1-or-later
#include "ctl.h"
#include "pc/pc_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "pc/pc_kv.h"
#include "pc/pc_radio.h"
#include "pc/pc_settings.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_geom.h"
#include "library_svc.h"
#include "bt_svc.h"
#include "cfg.h"
#include "vol_out.h"
#include "mp3_ctl.h"
#include "clock_svc.h"
#include "fx_state.h"
#include "lists_ctl.h"
#include "fonts_ctl.h"
#include "player_ctl.h"
#include "radio_svc.h"
#include "thumb_svc.h"
#include "sysmode.h"
#include "wifi_svc.h"
#include "ui/font.h"
#include "ui/ui.h"

static void reply(cJSON *r)
{
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    if (!s) return;
    printf("\nCTL %s\n", s);                /* one call: the probe task and others must not cut into the line; the leading newline puts the reply on a fresh line after any cut-off log fragment (the internal notes, Task 6b) */
    fflush(stdout);
    free(s);
}
static cJSON *fail(cJSON *r, const char *why)
{
    cJSON_DeleteItemFromObject(r, "ok");            /* one "ok" per reply: the helper that answers ok:true first leaves none behind */
    cJSON_AddFalseToObject(r, "ok");
    cJSON_DeleteItemFromObject(r, "error"); cJSON_AddStringToObject(r, "error", why);
    return r;
}
static const char *str(const cJSON *q, const char *k) { const cJSON *v = cJSON_GetObjectItem(q, k); return cJSON_IsString(v) ? v->valuestring : NULL; }

/* which build this is (2026-11-22: the vendor-codec image is the default): the A2DP codecs the firmware can negotiate, best first */
const char *ctl_codecs(void)
{
#if JC_LDAC
    return "ldac,aptxhd,aptx,aac,sbc";
#else
    return "aac,sbc";
#endif
}

static void status(cJSON *r)
{
    const now_ops_t *o = player_ctl_now();
    char t[256] = "";
    if (o && o->playing && o->playing()) o->title(t, sizeof t);
    cJSON_AddStringToObject(r, "playing", t);
    cJSON_AddNumberToObject(r, "radio", radio_current());
    cJSON_AddStringToObject(r, "board", BSP_BOARD_ID);           /* the web flasher picks its manifest by it */
    cJSON_AddNumberToObject(r, "lang", (double)i18n_lang());       /* the UI language: the web page orders its region list by it */
    {
        char fxs[16];
        snprintf(fxs, sizeof fxs, "%d on", fx_on_count());
        cJSON_AddStringToObject(r, "fx", fxs);
    }
    cJSON_AddNumberToObject(r, "bt_mode", bt_svc_mode());
    cJSON_AddStringToObject(r, "bt", bt_svc_mode_label(bt_svc_mode()));
    cJSON_AddStringToObject(r, "bt_codec", bt_svc_codec());
    cJSON_AddStringToObject(r, "codecs", ctl_codecs());       /* the build: codecs=ldac,aptxhd,aptx,aac,sbc (default image) or aac,sbc (make build-stock) */
    cJSON_AddBoolToObject(r, "wifi", wifi_svc_connected());
    cJSON_AddStringToObject(r, "ip", wifi_svc_ip());
    cJSON_AddNumberToObject(r, "rssi", wifi_svc_rssi());
    cJSON_AddNumberToObject(r, "volume", app_volume());
    cJSON_AddNumberToObject(r, "internal_free", (double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(r, "internal_min", (double)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(r, "psram_free", (double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

/* web font builder (spec 2026-11-15-web-tool.md section 6): the characters the library needs, sorted by code point, a chunk of up to FC_CHUNK per call:
 * {"op":"font_charset","off":N} -> {"total":T,"off":N,"chars":"..."} (UTF-8, the next characters from index N). The set is the library index's string pool
 * (paths, titles, artists, albums, folder names) read under the display lock (the library is the LVGL task's); a 0x110000-bit map in PSRAM is built per call
 * (no state kept: 136 KB for a few ms, freed before the reply). Refused while a scan runs. */
#define FC_CHUNK 256
static cJSON *font_charset_op(cJSON *r, const cJSON *q)
{
    const cJSON *o = cJSON_GetObjectItem(q, "off");
    long off = cJSON_IsNumber(o) ? (long)o->valuedouble : 0;
    if (off < 0) off = 0;
    if (library_scanning()) return fail(r, "scanning");
    uint8_t *bm = heap_caps_calloc(1, 0x110000 / 8, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!bm) return fail(r, "no memory");
    bsp_display_lock(-1);
    const uint8_t *p = (const uint8_t *)g_lib.pool;
    for (uint32_t i = 0, n = p ? g_lib.pool_len : 0; i < n;) {
        uint32_t c = p[i], len = 1;
        if (c >= 0xf0) { len = 4; c &= 7; } else if (c >= 0xe0) { len = 3; c &= 15; } else if (c >= 0xc0) { len = 2; c &= 31; } else if (c >= 0x80) { i++; continue; }
        if (i + len > n) break;
        bool ok = true;
        for (uint32_t k = 1; k < len; k++) { if ((p[i + k] & 0xc0) != 0x80) { ok = false; break; } c = (c << 6) | (p[i + k] & 0x3f); }
        i += ok ? len : 1;
        if (ok && c > 0x20 && c != 0x7f && c < 0x110000 && !(c >= 0x80 && c < 0xa0)) bm[c >> 3] |= (uint8_t)(1u << (c & 7));
    }
    bsp_display_unlock();
    long total = 0;
    for (uint32_t i = 0; i < 0x110000 / 8; i++) total += __builtin_popcount(bm[i]);
    char buf[FC_CHUNK * 4 + 1]; size_t bl = 0; long idx = 0;
    for (uint32_t c = 0x21; c < 0x110000 && idx < off + FC_CHUNK; c++) {
        if (!(bm[c >> 3] & (1u << (c & 7)))) continue;
        if (idx++ < off) continue;
        if (c < 0x80) buf[bl++] = (char)c;
        else if (c < 0x800) { buf[bl++] = (char)(0xc0 | (c >> 6)); buf[bl++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { buf[bl++] = (char)(0xe0 | (c >> 12)); buf[bl++] = (char)(0x80 | ((c >> 6) & 63)); buf[bl++] = (char)(0x80 | (c & 63)); }
        else { buf[bl++] = (char)(0xf0 | (c >> 18)); buf[bl++] = (char)(0x80 | ((c >> 12) & 63)); buf[bl++] = (char)(0x80 | ((c >> 6) & 63)); buf[bl++] = (char)(0x80 | (c & 63)); }
    }
    buf[bl] = 0;
    free(bm);
    cJSON_AddNumberToObject(r, "total", (double)total);
    cJSON_AddNumberToObject(r, "off", (double)off);
    cJSON_AddStringToObject(r, "chars", buf);
    return r;
}

/* {"op":"font_miss"} -> {"total":N,"more":M,"list":[code points]}: what `fontmiss` prints (glyphs no font had) */
static cJSON *font_miss_op(cJSON *r)
{
    uint32_t cp[16], total = 0, more = 0;
    int n = font_miss_get(cp, 16, &total, &more);
    cJSON_AddNumberToObject(r, "total", (double)total);
    cJSON_AddNumberToObject(r, "more", (double)more);
    cJSON *a = cJSON_AddArrayToObject(r, "list");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(a, cJSON_CreateNumber((double)cp[i]));
    return r;
}

static cJSON *stations_get(cJSON *r)
{
    int n = radio_count() ? radio_count() : radio_load();
    cJSON *a = cJSON_AddArrayToObject(r, "stations");
    for (int i = 0; i < n; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "name", radio_station(i)->name);
        cJSON_AddStringToObject(s, "url", radio_station(i)->url);
        { int u = pc_station_pick(radio_station(i), true);            /* which address plays: http first; "https" = TLS (an https-only listing is tried as http first, then https) */
          cJSON_AddNumberToObject(s, "use", u);
          cJSON_AddStringToObject(s, "scheme", u < 0 ? "" : pc_url_is_https(pc_station_url(radio_station(i), u)) ? "https" : "http"); }
        if (pc_station_urls(radio_station(i)) > 1) {                 /* backup addresses: name|url|url2|url3 */
            cJSON *al = cJSON_AddArrayToObject(s, "alts");
            for (int k = 1; k < pc_station_urls(radio_station(i)); k++) cJSON_AddItemToArray(al, cJSON_CreateString(pc_station_url(radio_station(i), k)));
        }
        cJSON_AddItemToArray(a, s);
    }
    static char stems[16][PC_RADIO_STEM_MAX];
    int nl = radio_lists(stems, 16);
    cJSON *ls = cJSON_AddArrayToObject(r, "lists");                  /* the list files on the card ("radio" = the main one) */
    for (int i = 0; i < nl; i++) cJSON_AddItemToArray(ls, cJSON_CreateString(stems[i][0] ? stems[i] : "radio"));
    cJSON_AddStringToObject(r, "list", radio_list_stem()[0] ? radio_list_stem() : "radio");
    return r;
}

static int write_file(const char *path, const char *text)   /* .tmp, then replace */
{
    char tmp[96];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    size_t n = strlen(text);
    bool ok = fwrite(text, 1, n, f) == n;
    ok = !fclose(f) && ok;
    if (!ok) { remove(tmp); return -1; }
    remove(path);                                       /* FAT rename does not replace */
    return rename(tmp, path);
}

static cJSON *stations_put(cJSON *r, const cJSON *q)
{
    const cJSON *a = cJSON_GetObjectItem(q, "stations");
    if (!cJSON_IsArray(a)) return fail(r, "stations?");
    int n = cJSON_GetArraySize(a);
    if (n <= 0 || n > RADIO_MAX) return fail(r, "1..64 stations");   /* never wipe the list */
    pc_station_t *s = heap_caps_calloc((size_t)n, sizeof *s, MALLOC_CAP_SPIRAM);
    char *text = heap_caps_malloc((size_t)n * 880 + 64, MALLOC_CAP_SPIRAM);
    int k = 0;
    for (int i = 0; s && text && i < n; i++) {
        const cJSON *e = cJSON_GetArrayItem(a, i);
        const char *name = str(e, "name"), *url = str(e, "url");
        if (!url || !url[0] || strchr(url, '|') || strpbrk(url, "\r\n")) continue;
        if (strlen(url) >= sizeof s[k].url) { free(s); free(text); return fail(r, "an address is too long (255 max)"); }
        snprintf(s[k].url, sizeof s[k].url, "%s", url);
        const cJSON *alts = cJSON_GetObjectItem(e, "alts");          /* backup addresses (max 2), empty ones skipped */
        for (int j = 0, u = 0; cJSON_IsArray(alts) && j < cJSON_GetArraySize(alts) && u < PC_STATION_ALTS; j++) {
            const cJSON *av = cJSON_GetArrayItem(alts, j);
            if (!cJSON_IsString(av) || !av->valuestring[0] || strchr(av->valuestring, '|') || strpbrk(av->valuestring, "\r\n")) continue;
            if (strlen(av->valuestring) >= sizeof s[k].alt[u]) { free(s); free(text); return fail(r, "an address is too long (255 max)"); }
            snprintf(s[k].alt[u++], sizeof s[k].alt[0], "%s", av->valuestring);
        }
        snprintf(s[k].name, sizeof s[k].name, "%s", name && name[0] ? name : url);
        for (char *c = s[k].name; *c; c++) if (*c == '\r' || *c == '\n') *c = ' ';
        k++;
    }
    int len = k && text ? pc_radio_format(s, k, text, (size_t)n * 880 + 64) : -1;
    char path[64];
    const char *want = str(q, "list");                               /* the list the page edited ("radio" = the main one); the one in use when absent */
    int rc = -1;
    if (len > 0) {
        if (want && pc_radio_list_path(!strcmp(want, "radio") ? "" : want, path, sizeof path) < 0) rc = -2;
        else {
            if (!want) radio_list_path(path, sizeof path);
            rc = write_file(path, text);
        }
    }
    free(s);
    free(text);
    if (rc == -2) return fail(r, "list?");
    if (rc) return fail(r, k ? "cannot write the TF card" : "no valid station");
    cJSON_AddNumberToObject(r, "stations", rc == 0 && want && strcmp(want, radio_list_stem()[0] ? radio_list_stem() : "radio") ? k : radio_load());   /* the count of the list just written */
    return r;
}

static cJSON *fx_list_op(cJSON *r)
{
    static char names[32][48];
    int n = fx_list(names, 32);
    cJSON *a = cJSON_AddArrayToObject(r, "presets");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(a, cJSON_CreateString(names[i]));
    cJSON_AddStringToObject(r, "state_from", fx_state_from());
    return r;
}

/* one app as JSON; NULL when any allocation failed (the half-built object is freed here) */
static cJSON *fx_app_json(int i)
{
    const pc_fxapp_t *ap = fx_app(i);
    const char *why = NULL;
    fx_look_t lk = fx_look(i, &why);
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    bool own_mix = false;
    for (int p = 0; p < ap->nparam; p++) own_mix |= !strcmp(ap->param[p].id, "mix");
    bool ok = cJSON_AddStringToObject(o, "id", ap->id) && cJSON_AddStringToObject(o, "zh", ap->zh) && cJSON_AddStringToObject(o, "en", ap->en)
        && cJSON_AddStringToObject(o, "desc_zh", ap->desc_zh ? ap->desc_zh : "") && cJSON_AddStringToObject(o, "desc_en", ap->desc_en ? ap->desc_en : "")
        && cJSON_AddStringToObject(o, "cls", ap->cls == PC_FXC_HEAVY ? "heavy" : "light")
        && cJSON_AddStringToObject(o, "look", lk == FX_OFF ? "off" : lk == FX_RUN ? "run" : "dim") && cJSON_AddStringToObject(o, "why", why ? why : "")
        && cJSON_AddBoolToObject(o, "on", fx_on(i));
    if (ok && ((ap->flags & PC_FXA_MIX) || own_mix)) ok = cJSON_AddNumberToObject(o, "mix", fx_get(i, FX_P_MIX)) != NULL;
    cJSON *pa = ok ? cJSON_AddArrayToObject(o, "params") : NULL;
    for (int p = 0; pa && p < ap->nparam; p++) {
        const pc_fxparam_t *pp = &ap->param[p];
        cJSON *q = cJSON_CreateObject();
        if (!q) { pa = NULL; break; }
        cJSON_AddItemToArray(pa, q);                /* owned by the array from here: a later failure frees it with o */
        ok = cJSON_AddStringToObject(q, "id", pp->id) && cJSON_AddStringToObject(q, "zh", pp->zh) && cJSON_AddStringToObject(q, "en", pp->en)
            && cJSON_AddNumberToObject(q, "lo", pp->lo) && cJSON_AddNumberToObject(q, "hi", pp->hi) && cJSON_AddNumberToObject(q, "def", pp->def)
            && cJSON_AddNumberToObject(q, "step", pp->step) && cJSON_AddNumberToObject(q, "unit", pp->unit) && cJSON_AddNumberToObject(q, "flags", pp->flags);
        if (ok && pp->unit == PC_FXU_CHOICE && pp->ch_zh && pp->ch_en) {
            int nc = (int)(pp->hi - pp->lo) + 1;
            cJSON *cz = cJSON_AddArrayToObject(q, "choices_zh"), *ce = cJSON_AddArrayToObject(q, "choices_en");
            ok = cz && ce;
            for (int c = 0; ok && c < nc; c++) {
                cJSON *z = cJSON_CreateString(pp->ch_zh[c]), *e = cJSON_CreateString(pp->ch_en[c]);
                if (z) cJSON_AddItemToArray(cz, z);
                if (e) cJSON_AddItemToArray(ce, e);
                ok = z && e;
            }
        }
        if (ok) ok = cJSON_AddNumberToObject(q, "v", fx_get(i, p)) != NULL;
        if (!ok) { pa = NULL; break; }
    }
    if (!pa) { cJSON_Delete(o); return NULL; }
    return o;
}

/* every fx app with its switch, look and values, in the chain's fixed order; built per request and freed with the reply (PSRAM hooks) */
static cJSON *fx_apps_op(cJSON *r)
{
    cJSON *arr = cJSON_AddArrayToObject(r, "apps");
    if (!arr) return fail(r, "out of memory");
    for (int i = 0; i < fx_count(); i++) {
        cJSON *o = fx_app_json(i);
        if (!o) { cJSON_DeleteItemFromObject(r, "apps"); return fail(r, "out of memory"); }
        cJSON_AddItemToArray(arr, o);
    }
    return r;
}

static cJSON *config_get(cJSON *r)
{
    int n;
    const pc_setting_t *s = pc_settings(&n);
    cJSON *c = cJSON_AddObjectToObject(r, "config");
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_AddObjectToObject(c, s[i].key);
        cJSON_AddNumberToObject(o, "value", cfg_int(s[i].key, s[i].def));
        cJSON_AddNumberToObject(o, "min", s[i].min);
        cJSON_AddNumberToObject(o, "max", s[i].max);
        cJSON_AddBoolToObject(o, "reboot", s[i].reboot);
    }
    return r;
}

static cJSON *config_set(cJSON *r, const cJSON *q)
{
    const cJSON *vals = cJSON_GetObjectItem(q, "values");
    if (!cJSON_IsObject(vals)) return fail(r, "values?");
    int n, changed = 0, bad = 0;
    bool reboot = false;
    const pc_setting_t *s = pc_settings(&n);
    const cJSON *it;
    cJSON_ArrayForEach(it, vals) {
        char b[16];
        int v;
        snprintf(b, sizeof b, "%d", cJSON_IsNumber(it) ? it->valueint : -999999);
        if (pc_setting_check(it->string, b, &v)) { bad++; continue; }
        if (cfg_int(it->string, -99999) == v) continue;
        if (!strcmp(it->string, "lang")) i18n_set_lang(pc_lang_norm(v)); else cfg_set_int(it->string, v);   /* i18n A: the language byte, the stored key and every screen again */
        changed++;
        for (int i = 0; i < n; i++) if (!strcmp(s[i].key, it->string) && s[i].reboot) reboot = true;
        if (!strcmp(it->string, "brightness")) ui_backlight_apply();   /* the live value follows cfg, except under the torch */
        if (!strcmp(it->string, "volume_max") && vol_out_limit(v) && ui_volume_hook) ui_volume_hook(app_volume());   /* the limit applies now (wired gain, headset); BTVOL: every output's stored value comes under it */
        if (!strncmp(it->string, "rg", 2)) mp3_ctl_rg_changed();
        if (!strcmp(it->string, "ldac_kbps")) bt_svc_ldac_kbps(v);   /* the LDAC top rate applies now (-1: a build without LDAC; the stored value is read at boot) */
        if (!strcmp(it->string, "cover_auto")) thumb_svc_set_auto(v);   /* hires T8e: the background pass follows at once */
    }
    cJSON_AddNumberToObject(r, "changed", changed);
    cJSON_AddNumberToObject(r, "rejected", bad);
    cJSON_AddBoolToObject(r, "reboot", reboot);
    return r;
}

static cJSON *wifi_set(cJSON *r, const cJSON *q)
{
    const char *ssid = str(q, "ssid"), *pass = str(q, "pass");
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || !pass || strlen(pass) > 64 || strpbrk(ssid, "\r\n") || strpbrk(pass, "\r\n"))
        return fail(r, "ssid 1..32, password 0..64");
    if (isspace((unsigned char)ssid[0]) || isspace((unsigned char)ssid[strlen(ssid) - 1]) || (pass[0] && (isspace((unsigned char)pass[0]) || isspace((unsigned char)pass[strlen(pass) - 1]))))
        return fail(r, "spaces at the start or end cannot be stored");   /* wifi.txt values are trimmed when read */
    pc_kv_t kv[2];
    int n = 0;
    snprintf(kv[n].key, sizeof kv[n].key, "ssid"); snprintf(kv[n].val, sizeof kv[n].val, "%s", ssid); n++;
    snprintf(kv[n].key, sizeof kv[n].key, "pass"); snprintf(kv[n].val, sizeof kv[n].val, "%s", pass); n++;
    int rc = pc_kv_save(PC_CFG_WIFI, kv, n);
    memset(kv, 0, sizeof kv);
    printf("APP ctl wifi saved rc=%d\n", rc);            /* never the values */
    if (rc) return fail(r, "cannot write the TF card");
    wifi_svc_reconnect();   /* reconnect with the new network (only when it is on) */
    return r;
}

static void probe_done(int id, bool ok, const char *fmt, const char *url, const char *why)   /* probe task */
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "id", id);
    cJSON_AddTrueToObject(r, "ok");
    cJSON_AddBoolToObject(r, "playable", ok);
    cJSON_AddStringToObject(r, "fmt", fmt);
    cJSON_AddStringToObject(r, "url", url);
    cJSON_AddStringToObject(r, "why", why);
    reply(r);
}

void ctl_run(void *arg)
{
    char *line = arg;
    cJSON *q = cJSON_Parse(line);
    free(line);
    cJSON *r = cJSON_CreateObject();
    const cJSON *id = cJSON_GetObjectItem(q, "id");
    if (cJSON_IsNumber(id)) cJSON_AddNumberToObject(r, "id", id->valuedouble);
    const char *op = q ? str(q, "op") : NULL;
    if (!op) { reply(fail(r, "bad request")); cJSON_Delete(q); return; }
    cJSON_AddTrueToObject(r, "ok");
    const cJSON *iv = cJSON_GetObjectItem(q, "i");
    if (!strcmp(op, "status")) status(r);
    else if (!strcmp(op, "stations_get")) stations_get(r);
    else if (!strcmp(op, "stations_put")) r = stations_put(r, q);
    else if (!strcmp(op, "radio_list")) {             /* {"list":"cn"} use that list ("radio" = the main one); {"list":"x","create":true} make an empty list first */
        const char *l = str(q, "list");
        const char *stem = l && !strcmp(l, "radio") ? "" : l;
        char path[64];
        if (!stem || pc_radio_list_path(stem, path, sizeof path) < 0) r = fail(r, "list?");
        else {
            if (cJSON_IsTrue(cJSON_GetObjectItem(q, "create")) && stem[0]) {
                FILE *f = fopen(path, "ab");
                if (f) fclose(f);
            }
            if (radio_list_select(stem) < 0) r = fail(r, "no such list");
            else stations_get(r);
        }
    }
    else if (!strcmp(op, "radio")) {
        int i = cJSON_IsNumber(iv) ? iv->valueint : -1;
        if (!radio_count()) radio_load();           /* right after boot nothing has read the list yet */
        int rc = i < 0 ? (radio_stop(), 0) : radio_play(i);
        if (rc) r = fail(r, "cannot play (broadcast mode or not http)");
    } else if (!strcmp(op, "probe")) {              /* answered later, from the probe task */
        const char *u = str(q, "url");
        int rc = u && cJSON_IsNumber(id) ? radio_probe(u, id->valueint, probe_done) : -1;
        if (!rc) { cJSON_Delete(r); cJSON_Delete(q); return; }
        r = fail(r, rc == -2 ? "busy: another check is running" : "cannot check now (broadcast mode?)");
    } else if (!strcmp(op, "clock_get")) {
        pc_alarm_t *a = clock_alarm();
        cJSON *al = cJSON_AddObjectToObject(r, "alarm");
        cJSON_AddBoolToObject(al, "on", a->on);
        cJSON_AddNumberToObject(al, "min", a->min);
        cJSON_AddNumberToObject(al, "days", a->days);
        cJSON_AddStringToObject(al, "sound", clock_alarm_sound());
        cJSON_AddStringToObject(r, "tz", clock_tz_str());              /* the POSIX TZ string in force */
        cJSON_AddNumberToObject(r, "tz_min", clock_tz_min());
        cJSON_AddBoolToObject(r, "sun", clock_sun_has());               /* a sunrise location is set; lat / lon in 0.01 degrees */
        if (clock_sun_has()) { cJSON_AddNumberToObject(r, "sun_lat", clock_sun_lat_e4() / 100); cJSON_AddNumberToObject(r, "sun_lon", clock_sun_lon_e4() / 100); }
        cJSON_AddBoolToObject(r, "synced", clock_time_valid());
    } else if (!strcmp(op, "clock_set")) {             /* any of: alarm_on, alarm_min, alarm_days, tz (POSIX TZ string), sun_lat + sun_lon (0.01 degrees), sun_clear, sound ("" = built-in) */
        pc_alarm_t *a = clock_alarm();
        const cJSON *v;
        const cJSON *la = cJSON_GetObjectItem(q, "sun_lat"), *lo = cJSON_GetObjectItem(q, "sun_lon"), *tzv = cJSON_GetObjectItem(q, "tz");
        bool loc = cJSON_IsNumber(la) && cJSON_IsNumber(lo);
        if (tzv && (!cJSON_IsString(tzv) || !clock_tz_valid(tzv->valuestring))) r = fail(r, "not a valid POSIX TZ string");           /* everything is checked first: a bad value saves nothing */
        else if (loc && (la->valuedouble < -9000 || la->valuedouble > 9000 || lo->valuedouble < -18000 || lo->valuedouble > 18000)) r = fail(r, "latitude / longitude out of range");
        else {
            if (cJSON_IsBool(v = cJSON_GetObjectItem(q, "alarm_on"))) a->on = cJSON_IsTrue(v);
            if (cJSON_IsNumber(v = cJSON_GetObjectItem(q, "alarm_min")) && v->valueint >= 0 && v->valueint < 1440) a->min = v->valueint;
            if (cJSON_IsNumber(v = cJSON_GetObjectItem(q, "alarm_days")) && v->valueint >= 0 && v->valueint <= 0x7F) a->days = (uint8_t)v->valueint;
            clock_alarm_saved();
            if (tzv) clock_tz_set(tzv->valuestring);
            if (cJSON_IsTrue(cJSON_GetObjectItem(q, "sun_clear"))) clock_sun_clear();
            else if (loc) clock_sun_set((int)la->valuedouble, (int)lo->valuedouble);
            if (cJSON_IsString(v = cJSON_GetObjectItem(q, "sound")) && !v->valuestring[0]) clock_alarm_set_sound("");
            if (a->on && !clock_time_valid()) clock_sync_now();
        }
    } else if (!strcmp(op, "stop")) {
        const now_ops_t *o = player_ctl_now();
        if (o && o->stop) o->stop();
    } else if (!strcmp(op, "fx_list")) fx_list_op(r);
    else if (!strcmp(op, "fx_file")) {
        const char *name = str(q, "name");
        char *t = name ? fx_read(name) : NULL;
        if (t) { cJSON_AddStringToObject(r, "text", t); heap_caps_free(t); } else r = fail(r, "no such preset");
    } else if (!strcmp(op, "fx_live")) {
        const char *t = str(q, "text");
        const char *un[8];
        if (!t || fx_import_text(t, false, NULL, 0) < 0) r = fail(r, "the TOML does not load (format = 1?)");
        else {                                      /* the pointers die with the next import: copied into the reply now */
            int nu = fx_unsupported(un, 8);
            cJSON *ua = cJSON_AddArrayToObject(r, "unsupported");
            for (int i = 0; ua && i < nu && i < 8; i++) {
                cJSON *u = cJSON_CreateString(un[i]);
                if (!u) { ua = NULL; break; }
                cJSON_AddItemToArray(ua, u);
            }
            if (!ua) r = fail(r, "out of memory");
        }
    } else if (!strcmp(op, "fx_apps")) r = fx_apps_op(r);
    else if (!strcmp(op, "fx_export")) {
        char *t = fx_export_text();
        if (t) { cJSON_AddStringToObject(r, "text", t); heap_caps_free(t); } else r = fail(r, "cannot export");
    } else if (!strcmp(op, "fx_save")) {
        const char *t = str(q, "text"), *name = str(q, "name");
        if (!t || !name || fx_save(name, t)) r = fail(r, "cannot save (bad TOML or name)");
    } else if (!strcmp(op, "fx_select")) {
        const char *name = str(q, "name");
        if (!name || fx_select(name)) r = fail(r, "cannot select");
    } else if (!strcmp(op, "config_get")) config_get(r);
    else if (!strcmp(op, "config_set")) r = config_set(r, q);
    else if (!strcmp(op, "volume")) {
        if (cJSON_IsNumber(iv)) { app_set_volume(iv->valueint); if (ui_volume_hook) ui_volume_hook(app_volume()); }
        cJSON_AddNumberToObject(r, "volume", app_volume());
    } else if (!strcmp(op, "wifi_set")) r = wifi_set(r, q);
    else if (!strcmp(op, "reboot")) { reply(r); cJSON_Delete(q); sysmode_reboot_msg(T(S_RESTARTING) /* 重新開機… */); return; }
    else if (!strcmp(op, "font_charset")) r = font_charset_op(r, q);
    else if (!strcmp(op, "font_miss")) r = font_miss_op(r);
    else if (!strncmp(op, "font_put", 8)) r = fonts_ctl(op, r, q);   /* font upload to the card (fonts_ctl.c) */
    else if (!strncmp(op, "list_", 5)) r = lists_ctl(op, r, q);   /* glyph-pack lists (lists_ctl.c) */
    else r = fail(r, "unknown op");
    reply(r);
    cJSON_Delete(q);
}
