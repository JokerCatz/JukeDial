// SPDX-License-Identifier: LGPL-2.1-or-later
/* 外接模式 T2: the layer / key model (ext_mode.h). */
#include "ext_mode.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "audio_svc.h"
#include "ext_audio.h"
#include "ext_hid_send.h"
#include "ui/ui.h"
#include "font.h"
#include "usb_mode.h"

EXT_RAM_BSS_ATTR static pc_extkeys_cfg_t s_cfg, s_tmp;
EXT_RAM_BSS_ATTR static pc_extkeys_res_t s_res, s_tres;
EXT_RAM_BSS_ATTR static char s_buf[PC_EXTKEYS_FILE_MAX + 1];
EXT_RAM_BSS_ATTR static int s_layer, s_gen;
EXT_RAM_BSS_ATTR static bool s_episode;       /* this eject / unplug has had its reload */

const pc_extkeys_cfg_t *ext_cfg(void) { return &s_cfg; }
const pc_extkeys_res_t *ext_res(void) { return &s_res; }
int ext_layer(void) { return s_layer; }
int ext_generation(void) { return s_gen; }
void ext_layer_step(int d)
{
    int n = s_cfg.nlayers;
    if (n > 0) s_layer = ((s_layer + d) % n + n) % n;
}

/* the file into s_buf: 1 = read (len in *len, one byte past the limit says "too big"), 0 = there is no file */
static int read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    *len = fread(s_buf, 1, PC_EXTKEYS_FILE_MAX + 1, f);
    fclose(f);
    return 1;
}
static void apply(const pc_extkeys_cfg_t *c, const pc_extkeys_res_t *r)
{
    s_cfg = *c;
    s_res = *r;
    if (s_layer >= s_cfg.nlayers) s_layer = 0;
    s_gen++;
    printf("APP ext cfg layers=%d from_file=%d ok=%d errors=%d\n", s_cfg.nlayers, s_res.from_file, s_res.ok, s_res.total);
}
void ext_mode_start(const char *path)
{
    ext_send_init();
    size_t n = 0;
    pc_extkeys_parse(NULL, 0, &s_tmp, &s_tres);
    if (read_file(path, &n)) pc_extkeys_parse(s_buf, n, &s_tmp, &s_tres);
    s_layer = 0;
    s_episode = false;
    apply(&s_tmp, &s_tres);
}

int ext_reload(void)
{
    usb_mode_card_lock();                      /* the card's MOUNT_START waits for this: the FATFS cannot go while it is read (esp_tinyusb frees it right after that event) */
    int w = usb_mode_where();
    if (w == 1) { usb_mode_card_unlock(); return EXT_RL_IN_USE; }
    if (!usb_mode_card_ours()) { usb_mode_card_unlock(); return EXT_RL_NOT_OURS; }
    size_t n = 0;
    pc_extkeys_parse(NULL, 0, &s_tmp, &s_tres);
    if (read_file(usb_mode_keys_path(), &n)) pc_extkeys_parse(s_buf, n, &s_tmp, &s_tres);
    usb_mode_card_unlock();
    apply(&s_tmp, &s_tres);
    return EXT_RL_OK;
}
void ext_poll(void)
{
    if (usb_mode_card_ours()) {
        if (!s_episode) { s_episode = true; ext_reload(); }
    } else s_episode = false;
}

bool ext_key(int i)
{
    if (s_layer >= s_cfg.nlayers || i < 0 || i >= s_cfg.layers[s_layer].nkeys) return false;
    const pc_extkeys_key_t *k = &s_cfg.layers[s_layer].keys[i];
    bool ok = ext_send_key(k);
    printf("APP ext key layer=%d key=%d act=%d queued=%d\n", s_layer, i, k->act, ok);
    return ok;
}
bool ext_knob(int d)
{
    int knob = s_layer < s_cfg.nlayers ? s_cfg.layers[s_layer].knob : PC_KNOB_NONE;
    if (knob == PC_KNOB_VOLUME && usb_mode_has_uac() && ext_audio_volume_step(d)) {   /* the computer is streaming to the sound card: the volume role is the device's own volume, not the computer's (owner default: 音效卡開著時旋鈕＝本機裝置音量); scroll / arrows stay with the computer */
        ui_volume_shown(audio_svc_volume_last());
        return true;
    }
    int n = ext_send_knob(knob, d);
    printf("APP ext knob role=%d detents=%d queued=%d\n", knob, d, n);
    return true;
}
const char *ext_knob_word(void)
{
    switch (s_layer < s_cfg.nlayers ? s_cfg.layers[s_layer].knob : PC_KNOB_NONE) {
    case PC_KNOB_VOLUME: return T(S_VOLUME_2) /* 音量 */;
    case PC_KNOB_SCROLL: return T(S_SCROLL) /* 捲動 */;
    case PC_KNOB_ARROWS: return T(S_ARROW_KEYS) /* 方向鍵 */;
    default: return T(S_NOTHING) /* 無 */;
    }
}
