// SPDX-License-Identifier: LGPL-2.1-or-later
#include "boot_front.h"
#include <stdio.h>
#include <string.h>
#include "bsp/bsp_display.h"
#include "cast_mode.h"
#include "cfg.h"
#include "lcd_clock.h"
#include "charge_mode.h"
#include "instrument.h"
#include "sdkconfig.h"
#include "splash.h"
#include "usb_mode.h"
#if !SIM
#include "guard.h"
#endif

esp_err_t bsp_i2c_init(void);         /* bsp/bsp_i2c.h, bsp/bsp_sdcard.h (not included: their ESP driver types are no use to the simulator, whose shim_bsp.c provides these) */
esp_err_t bsp_sdcard_mount(void);
esp_err_t bsp_sdcard_unmount(void);

void boot_front_mode(const char *mode)
{
    const bool player = !strcmp(mode, "player");
    printf("APP boot front: mode=%s\n", mode);
    g_sd_ok = bsp_sdcard_mount() == ESP_OK;           /* the call normal boot makes (a second one is a no-op success) */
    printf("APP boot front: mount card=%d\n", g_sd_ok);
    cfg_load();                                       /* brightness, rotation, splash, language: as each mode reads them (a second cfg_load re-reads the same file) */
    printf("APP boot front: config\n");
    if (cfg_int("splash", 1) == 0) goto release;      /* the off switch: no display start here either, the mode starts its own as today */
    {
        const bool pic = splash_prepare(mode);        /* file read, closed, decoded into PSRAM */
        if (!pic) goto release;
        bsp_i2c_init();                               /* every mode does this before its display (idempotent) */
        bsp_display_set_rotation(cfg_rotation());
    lcd_clock_prepare(false);   /* the experimental QSPI clock (lcd_clock.h) */
        bsp_display_set_task_stack(player ? 24 * 1024 : 16 * 1024, true);   /* the mode's own numbers (app_main.c / the mode entries): the later start_ex call changes nothing */
        lv_display_t *disp = NULL;
        esp_err_t ed = bsp_display_start_ex(&disp, 0);   /* dark: the picture comes up from 0 */
        printf("APP boot front: display ok=%d\n", ed == ESP_OK);
        if (ed == ESP_OK) splash_run(disp);
        else splash_drop();                           /* no screen (the peer bench board): the mode runs as today */
    }
release:
    if (!strcmp(mode, "usb") && g_sd_ok) {            /* the drive mode finds the card as it expects it: not mounted by us (its own mount, presence check and message are unchanged) */
        bsp_sdcard_unmount();
        g_sd_ok = false;
        printf("APP boot front: card released\n");
    }
    printf("APP boot front: handover mode=%s\n", mode);
}

#if !SIM
void boot_front_run(void)
{
#if CONFIG_APP_TEST_NO_SD
    return;
#else
    if (guard_last_reset_was_crash()) return;         /* after a crash: straight to the mode, no picture (a crash inside the front can never loop) */
    const char *mode = usb_mode_peek_request() ? "usb" : instrument_peek_request() ? "inst" : cast_mode_peek_request() ? "cast" : charge_mode_wanted() ? "charge" : "player";   /* the dispatch's own order */
    boot_front_mode(mode);
#endif
}
#endif
