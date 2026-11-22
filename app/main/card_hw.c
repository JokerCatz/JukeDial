// SPDX-License-Identifier: LGPL-2.1-or-later
/* 記憶卡診斷: the board's lower half (card_test.h). The simulator's is sim/fake_sd.c. Both boards: the card is mounted by bsp_sdcard.c. */
#include <string.h>
#include "bsp/bsp_sdcard.h"
#include "card_test.h"
#include "esp_timer.h"
#include "ff.h"

bool card_hw_info(ct_info_t *i)
{
    bsp_sdcard_info_t si;
    if (bsp_sdcard_info(&si) != ESP_OK) return false;
    memset(i, 0, sizeof *i);
    i->capacity = si.capacity_bytes;
    memcpy(i->name, si.name, sizeof i->name);
    i->freq_khz = si.freq_khz;
    i->serial = si.serial;
    FATFS *fs = NULL;
    DWORD n = 0;
    if (f_getfree("0:", &n, &fs) == FR_OK && fs) {                    /* the volume's cached count (FSInfo / bitmap); a first call may scan the table */
        i->free_bytes = (uint64_t)n * fs->csize * 512u;
        i->fs = fs->fs_type == FS_EXFAT ? "exFAT" : fs->fs_type == FS_FAT32 ? "FAT32" : fs->fs_type == FS_FAT16 ? "FAT16" : "FAT12";
    }
    return true;
}

int card_hw_read(uint32_t sector, uint32_t n, void *buf) { return bsp_sdcard_read_raw(sector, n, buf) == ESP_OK ? 0 : -1; }
int64_t card_hw_now_us(void) { return esp_timer_get_time(); }
void card_hw_account(uint32_t bytes, bool write) { (void)bytes; (void)write; }
