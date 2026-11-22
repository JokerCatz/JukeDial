// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sd_protocol_types.h"
#include "pc/pc_fmtguard.h"

#define BSP_SD_MOUNT_POINT "/sdcard"

typedef struct {
    uint64_t capacity_bytes;
    char name[8];
    uint32_t freq_khz;
    bool high_capacity;
    uint32_t serial;          /* the CID serial (the card diagnostics file names the card by it) */
} bsp_sdcard_info_t;

/* SDMMC 4-bit, high speed, never formats (the one format is bsp_sdcard_format below). */
esp_err_t bsp_sdcard_mount(void);
/* A failed mount prints "APP sd mount failed reason=<nocard|stuck|gpt|nofs|err> err=<esp_err_to_name> guide=<insert|repower|format|reinsert>" and keeps the reason word here (NULL after a
 * good mount): nocard = no card answered (timeout / not found), gpt = it answered and carries a GPT partition table (FatFs is built without
 * 64-bit LBA: use MBR), nofs = it answered and the filesystem did not mount, stuck = it answered and cannot be initialised (a card left in a bad state by an unclean
 * hand-back: a full power-off or re-inserting it brings it back; never formatted), err = any other error (out of memory, bad state, CRC, ...). */
const char *bsp_sdcard_fail_reason(void);
void bsp_sdcard_release_raw(sdmmc_card_t *card);   /* USB mode hand-back: wait ready, deselect, CMD0, free the handle, de-init the host (the MSC storage is gone first) */
esp_err_t bsp_sdcard_unmount(void);
esp_err_t bsp_sdcard_info(bsp_sdcard_info_t *out);
/* 記憶卡診斷 (cardtest): read `count` 512-byte sectors from the MOUNTED card, straight from the block device (below the files), through FatFs'
 * own disk_read on the card's drive, the same path every file read ends in. Read only: there is no raw write in the firmware but the format.
 * `buf` as the SD host wants it (128-byte aligned, PSRAM is fine: the decoders' read-ahead buffers are). ESP_ERR_INVALID_STATE when not mounted. */
esp_err_t bsp_sdcard_read_raw(uint32_t sector, uint32_t count, void *buf);
/* The card without a filesystem (USB mass storage hands it to the computer). Call after bsp_sdcard_unmount(). */
esp_err_t bsp_sdcard_open_raw(sdmmc_card_t **out);

/* The card without a filesystem, looked at once (a failed mount: the 記憶卡 page shows its size): sectors, sector size and the CID serial.
 * Refuses while mounted. Leaves the SD host released, as probe_reason does. */
esp_err_t bsp_sdcard_raw_info(uint64_t *sectors, uint32_t *sector_size, uint32_t *serial);
/* 記憶卡格式化 (cardfmt): the ONLY path to f_mkfs in the firmware. Formats the card in the slot as plan `p` says (pc_fmt_plan: MBR with one
 * partition at the SD boundary unit, FAT16 / FAT32 / exFAT, the cluster size, volume label JUKEDIAL), then mounts it. Refuses (and writes nothing):
 *   ESP_ERR_INVALID_STATE  a card is mounted, or the card mounts NOW (it is then left mounted: never format a card that mounts);
 *   ESP_ERR_NOT_FOUND      the new mount failed with any reason but "gpt" / "nofs" (no card, other error);
 *   ESP_ERR_INVALID_SIZE   another card than the one the plan was made for (sector count, sector size or CID serial differ);
 *   ESP_ERR_INVALID_ARG    no plan / a work buffer under 32 KB or not whole sectors.
 * `work` (the caller's, in PSRAM) is f_mkfs's work area. Blocking (seconds to minutes): call it from a worker task, never the LVGL task.
 * *stage (pc_fmt_stage_t) says how far it got, so the caller can say whether the card was touched (pc_fmt_outcome): every refusal above is
 * PC_FMT_STAGE_BEFORE (nothing written); WRITE is set right before the first sector write, MKFS before f_mkfs, MOUNT once the volume is made.
 * ESP_OK = formatted and mounted. Prints APP sd format layout / mkfs lines. */
esp_err_t bsp_sdcard_format(const pc_fmt_plan_t *p, uint32_t serial, uint32_t disk_sig, uint8_t *work, size_t work_len, int *stage);
