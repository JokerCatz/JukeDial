// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes the SD layer under the card format (sd_format.h's lower half; the board's is app/main/sd_hw.c on bsp_sdcard_format).
 * Behaviour: the mount's fail word follows g_sd_ok ("nocard" when the simulator boots without a card, SIM_STATE sd=0) until `simsd` sets it;
 * the card in the slot is `simsd size` bytes (default a "32 GB" card, 31914983424) with CID serial 0x5D01 (`simsd swap`: another card of the same
 * size, serial 0x5D02). A format ends 1.5 s of virtual time later and first does what the board's bsp_sdcard_format does before any write, with
 * the same pure check: a card that mounts now (`simsd fixed`: the card was mended elsewhere, the player has not seen it) is left mounted
 * (ESP_ERR_INVALID_STATE), a reason other than gpt / nofs is ESP_ERR_NOT_FOUND, another card than the one shown (pc_fmt_same_card) is
 * ESP_ERR_INVALID_SIZE, all with stage BEFORE and nothing recorded. Otherwise the format is recorded (printed "SIM sd format fs= cluster= bu= align=
 * start= sectors= serial=", counted in "simsd") and, unless `simsd formatfail write|mkfs|mount` (1 = mkfs) fails it at that stage, the card is
 * mounted (g_sd_ok, the fail word gone) and sd_format_finish(0, MOUNT). Nothing on the simulator's root is erased (the root is the card's files;
 * sd_format.c makes its folders there). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "card_test.h"
#include "cfg.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "sd_format.h"
#include "sim.h"

static char s_fail[8];                   /* "" = not set: from g_sd_ok */
static uint64_t s_bytes = 31914983424ull;
static uint32_t s_serial = 0x5D01;
static int s_formats, s_formatfail = -1; /* -1: none, else the pc_fmt_stage_t that fails */
static bool s_fixed;                     /* the card in the slot mounts now (the player does not know) */
static pc_fmt_plan_t s_plan;
static uint32_t s_shown_serial;
static esp_timer_handle_t s_t;

const char *sd_hw_reason(void)
{
    if (g_sd_ok) return NULL;
    return s_fail[0] ? s_fail : "nocard";
}

bool sd_hw_card(uint64_t *bytes, uint32_t *serial)
{
    if (g_sd_ok || !strcmp(sd_hw_reason(), "nocard")) return false;
    *bytes = s_bytes;
    *serial = s_serial;
    return true;
}

static void mount_ok(void) { s_fail[0] = 0; g_sd_ok = true; s_fixed = false; }

static void finish(void *arg)
{
    (void)arg;
    esp_timer_delete(s_t);
    s_t = NULL;
    /* before any write: the checks of bsp_sdcard_format */
    if (s_fixed) { mount_ok(); sd_format_finish(ESP_ERR_INVALID_STATE, PC_FMT_STAGE_BEFORE); return; }
    if (!pc_fmt_entry_allowed(sd_hw_reason(), false, false)) { sd_format_finish(ESP_ERR_NOT_FOUND, PC_FMT_STAGE_BEFORE); return; }
    const pc_fmt_card_t shown = { s_plan.total_sectors, 512, s_shown_serial }, now = { s_bytes / 512, 512, s_serial };
    if (!pc_fmt_same_card(&shown, &now)) { sd_format_finish(ESP_ERR_INVALID_SIZE, PC_FMT_STAGE_BEFORE); return; }
    s_formats++;                                                       /* from here on the board writes */
    printf("SIM sd format fs=%s cluster=%lu bu=%lu align=%lu start=%lu sectors=%lu serial=%lX\n", pc_fmt_fs_name(s_plan.fs), (unsigned long)s_plan.cluster_bytes,
           (unsigned long)s_plan.bu_sectors, (unsigned long)s_plan.align, (unsigned long)s_plan.part_start, (unsigned long)s_plan.part_sectors, (unsigned long)s_shown_serial);
    if (s_formatfail == PC_FMT_STAGE_WRITE || s_formatfail == PC_FMT_STAGE_MKFS) { sd_format_finish(ESP_FAIL, s_formatfail); return; }
    if (s_formatfail == PC_FMT_STAGE_MOUNT) { snprintf(s_fail, sizeof s_fail, "err"); sd_format_finish(ESP_FAIL, PC_FMT_STAGE_MOUNT); return; }
    mount_ok();
    sd_format_finish(ESP_OK, PC_FMT_STAGE_MOUNT);
}

bool sd_hw_format(const pc_fmt_plan_t *p, uint32_t serial)
{
    if (s_t) return false;
    s_plan = *p;
    s_shown_serial = serial;
    const esp_timer_create_args_t a = { .callback = finish, .name = "simsd" };
    if (esp_timer_create(&a, &s_t) != ESP_OK) return false;
    esp_timer_start_once(s_t, 1500 * 1000);
    return true;
}

/* 記憶卡診斷 (card_test.h's lower half): the fake card reads at `simsd speed <raw16> [<raw64> [<file write> [<file read>]]]` (MB/s x 100; default 350 800 400 380);
 * every raw read and every file chunk the service makes advances the service's clock by its bytes at that speed (so a result is exact and a test takes no real time);
 * `simsd free <bytes>` (default 12 GB), the card name SIMSD, 40 MHz, FAT32 (exFAT above 32 GiB). A raw read past the end of the card fails. */
static uint32_t s_spd[4] = { 350, 800, 400, 380 };
static uint32_t s_reg[3];   /* `simsd region <start> <middle> <end>`: the 16 KB read speed per part of the card (0 = all follow s_spd[0]) */
static uint64_t s_free = 12000000000ull;
static int64_t s_fake_us;

bool card_hw_info(ct_info_t *i)
{
    if (!g_sd_ok) return false;
    memset(i, 0, sizeof *i);
    i->capacity = s_bytes;
    i->free_bytes = s_free;
    memcpy(i->name, "SIMSD", 6);
    i->freq_khz = 40000;
    i->serial = s_serial;
    i->fs = s_bytes > (32ull << 30) ? "exFAT" : "FAT32";
    return true;
}

int card_hw_read(uint32_t sector, uint32_t n, void *buf)
{
    if ((uint64_t)(sector + n) * 512 > s_bytes) return -1;
    memset(buf, 0, (size_t)n * 512);
    uint32_t spd = s_spd[n * 512 > 16384 ? 1 : 0];
    if (n * 512 <= 16384 && s_reg[0]) { const uint64_t q = s_bytes / 512 / 4; spd = sector < q ? s_reg[0] : sector < 3 * q ? s_reg[1] : s_reg[2]; }
    s_fake_us += (int64_t)n * 512 * 100 / spd;
    return 0;
}

int64_t card_hw_now_us(void) { return esp_timer_get_time() + s_fake_us; }
void card_hw_account(uint32_t bytes, bool write) { s_fake_us += (int64_t)bytes * 100 / s_spd[write ? 2 : 3]; }

/* simsd fail gpt|nofs|nocard|stuck|err (not mounted, that reason) | ok (mounted, the player knows) | fixed (the card mounts now, the player does not know) |
 * swap (another card of the same size) | size <bytes> | formatfail 0|1|write|mkfs|mount | "" (state) */
bool fake_sd_cmd(const char *a)
{
    unsigned long long n;
    if (!strncmp(a, "fail ", 5) && (!strcmp(a + 5, "gpt") || !strcmp(a + 5, "nofs") || !strcmp(a + 5, "nocard") || !strcmp(a + 5, "stuck") || !strcmp(a + 5, "err"))) {
        snprintf(s_fail, sizeof s_fail, "%s", a + 5);
        g_sd_ok = false;
    } else if (!strcmp(a, "ok")) mount_ok();
    else if (!strcmp(a, "fixed")) s_fixed = true;
    else if (!strcmp(a, "swap")) s_serial = s_serial == 0x5D01 ? 0x5D02 : 0x5D01;
    else if (sscanf(a, "size %llu", &n) == 1) s_bytes = n;
    else if (sscanf(a, "free %llu", &n) == 1) s_free = n;
    else if (!strncmp(a, "region ", 7)) { unsigned v[3]; if (sscanf(a + 7, "%u %u %u", v, v + 1, v + 2) == 3 && v[0] && v[1] && v[2]) for (int j = 0; j < 3; j++) s_reg[j] = v[j]; }
    else if (!strncmp(a, "speed ", 6)) { unsigned v[4]; int k = sscanf(a + 6, "%u %u %u %u", v, v + 1, v + 2, v + 3); for (int j = 0; j < k; j++) if (v[j]) s_spd[j] = v[j]; }
    else if (!strcmp(a, "formatfail 0")) s_formatfail = -1;
    else if (!strcmp(a, "formatfail 1") || !strcmp(a, "formatfail mkfs")) s_formatfail = PC_FMT_STAGE_MKFS;
    else if (!strcmp(a, "formatfail write")) s_formatfail = PC_FMT_STAGE_WRITE;
    else if (!strcmp(a, "formatfail mount")) s_formatfail = PC_FMT_STAGE_MOUNT;
    else if (a[0]) return false;
    const char *r = sd_hw_reason();
    printf("SIM sd mounted=%d reason=%s size=%llu formats=%d formatfail=%s serial=%lX fixed=%d\n", g_sd_ok, r ? r : "-", (unsigned long long)s_bytes, s_formats,
           s_formatfail < 0 ? "0" : pc_fmt_stage_name(s_formatfail), (unsigned long)s_serial, s_fixed);
    return true;
}
