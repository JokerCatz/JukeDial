// SPDX-License-Identifier: LGPL-2.1-or-later
#include "bsp/bsp_sdcard.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "bsp/bsp_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
#include "sdmmc_cmd.h"
#include "sd_protocol_defs.h"
#include "esp_private/sdmmc_common.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pc/pc_sdguide.h"

static const char *TAG = "bsp_sd";
static sdmmc_card_t *s_card;
static const char *s_fail;   /* why the last mount failed: "nocard", "stuck", "gpt", "nofs" or "err"; NULL after a good mount */

static void slot_cfg(sdmmc_host_t *host, sdmmc_slot_config_t *slot)
{
    *host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    host->max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    *slot = (sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
    slot->clk = BSP_PIN_SD_CLK;
    slot->cmd = BSP_PIN_SD_CMD;
    slot->d0 = BSP_PIN_SD_D0;
    slot->d1 = BSP_PIN_SD_D1;
    slot->d2 = BSP_PIN_SD_D2;
    slot->d3 = BSP_PIN_SD_D3;
    slot->width = 4;
    slot->flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
}

esp_err_t bsp_sdcard_open_raw(sdmmc_card_t **out)
{
    ESP_RETURN_ON_FALSE(!s_card && out, ESP_ERR_INVALID_STATE, TAG, "still mounted");
    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    slot_cfg(&host, &slot);
    sdmmc_card_t *c = calloc(1, sizeof *c);
    ESP_RETURN_ON_FALSE(c, ESP_ERR_NO_MEM, TAG, "card");
    esp_err_t e = host.init();
    bool inited = e == ESP_OK;          /* ours to release again if a later step fails (INVALID_STATE: someone else's, left alone) */
    if (e == ESP_OK || e == ESP_ERR_INVALID_STATE) e = sdmmc_host_init_slot(host.slot, &slot);
    if (e == ESP_OK) e = sdmmc_card_init(&host, c);
    if (e != ESP_OK) { if (inited) sdmmc_host_deinit(); free(c); return e; }
    *out = c;
    return ESP_OK;
}

/* A failed mount has released everything (card, host, VFS), so look again. FatFs reads no GPT (FF_LBA64 is 0); a GPT card's protective
 * MBR (sector 0, an entry of type 0xEE) is the one failure worth naming. Only ESP_FAIL (the card answered, f_mount did not) gets here.
 * Leaves the SD host as it found it on every path: open_raw releases the host itself when it fails after host.init(); on success the
 * host is deinitialised here, after the card handle and the sector buffer are freed. A card that does not initialise a second time either is
 * not a file-system failure: it answered (not "nocard") and cannot be set up (cardsafe: "stuck"; today's single retry, no recovery ladder). */
static const char *probe_reason(void)
{
    sdmmc_card_t *c = NULL;
    esp_err_t oe = bsp_sdcard_open_raw(&c);
    if (oe != ESP_OK) return pc_sdrec_reason(oe == ESP_ERR_TIMEOUT || oe == ESP_ERR_NOT_FOUND ? PC_SDE_SILENT : oe == ESP_FAIL || oe == ESP_ERR_INVALID_CRC || oe == ESP_ERR_INVALID_RESPONSE ? PC_SDE_INITFAIL : PC_SDE_OTHER, NULL);   /* the host is already released */
    const char *why = "nofs";
    size_t n = c->csd.sector_size >= 512 ? (size_t)c->csd.sector_size : 512;
    uint8_t *b = heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (b && sdmmc_read_sectors(c, b, 0, 1) == ESP_OK && b[510] == 0x55 && b[511] == 0xAA)
        for (int i = 0; i < 4; i++) if (b[446 + 16 * i + 4] == 0xEE) why = "gpt";
    free(b);
    free(c);
    sdmmc_host_deinit();
    return why;
}

esp_err_t bsp_sdcard_mount(void)
{
    if (s_card) return ESP_OK;
    const esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false, .max_files = 10, .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    slot_cfg(&host, &slot);
    esp_err_t e = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot, &mc, &s_card);
    if (e == ESP_OK) { s_fail = NULL; return e; }
    s_card = NULL;
    /* nocard: no card answered (sdmmc_card_init: ESP_ERR_TIMEOUT; ESP_ERR_NOT_FOUND from the host). ESP_FAIL: the card answered and the file system
     * did not mount (gpt or nofs), or its init failed (stuck: probe_reason tells them apart; INVALID_CRC / INVALID_RESPONSE from the init say stuck
     * too). Anything else (NO_MEM, INVALID_STATE, NOT_SUPPORTED, ...): err. */
    s_fail = e == ESP_ERR_TIMEOUT || e == ESP_ERR_NOT_FOUND ? "nocard" : e == ESP_FAIL ? probe_reason()
           : e == ESP_ERR_INVALID_CRC || e == ESP_ERR_INVALID_RESPONSE ? "stuck" : "err";
    printf("APP sd mount failed reason=%s err=%s guide=%s\n", s_fail, esp_err_to_name(e), pc_sd_guide_key(s_fail));
    return e;
}

const char *bsp_sdcard_fail_reason(void) { return s_fail; }

/* USB drive mode hand-back (cardsafe): `card` came from bsp_sdcard_open_raw and its MSC storage is already deleted (no I/O in flight). Waits for
 * the card's own programming to finish (CMD13: ready, transfer state; at most 500 ms), deselects it (CMD7 with RCA 0), sends it to idle (CMD0),
 * frees the handle and de-initialises the host. Every step's result is printed; none of them blocks longer than its timeout. */
void bsp_sdcard_release_raw(sdmmc_card_t *card)
{
    if (!card) return;
    int64_t t0 = esp_timer_get_time();
    uint32_t st = 0;
    esp_err_t es = ESP_FAIL;
    for (int i = 0; i < 50; i++) {
        es = sdmmc_send_cmd_send_status(card, &st);
        if (es == ESP_OK && (st & (1u << 8)) && ((st >> 9) & 0xF) == 4) break;   /* READY_FOR_DATA and state tran */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    sdmmc_command_t sel = { .opcode = MMC_SELECT_CARD, .arg = 0, .flags = SCF_CMD_AC };
    esp_err_t ed = sdmmc_send_cmd(card, &sel);
    esp_err_t ei = sdmmc_send_cmd_go_idle_state(card);
    printf("APP sd release status=%s state=%u ready_ms=%d deselect=%s idle=%s\n", esp_err_to_name(es), (unsigned)((st >> 9) & 0xF), (int)((esp_timer_get_time() - t0) / 1000),
           esp_err_to_name(ed), esp_err_to_name(ei));
    free(card);
    sdmmc_host_deinit();
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (!s_card) return ESP_OK;
    esp_err_t e = esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, s_card);
    s_card = NULL;
    return e;
}

esp_err_t bsp_sdcard_info(bsp_sdcard_info_t *out)
{
    ESP_RETURN_ON_FALSE(s_card && out, ESP_ERR_INVALID_STATE, TAG, "not mounted");
    out->capacity_bytes = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
    memset(out->name, 0, sizeof out->name);
    memcpy(out->name, s_card->cid.name, sizeof out->name - 1);
    out->freq_khz = (uint32_t)s_card->max_freq_khz;
    out->high_capacity = (s_card->ocr & SD_OCR_SDHC_CAP) != 0;
    out->serial = (uint32_t)s_card->cid.serial;
    return ESP_OK;
}

esp_err_t bsp_sdcard_read_raw(uint32_t sector, uint32_t count, void *buf)
{
    ESP_RETURN_ON_FALSE(s_card && buf && count, ESP_ERR_INVALID_STATE, TAG, "not mounted");
    return disk_read(ff_diskio_get_pdrv_card(s_card), buf, sector, count) == RES_OK ? ESP_OK : ESP_FAIL;   /* FatFs' own block layer, below the files; read only */
}

esp_err_t bsp_sdcard_raw_info(uint64_t *sectors, uint32_t *sector_size, uint32_t *serial)
{
    ESP_RETURN_ON_FALSE(!s_card && sectors && sector_size && serial, ESP_ERR_INVALID_STATE, TAG, "mounted");
    sdmmc_card_t *c = NULL;
    esp_err_t e = bsp_sdcard_open_raw(&c);
    if (e != ESP_OK) return e;
    *sectors = (uint64_t)c->csd.capacity;
    *sector_size = (uint32_t)c->csd.sector_size;
    *serial = (uint32_t)c->cid.serial;
    free(c);
    sdmmc_host_deinit();
    return ESP_OK;
}

/* the old table's traces first (a GPT: header + entries at LBA 1..33 and the backup at the end), then the new MBR at LBA 0 */
#define GPT_SECTORS 33
static esp_err_t write_layout(sdmmc_card_t *c, const pc_fmt_plan_t *p, uint32_t disk_sig, uint8_t *work)
{
    memset(work, 0, GPT_SECTORS * 512);
    esp_err_t e = sdmmc_write_sectors(c, work, 1, GPT_SECTORS);
    if (e == ESP_OK) e = sdmmc_write_sectors(c, work, p->total_sectors - GPT_SECTORS, GPT_SECTORS);
    if (e != ESP_OK) return e;
    pc_fmt_mbr(p, disk_sig, work);
    return sdmmc_write_sectors(c, work, 0, 1);
}

/* f_mkfs into partition 1 of the MBR just written (FF_MULTI_PARTITION: VolToPart says which partition a logical drive is; IDF maps drive i to
 * physical drive i, partition 0 = auto), then the volume label. The mapping is put back on every path. */
static esp_err_t make_fs(BYTE pdrv, const pc_fmt_plan_t *p, uint8_t *work, size_t len)
{
    if (VolToPart[pdrv].pd != pdrv) return ESP_ERR_INVALID_STATE;
    char drv[3] = { (char)('0' + pdrv), ':', 0 }, lab[16];
    const MKFS_PARM opt = { .fmt = p->fs == PC_FMT_EXFAT ? FM_EXFAT : p->fs == PC_FMT_FAT32 ? FM_FAT32 : FM_FAT, .n_fat = p->n_fat,
                            .align = p->align, .n_root = 0, .au_size = p->cluster_bytes };
    BYTE pt = VolToPart[pdrv].pt;
    VolToPart[pdrv].pt = 1;
    FRESULT r = f_mkfs(drv, &opt, work, (UINT)len);
    FRESULT rl = FR_OK;
    if (r == FR_OK) {
        FATFS *fs = heap_caps_calloc(1, sizeof *fs, MALLOC_CAP_SPIRAM);
        snprintf(lab, sizeof lab, "%s%s", drv, "JUKEDIAL");
        rl = !fs ? FR_NOT_ENOUGH_CORE : f_mount(fs, drv, 1);
        if (rl == FR_OK) rl = f_setlabel(lab);
        if (fs) { f_mount(NULL, drv, 0); free(fs); }
    }
    VolToPart[pdrv].pt = pt;
    printf("APP sd format mkfs fr=%d label_fr=%d\n", (int)r, (int)rl);   /* a label that failed does not fail the format: the volume is there */
    return r == FR_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_sdcard_format(const pc_fmt_plan_t *p, uint32_t serial, uint32_t disk_sig, uint8_t *work, size_t work_len, int *stage)
{
    int dummy;
    if (!stage) stage = &dummy;
    *stage = PC_FMT_STAGE_BEFORE;                                          /* nothing written until it says WRITE */
    if (!p || !work || work_len < 32 * 1024 || work_len % 512) return ESP_ERR_INVALID_ARG;
    if (s_card) return ESP_ERR_INVALID_STATE;                              /* never a mounted card */
    if (bsp_sdcard_mount() == ESP_OK) return ESP_ERR_INVALID_STATE;        /* the card as it is now: one that mounts stays as it is (and mounted) */
    if (!pc_fmt_entry_allowed(s_fail, false, false)) return ESP_ERR_NOT_FOUND;   /* only a card that answers and failed with gpt / nofs */
    sdmmc_card_t *c = NULL;
    esp_err_t e = bsp_sdcard_open_raw(&c);
    if (e != ESP_OK) return e;
    const pc_fmt_card_t shown = { p->total_sectors, 512, serial }, now = { (uint64_t)c->csd.capacity, (uint32_t)c->csd.sector_size, (uint32_t)c->cid.serial };
    if (!pc_fmt_same_card(&shown, &now)) e = ESP_ERR_INVALID_SIZE;        /* another card than the one the warning page showed (host-tested check) */
    BYTE pdrv = 0xFF;
    if (e == ESP_OK) e = ff_diskio_get_drive(&pdrv);
    if (e == ESP_OK && VolToPart[pdrv].pd != pdrv) e = ESP_ERR_NOT_SUPPORTED;   /* make_fs's drive mapping, checked before any write (review I1) */
    if (e == ESP_OK) {
        ff_diskio_register_sdmmc(pdrv, c);
        *stage = PC_FMT_STAGE_WRITE;                                       /* from here on the card may be changed */
        e = write_layout(c, p, disk_sig, work);
        printf("APP sd format layout err=%s start=%lu sectors=%lu\n", esp_err_to_name(e), (unsigned long)p->part_start, (unsigned long)p->part_sectors);
        if (e == ESP_OK) { *stage = PC_FMT_STAGE_MKFS; e = make_fs(pdrv, p, work, work_len); }
        ff_diskio_unregister(pdrv);
    }
    free(c);
    sdmmc_host_deinit();
    if (e == ESP_OK) { *stage = PC_FMT_STAGE_MOUNT; e = bsp_sdcard_mount(); }   /* formatted: an error now is the mount's */
    return e;
}
