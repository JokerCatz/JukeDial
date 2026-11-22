// SPDX-License-Identifier: LGPL-2.1-or-later
#include "keys_svc.h"
#include "usb_mode.h"
#include "flash_safe.h"
#include "pc/pc_paths.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "class/hid/hid_device.h"
#include "nvs.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_haptic.h"
#include "bsp/bsp_i2c.h"
#include "bsp/bsp_knob.h"
#include "bsp/bsp_sdcard.h"
#include "bsp/bsp_touch.h"
#include "cfg.h"
#include "lcd_clock.h"
#include "lab.h"
#include "sysmode.h"
#include "driver/usb_serial_jtag.h"
#include "pc/pc_plug.h"
#include "pc/pc_sdguide.h"
#include "esp_timer.h"
#include "ext_hid_send.h"
#include "ext_prio.h"
#include "ext_uac.h"
#include "ext_audio.h"
#include "ext_usbdesc.h"
#include "pc/pc_exthid.h"
#include "ext_mode.h"
#include "ext_xport.h"
#include "ext_ble.h"
#include "console.h"
#include "power_svc.h"
#include "ui/font.h"
#include "ui/ui.h"
#include "ui/ui_ring.h"

#define MAGIC 0x5553424Du                     /* "USBM" */
static RTC_NOINIT_ATTR uint32_t s_req;

bool usb_mode_peek_request(void) { return s_req == MAGIC; }   /* the boot front (boot_front.c): the same flag, not consumed */

bool usb_mode_take_request(void)
{
    bool r = s_req == MAGIC;
    s_req = 0;
    return r;
}

void usb_mode_request(void)
{
    s_req = MAGIC;
    sysmode_reboot();                        /* stores what is playing; normal boot after USB mode resumes it */
}

static void usb_card_release(const char *why);   /* below: the hand-back every way out of the mode goes through */

/* ---- descriptors (外接模式 T4): built at the mode start for the chosen function mix by ext_usbdesc_build (pc_extdesc: MSC, HID, AudioControl, AudioStreaming
 * as the mix has them), the HID report map is pc_exthid_map (consumer 1, keyboard 2, mouse 3). They live in one internal heap block (this boot mode only: no static internal RAM). ---- */
typedef struct { tusb_desc_device_t dev; tusb_desc_device_qualifier_t qual; uint8_t cfg[PC_EXTDESC_MAX_LEN]; } desc_blk_t;
EXT_RAM_BSS_ATTR static desc_blk_t *s_blk;
EXT_RAM_BSS_ATTR static int s_combo;
EXT_RAM_BSS_ATTR static pc_extdesc_info_t s_info;
EXT_RAM_BSS_ATTR static pc_extdesc_fifo_t s_fifo;
static const char *s_str[] = { (const char[]){ 0x09, 0x04 }, "JukeDial", "JukeDial", "", "JukeDial Card", "JukeDial Keys", "JukeDial Sound" };

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { (void)instance; return pc_exthid_map; }
uint16_t tud_hid_get_report_cb(uint8_t i, uint8_t id, hid_report_type_t t, uint8_t *b, uint16_t n) { (void)i; (void)id; (void)t; (void)b; (void)n; return 0; }
void tud_hid_set_report_cb(uint8_t i, uint8_t id, hid_report_type_t t, uint8_t const *b, uint16_t n) { (void)i; (void)id; (void)t; (void)b; (void)n; }
/* the function mix is kept in NVS (namespace ext, key usb): the 三合一 default, or one of the two exclusive fallbacks (spec section 2); changing it restarts into the mode */
static int combo_load(void)
{
    nvs_handle_t h;
    uint8_t v = PC_EXTCOMBO_ALL;
    if (nvs_open("ext", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, "usb", &v); nvs_close(h); }
    return ext_usbdesc_combo_norm(v);
}
int usb_mode_combo(void) { return s_combo; }
bool usb_mode_has_msc(void) { return pc_extdesc_combo_msc(s_combo); }
bool usb_mode_has_uac(void) { return pc_extdesc_combo_uac(s_combo); }
/* the NVS write goes through flash_safe_call (internal-RAM stack): the LVGL task's stack is in PSRAM and asserts in the flash write otherwise
 * ("esp_task_stack_is_sane_cache_disabled", seen on the peer board at the second tap of the USB 功能 cell) */
static void combo_write(void *a)
{
    nvs_handle_t h;
    if (nvs_open("ext", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "usb", (uint8_t)ext_usbdesc_combo_norm((int)(intptr_t)a)); nvs_commit(h); nvs_close(h); }
}
void usb_mode_set_combo(int combo)
{
    flash_safe_call(combo_write, (void *)(intptr_t)combo, 3000);
    printf("APP usb combo=%d: restart into the mode\n", combo);
    s_req = MAGIC;                           /* the next boot enters the external mode again (a new enumeration with the new descriptor) */
    cfg_save_now();                          /* pending settings first (g_sd_ok is false once the card went to the computer: then nothing is written) */
    usb_card_release("combo");
    esp_restart();
}

/* ---- state ---- */
/* The last MOUNT_COMPLETE put the card on our side: an eject (SCSI), or tud_umount_cb at the unplug. Starts false: the storage is created at
 * USB, and esp_tinyusb's mount-to-USB on the first connect finds it there and sends no event (final review I-1); a later connect moves
 * it back to USB with an event. where = pc_usb_where(tud_mounted(), this). */
EXT_RAM_BSS_ATTR static SemaphoreHandle_t s_card_mx;   /* created before the MSC storage; NULL without it (no drive: nothing to guard) */
void usb_mode_card_lock(void) { if (s_card_mx) xSemaphoreTake(s_card_mx, portMAX_DELAY); }
void usb_mode_card_unlock(void) { if (s_card_mx) xSemaphoreGive(s_card_mx); }
/* cardsafe: the hand-back state (PSRAM: no internal RAM). released = the card was let go on purpose (the computer went away without an eject, or we are leaving);
 * ejected = the card moved to our side while the computer was still configured (a SCSI eject: the clean way); was_mounted = the computer configured the device. */
EXT_RAM_BSS_ATTR static sdmmc_card_t *s_card_h;
EXT_RAM_BSS_ATTR static tinyusb_msc_storage_handle_t s_sh;
EXT_RAM_BSS_ATTR static volatile bool s_released, s_ejected, s_was_mounted;
EXT_RAM_BSS_ATTR static int s_lost_n;       /* link drops this mode (each one released the card) */
static volatile bool s_app_side;          /* no MSC in the mix: the card never leaves us, set at the start */
static void storage_cb(tinyusb_msc_storage_handle_t h, tinyusb_msc_event_t *e, void *arg)
{
    (void)h; (void)arg;
    if (e->id == TINYUSB_MSC_EVENT_MOUNT_START) { usb_mode_card_lock(); s_app_side = false; usb_mode_card_unlock(); }   /* before the FATFS goes: a reload in progress ends first, no later one starts */
    if (e->id == TINYUSB_MSC_EVENT_MOUNT_COMPLETE) {
        s_app_side = e->mount_point != TINYUSB_MSC_STORAGE_MOUNT_USB;
        s_ejected = s_app_side && tud_mounted();   /* still configured = an eject; not configured = the cable / the host went (tud_umount_cb runs this first) */
    }
    printf("APP usb msc event=%d where=%d\n", e->id, usb_mode_where());
}

static void usb_event(tinyusb_event_t *e, void *arg)   /* esp_tinyusb's event callback (the tinyusb task) */
{
    (void)arg;
    if (e->id == TINYUSB_EVENT_ATTACHED) s_was_mounted = true;
    if (e->id == TINYUSB_EVENT_DETACHED) printf("APP usb event detached ejected=%d\n", s_ejected);   /* the release itself runs in the mode's loop (this is the tinyusb task: its deferred writes cannot drain from here) */
    if (e->id == TINYUSB_EVENT_DETACHED && usb_mode_has_uac()) ext_uac_bus_gone();   /* unplug / SetConfiguration(0): the stream is over (suspend callbacks are not compiled in this sdkconfig) */
}
static char s_url[160];
static bool hid_sink(const pc_extsend_ev *e)       /* ext_send_pump's sink: the HID reports (ids of pc_exthid_map) */
{
    if (e->kind == PC_XEV_WAIT) return true;
    if (!tud_hid_ready()) return false;
    if (e->kind == PC_XEV_CONS) {
        uint16_t u = e->code;
        return tud_hid_report(PC_EXTHID_RID_CONSUMER, &u, 2);               /* a refused report is not a sent one: the pump retries it */
    }
    if (e->kind == PC_XEV_WHEEL) return tud_hid_mouse_report(PC_EXTHID_RID_MOUSE, 0, 0, 0, (int8_t)(int16_t)e->code, 0);   /* the knob's scroll role: wheel notches */
    uint8_t codes[6] = { (uint8_t)e->code };
    return tud_hid_keyboard_report(PC_EXTHID_RID_KEYBOARD, e->mod, codes);
}
static void key_task(void *a)
{
    (void)a;
    for (;;) {
        ext_xport_pump((uint32_t)(esp_timer_get_time() / 1000), hid_sink, tud_mounted());   /* USB or BLE, never both (pc_xport); a flush when neither can send */
        vTaskDelay(pdMS_TO_TICKS(ext_send_pending() ? 2 : 10));
    }
}

/* ---- the state the screen (ui/scr_ext.c) shows and changes; LVGL task ---- */
int usb_mode_where(void) { return s_released ? -1 : pc_usb_where(tud_mounted(), s_app_side); }
bool usb_mode_released(void) { return s_released; }
EXT_RAM_BSS_ATTR static bool s_slept;   /* the release was a clean bus suspend (PSRAM: no internal RAM for one flag) */
int usb_mode_link(void) { return pc_usb_link(tud_mounted(), s_was_mounted, s_ejected, s_released, s_slept); }
bool usb_mode_mounted(void) { return tud_mounted(); }
bool usb_mode_card_ours(void) { return s_app_side && !s_released; }
const char *usb_mode_keys_path(void) { return usb_mode_has_msc() ? PC_USB_CFG_KEYS : PC_CFG_KEYS; }
void usb_mode_type_url(void)
{
    char text[170];
    snprintf(text, sizeof text, "%s\n", s_url);
    printf("APP usb type url queued=%d\n", ext_send_text(text));
}
bool usb_mode_exit(bool force)
{
    bool in_use = usb_mode_has_msc() && usb_mode_where() == 1;      /* 1 only while mounted; without the drive the card is never the computer's */
    if (in_use && !force) return false;
    printf("APP usb mode exit in_use=%d\n", in_use);
    if (!in_use) cfg_save_now();             /* pending settings first; never while the computer owns the card */
    usb_card_release("exit");                /* stop the drive, drain, leave the card idle: a forced exit no longer resets it mid-write */
    esp_restart();                           /* the request was cleared at boot: a normal boot follows */
    return true;
}

/* ---- the internal RAM 10 s after the computer mounted the drive, for the next normal boot (spec 6; its console is on the other side
 * of the plug). 8 bytes of RTC memory: [0] = tag 0xB5 << 24 | free, [1] = check << 24 | min (24 bits each, internal RAM is < 16 MB);
 * check = a byte folded from free ^ min. Garbage after a cold start passes only with a 1 in 65536 chance and min <= free. ---- */
static RTC_NOINIT_ATTR uint32_t s_mem[2];
#define MEM_TAG 0xB5u
static uint32_t mem_check(uint32_t x) { return (0x5Bu ^ x ^ (x >> 8) ^ (x >> 16)) & 0xFFu; }
static void mem_note(void)
{
    uint32_t f = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) & 0xFFFFFFu, m = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) & 0xFFFFFFu;
    s_mem[0] = MEM_TAG << 24 | f;
    s_mem[1] = mem_check(f ^ m) << 24 | m;
    printf("APP usb mem free=%lu min=%lu\n", (unsigned long)f, (unsigned long)m);
}
bool usb_mode_mem_last(uint32_t *free_b, uint32_t *min_b)
{
    uint32_t a = s_mem[0], b = s_mem[1], f = a & 0xFFFFFFu, m = b & 0xFFFFFFu;
    s_mem[0] = s_mem[1] = 0;                 /* one print, then gone */
    if (a >> 24 != MEM_TAG || b >> 24 != mem_check(f ^ m) || m > f) return false;
    *free_b = f;
    *min_b = m;
    return true;
}

/* The hand-back (cardsafe), idempotent, from any task but the tinyusb task: the card is no longer ours-or-the-computer's from the page's point of view
 * (released first, so a settings reload in progress ends and no new one starts), then the MSC storage is deleted (esp_tinyusb refuses while deferred
 * writes are pending: those are the writes already acknowledged to the computer, so it is polled until they have reached the card, 1 s at most),
 * then the card is made idle and freed (bsp_sdcard_release_raw: ready, deselect, CMD0, host de-init). If the storage cannot be deleted the card is
 * left as it is (a free under a running write would be worse); the log says so. Up to about 1.6 s; the callers restart or show the lost state after. */
static void usb_card_release(const char *why)
{
    if (!s_card_h || !s_card_mx) return;
    usb_mode_card_lock();
    bool first = !s_released;
    if (first) s_slept = !strcmp(why, "suspend");   /* before s_released: the page never sees a release without its reason */
    s_released = true;
    s_app_side = false;
    usb_mode_card_unlock();
    if (!first) return;
    int64_t t0 = esp_timer_get_time();
    esp_err_t e = ESP_ERR_INVALID_STATE;
    for (int i = 0; i < 100 && e == ESP_ERR_INVALID_STATE; i++) {
        e = tinyusb_msc_delete_storage(s_sh);
        if (e == ESP_ERR_INVALID_STATE) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_slept) printf("APP usb lost n=%d why=suspend (the computer sleeps or stopped polling, no I/O in flight: harmless, tap Reconnect when it is awake)\n", ++s_lost_n);
    else if (!strcmp(why, "lost")) printf("APP usb lost n=%d why=lost (a bad cable resets the bus: try another)\n", ++s_lost_n);
    printf("APP usb card released why=%s storage=%s drain_ms=%d\n", why, esp_err_to_name(e), (int)((esp_timer_get_time() - t0) / 1000));
    if (e == ESP_OK) { sdmmc_card_t *c = s_card_h; s_card_h = NULL; bsp_sdcard_release_raw(c); }
    else printf("APP usb card release: storage busy, card left as it is\n");
}

/* The user's way back to the computer after a release (the Reconnect cell; LVGL task; never automatic): the card is opened again and a new MSC storage
 * presents it, then the device soft-disconnects for 100 ms so the computer re-enumerates and re-reads the medium (esp_tinyusb has no unit-attention call;
 * while released the storage is gone, the MSC reports no medium). 0 = attached, 1 = no host (plug the cable in first), 2 = could not (card busy / open failed).
 * s_was_mounted is cleared so the watch waits for the computer's next configuration before it can call the re-enumeration a loss. */
int usb_mode_reconnect(void)
{
    if (!s_released || !usb_mode_has_msc()) return 2;
    if (!tud_mounted()) { printf("APP usb reconnect refused: no host\n"); return 1; }
    if (s_card_h) { printf("APP usb reconnect refused: card still held\n"); return 2; }
    sdmmc_card_t *card = NULL;
    esp_err_t e = bsp_sdcard_open_raw(&card);
    tinyusb_msc_storage_handle_t sh = NULL;
    if (e == ESP_OK) {
        tinyusb_msc_storage_config_t sc = { .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB, .fat_fs = { .base_path = PC_USB, .config.max_files = 2, .do_not_format = true },
                                            .write_coalesce_bytes = (uint32_t)pc_usb_wbuf_norm(cfg_int("usb_wbuf_kb", 0)) * 1024 };
        sc.medium.card = card;
        e = tinyusb_msc_new_storage_sdmmc(&sc, &sh);
        if (e != ESP_OK) bsp_sdcard_release_raw(card);
    }
    if (e != ESP_OK) { printf("APP usb reconnect failed %s\n", esp_err_to_name(e)); return 2; }
    usb_mode_card_lock();
    s_card_h = card; s_sh = sh;
    s_ejected = false; s_app_side = false; s_was_mounted = false; s_released = false; s_slept = false;
    usb_mode_card_unlock();
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    tud_connect();
    printf("APP usb card attached by=user\n");
    return 0;
}

EXT_RAM_BSS_ATTR static bool s_active;   /* PSRAM: no internal RAM for one flag */
bool usb_mode_active(void) { return s_active; }

void usb_mode_run(void)
{
    s_active = true;
    printf("APP usb mode\n");
    g_sd_ok = bsp_sdcard_mount() == ESP_OK;
    cfg_load();
#if CONFIG_ESP_CONSOLE_UART
    if (cfg_int("ext_diag", 0)) console_start();     /* peer bench build with ext_diag: the UART console stays alive in this mode (ext key / ask / ... below); nothing else ever has a console here */
#endif
    s_combo = combo_load();
    s_blk = heap_caps_calloc(1, sizeof *s_blk, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);   /* this boot mode only */
    int dl = s_blk ? ext_usbdesc_build(s_combo, s_blk->cfg, sizeof s_blk->cfg, &s_info, &s_fifo) : -1;
    if (dl <= 0) { printf("APP usb descriptor failed %d: restart\n", dl); esp_restart(); }   /* a build error is a bug, not a state to run in */
    s_blk->dev = (tusb_desc_device_t){
        .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
        .bDeviceClass = TUSB_CLASS_MISC, .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = 0x303A, .idProduct = pc_extdesc_combo_pid(s_combo), .bcdDevice = 0x0100,
        .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
    };
    s_blk->qual = (tusb_desc_device_qualifier_t){
        .bLength = sizeof(tusb_desc_device_qualifier_t), .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER, .bcdUSB = 0x0200,
        .bDeviceClass = TUSB_CLASS_MISC, .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .bNumConfigurations = 1,
    };
    if (usb_mode_has_uac()) { ext_uac_init(&s_info); ext_audio_start(); }   /* the audio service first (volume, fixed limiter), the USB stack after: no block can come before the safety is in place */
    printf("APP usb combo=%d pid=%04x desc_len=%d itf=%d alts=%d rates=%d fifo rx=%u total=%u of %u\n", s_combo, pc_extdesc_combo_pid(s_combo), dl, s_info.n_itf, s_info.n_alt,
           s_info.n_rates, s_fifo.rx, s_fifo.total, PC_EXTDESC_FIFO_WORDS);
    {   /* the primary address of the setup_url list (the mirrors are on the help page) */
        char ub[256];
        const char *u[CFG_SETUP_URL_MAX];
        cfg_setup_urls(ub, sizeof ub, u);
        snprintf(s_url, sizeof s_url, "%s", u[0]);
    }
    esp_err_t ei = bsp_i2c_init();
    uint8_t hst = 0;
    if (ei == ESP_OK) bsp_haptic_init(&hst);  /* the shared host's tap and tick (k_usb_host.haptics); without it bsp_haptic_play only returns an error */
    lv_display_t *disp = NULL;
    bsp_display_set_task_stack(16 * 1024, true);
    bsp_display_set_rotation(cfg_rotation());
    lcd_clock_prepare(false);   /* experimental QSPI clock (lcd_clock.h); the line above: screen rotation from the card (0 without one); the panel, the touch and the keys follow */
    bsp_display_start_ex(&disp, (uint8_t)cfg_brightness());
    bsp_display_lock(-1);
    lv_indev_t *touch = NULL;
    bsp_touch_start(disp, &touch);
    ext_mode_start(PC_CFG_KEYS);   /* the card is still ours: the key layers (no file: the default 媒體鍵 layer); the send queue empty */
    bsp_knob_start(disp, false, NULL);
    keys_svc_start_host();                   /* the FakePod two keys = the knob detents (ext_knob); nothing on a board with a knob */
    ext_screen_start(disp, touch);           /* fonts from the flash pack only, the shared host (its knob hook: ext_knob), the page */
    bsp_display_unlock();

    esp_err_t e = ESP_OK;
    if (usb_mode_has_msc()) {
    /* the card goes to the computer: no filesystem on our side from here on */
    bsp_sdcard_unmount();
    g_sd_ok = false;
    sdmmc_card_t *card = NULL;
    e = bsp_sdcard_open_raw(&card);
    printf("APP usb card=%s\n", esp_err_to_name(e));
    if (e == ESP_OK) {
        s_card_mx = xSemaphoreCreateMutex();
        tinyusb_msc_storage_config_t sc = { .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
                                            .fat_fs = { .base_path = PC_USB, .config.max_files = 2, .do_not_format = true },
                                            .write_coalesce_bytes = (uint32_t)pc_usb_wbuf_norm(cfg_int("usb_wbuf_kb", 0)) * 1024 };   /* experimental write gathering, default off (esp_tinyusb UPSTREAM.txt) */
        sc.medium.card = card;
        tinyusb_msc_storage_handle_t sh = NULL;
        e = tinyusb_msc_new_storage_sdmmc(&sc, &sh);
        if (e == ESP_OK) tinyusb_msc_set_storage_callback(storage_cb, NULL);
        if (e == ESP_OK) { s_card_h = card; s_sh = sh; }
        printf("APP usb msc storage=%s bufsize=%d wbuf=%u\n", esp_err_to_name(e), (int)CONFIG_TINYUSB_MSC_BUFSIZE, (unsigned)(e == ESP_OK ? tinyusb_msc_write_coalesce_bytes(sh) : 0));   /* the class buffer (static, the size of every write chunk) and the gather size in effect (0 = off) */
    }
    } else s_app_side = true;                /* sound card without the drive: the card stays ours (_player/config/keys.txt is read from /sdcard any time) */
    ext_xport_start();                       /* the BLE stack once (kept for the whole mode; advertises only after unplug + yes) */
    tinyusb_config_t tc = TINYUSB_DEFAULT_CONFIG();
    tc.task = TINYUSB_TASK_CUSTOM(TINYUSB_DEFAULT_TASK_SIZE, EXT_PRIO_USB, TINYUSB_DEFAULT_TASK_AFFINITY);   /* the drive's level: below the keys, below the sound (peer test: priority 8 / 22 and core 1 change nothing for the iso OUT rate) */
    tc.event_cb = usb_event;
    tc.descriptor.device = &s_blk->dev;
    tc.descriptor.full_speed_config = s_blk->cfg;   /* the controller is high speed only; the same bytes serve the (never used) full-speed query */
    s_str[3] = ext_serial();
    tc.descriptor.string = s_str;
    tc.descriptor.string_count = sizeof s_str / sizeof s_str[0];
#if TUD_OPT_HIGH_SPEED
    tc.descriptor.high_speed_config = s_blk->cfg;
    tc.descriptor.qualifier = &s_blk->qual;
#endif
    e = tinyusb_driver_install(&tc);
    printf("APP usb driver=%s\n", esp_err_to_name(e));
    if (usb_mode_has_uac()) ext_uac_start();     /* the pump above the keys: endpoint FIFO -> ring, feedback, the mixer */
    xTaskCreatePinnedToCore(key_task, "usb_keys", 3072, NULL, EXT_PRIO_HID, NULL, 0);
    int dbg = 0, up = 0, susp = 0;
    bool io_at_susp = false;
    for (int i = 0;; i++) {
        for (int k = 0; k < 10; k++) {       /* 100 ms steps: a lost computer is noticed at once, no debounce */
            vTaskDelay(pdMS_TO_TICKS(100));
            /* What TinyUSB reports: tud_mounted() false after it was true = unplug (tud_umount_cb), VBUS gone, SetConfiguration(0) or a bus reset
             * that was not followed by a configuration (a reset raises no callback, the flag is cleared); tud_suspended() for 2 s without a resume
             * = the computer sleeps / stopped polling (the suspend callbacks are not compiled in this sdkconfig). A card that moved to our side
             * while still configured (a SCSI eject) is the clean way and is left alone. */
            bool m = tud_mounted();
            if (m) s_was_mounted = true;
            susp = tud_suspended() ? susp + 1 : 0;
            if (susp == 1 && s_card_h && !s_released) io_at_susp = tinyusb_msc_io_in_flight(s_sh);   /* the first moment of a suspend: was a command open? (esp_tinyusb patch) */
            if (usb_mode_has_msc() && s_card_h) {
                /* a clean suspend (no I/O in flight) is a sleeping computer: harmless, card released with its own message; a suspend with I/O in flight is a fault (pc_usb_release_why) */
                const int sus_ticks = lab_get(PC_LAB_USB_SUSPEND_MS) / 100;   /* lab knob (default 2000 ms; 0 = a suspend never releases) */
                const bool held = sus_ticks && susp >= sus_ticks;
                const char *why = pc_usb_release_why(m, s_was_mounted, s_ejected, s_released, held, io_at_susp, held && !s_released && s_card_h && tinyusb_msc_io_in_flight(s_sh));
                if (why) usb_card_release(why);
            }
        }
        if (up < 10 && tud_mounted() && ++up == 10) mem_note();   /* 10 s after the computer mounted it: once per USB mode */
        dbg = usb_serial_jtag_is_connected() ? dbg + 1 : 0;   /* the plug was turned over: settings side = normal mode */
        if (dbg >= 2) { printf("APP usb mode exit reason=debug_side\n"); cfg_save_now(); usb_card_release("debug_side"); esp_restart(); }
        if (i % 5 == 0) printf("APP usb mounted=%d where=%d link=%d released=%d\n", tud_mounted(), usb_mode_where(), usb_mode_link(), s_released);
    }
}

/* ---- normal mode: which side of the plug (pc_plug) ---- */
static void ask_pick(int idx)
{
    if (idx == 0) { usb_mode_request(); return; }
    ui_pop();
}
static void ask_back(void) { ui_pop(); }
static void ask_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[2] = { T(S_SWITCH_TO_USB_DRIVE) /* 切換成 USB 隨身碟 */, T(S_NO) /* 不要 */ };
    lv_obj_t *r = ui_ring_create(p, labels, 2, ask_pick, ask_back);
    ui_ring_set_system(r, 0);
    ui_ring_set_item_hint(r, 0, T(S_REBOOTS_AS_A_USB_DRIVE_MUSIC_STOPS) /* 重開機成隨身碟 （音樂會停） */);
    ui_ring_set_center_text(r, T(S_PLUGGED_IN_ON_THE_DRIVE_SIDE_SETTINGS) /* 電腦接在翻面那一面 （改設定請插另一面） */);
}
static const ui_screen_t scr_usb_ask = { .name = "usb_ask", .create = ask_create };
static pc_plug_t s_plug;
static void watch(lv_timer_t *t)
{
    (void)t;
    bool dbg = usb_serial_jtag_is_connected();
    if (pc_plug_tick(&s_plug, power_svc_state() == PC_PWR_USB, dbg)) {
        printf("APP usb side=otg: asking\n");
        if (ui_top() != &scr_usb_ask) ui_push(&scr_usb_ask, NULL);
    }
}
void usb_mode_watch_start(void) { lv_timer_create(watch, 1000, NULL); }

/* console "ext <...>" (peer bench build, ext_diag on): drives the mode from the UART. LVGL lock held for the model / screen calls. */
/* the screen-side commands run in the LVGL task (a cell tap may write NVS: the console task's stack is in PSRAM, flash writes from there assert) */
static char s_con_cmd[48];
static void con_ui(void *arg)
{
    (void)arg;
    const char *a = s_con_cmd;
    int n = 0;
    if (sscanf(a, "tap %d", &n) == 1) ext_screen_tap(n);
    else if (!strcmp(a, "ask yes")) ext_screen_ask(true);
    else if (!strcmp(a, "ask no")) ext_screen_ask(false);
    else if (!strncmp(a, "cell ", 5)) ext_screen_arm_cell(a + 5);
    else if (!strcmp(a, "unpair")) ext_ble_unpair();
}
void usb_mode_console(const char *a)
{
    int n = 0;
    if (!s_active) { printf("APP ext console: not in the external mode\n"); return; }
    bsp_display_lock(-1);
    if (sscanf(a, "key %d", &n) == 1) printf("APP ext console key %d -> %d\n", n, ext_key(n));
    else if (sscanf(a, "knob %d", &n) == 1) printf("APP ext console knob %d -> %d\n", n, ext_knob(n));
    else if (sscanf(a, "layer %d", &n) == 1) { ext_layer_step(n); printf("APP ext console layer=%d\n", ext_layer()); }
    else if (!strncmp(a, "tap ", 4) || !strncmp(a, "ask ", 4) || !strncmp(a, "cell ", 5) || !strcmp(a, "unpair")) { snprintf(s_con_cmd, sizeof s_con_cmd, "%s", a); lv_async_call(con_ui, NULL); }
    else if (!strcmp(a, "req")) printf("APP ext console req -> %d\n", ext_xport_request());
    else if (!strcmp(a, "state"))
        printf("APP ext console state sender=%d ask=%d ble_ok=%d link=%d adv=%d conn=%d peer=%s bonded=%d mounted=%d\n", ext_xport_sender(), ext_xport_ask(), ext_xport_ble_ok(), ext_xport_link_ok(),
               ext_ble_advertising(), ext_ble_connected(), ext_ble_peer(), ext_ble_bonded(), tud_mounted());
    else printf("APP ext console: key N | knob D | layer D | tap N | ask yes|no | req | cell NAME | unpair | state\n");
    bsp_display_unlock();
}
