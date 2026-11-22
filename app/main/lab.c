// SPDX-License-Identifier: LGPL-2.1-or-later
/* LAB run-time knobs (lab.h). State in PSRAM: no internal RAM of our own. */
#include "lab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cfg.h"
#include "esp_attr.h"
#include "bsp/bsp_geom.h"
#if !SIM
#include "guard.h"
#if !BSP_LCD_ROUND
#include "bsp/bsp_lab.h"     /* the FakePod's BSP: the panel / touch / DAC knobs */
#endif
#endif

EXT_RAM_BSS_ATTR static int s_v[PC_LAB_N];
EXT_RAM_BSS_ATTR static bool s_ready, s_logged;

static int dflt(int i) { return pc_lab_default(i, BSP_LCD_ROUND != 0); }

int lab_get(int id)
{
    if (!s_ready || id < 0 || id >= PC_LAB_N) return id >= 0 && id < PC_LAB_N ? dflt(id) : 0;
    return s_v[id];
}

static bool crashed(void)
{
#if SIM
    const char *e = getenv("SIM_LAB_CRASH");      /* the simulator has no guard: this fakes a boot after a crash */
    return e && *e == '1';
#else
    return guard_last_reset_was_crash();
#endif
}

/* the knobs whose consumer is a board driver: pushed after a load or a set (each setter only stores; the driver reads it at its next use) */
static void push(int id)
{
#if !SIM && !BSP_LCD_ROUND
    switch (id) {
    case PC_LAB_TOUCH_X_NUM: bsp_lab_touch_x_num(s_v[id]); break;
    case PC_LAB_CO5300_C4: case PC_LAB_CO5300_58: bsp_lab_co5300(s_v[PC_LAB_CO5300_C4], s_v[PC_LAB_CO5300_58]); break;
    case PC_LAB_LCD_PSRAM_DMA: case PC_LAB_LCD_QUEUE: case PC_LAB_LCD_CHUNK_ROWS:
        bsp_lab_lcd(s_v[PC_LAB_LCD_PSRAM_DMA], s_v[PC_LAB_LCD_QUEUE], s_v[PC_LAB_LCD_CHUNK_ROWS]); break;
    case PC_LAB_DAC_PDN_PAIR: case PC_LAB_DAC_HP_INV: case PC_LAB_DAC_XTAL_IBIAS:
        bsp_lab_dac(s_v[PC_LAB_DAC_PDN_PAIR], s_v[PC_LAB_DAC_HP_INV], s_v[PC_LAB_DAC_XTAL_IBIAS]); break;
    default: break;
    }
#else
    (void)id;
#endif
}

static void read_all(void)
{
    for (int i = 0; i < PC_LAB_N; i++) {
        int v = cfg_int(pc_lab_def(i)->key, dflt(i));
        if (!pc_lab_ok(i, v)) v = dflt(i);          /* a hand-edited file: back to the default, never a value outside the range */
        s_v[i] = v;
    }
    s_ready = true;
    for (int i = 0; i < PC_LAB_N; i++) push(i);
}

void lab_load(void)
{
    if (crashed()) {      /* every load of a crash boot (a second cfg_load re-reads the file, where the keys are still) */
        int n = 0;
        for (int i = 0; i < PC_LAB_N; i++) if (cfg_has(pc_lab_def(i)->key)) { cfg_del(pc_lab_def(i)->key); n++; }
        if (n) printf("APP lab: the last reset was a crash, %d knob(s) back to their defaults\n", n);
    }
    read_all();
    if (s_logged) return;
    s_logged = true;
    char b[320];
    int o = 0;
    for (int i = 0; i < PC_LAB_N && o < (int)sizeof b - 48; i++)
        if (s_v[i] != dflt(i)) o += snprintf(b + o, sizeof b - (size_t)o, " %s=%d", pc_lab_def(i)->key + 4, s_v[i]);
    if (o) printf("APP lab active:%s\n", b);
}

static void list_one(int i)
{
    const pc_lab_def_t *d = pc_lab_def(i);
    printf("APP lab %s=%d default=%d range=%s%d..%d%s when=%s\n", d->key + 4, s_v[i], dflt(i), d->zero_ok ? "0," : "", d->min, d->max,
           d->step > 1 ? " (even)" : "", pc_lab_when_name(d->when));
}

void lab_cmd(const char *arg)
{
    while (*arg == ' ') arg++;
    if (!*arg) { for (int i = 0; i < PC_LAB_N; i++) list_one(i); printf("APP lab listed n=%d\n", PC_LAB_N); return; }
    if (!strcmp(arg, "reset")) {
        int n = 0;
        for (int i = 0; i < PC_LAB_N; i++) if (cfg_has(pc_lab_def(i)->key)) { cfg_del(pc_lab_def(i)->key); n++; }
        read_all();
        printf("APP lab reset n=%d (a knob that is read at boot or at the next track takes it then)\n", n);
        return;
    }
    char key[40];
    const char *sp = strchr(arg, ' ');
    size_t kl = sp ? (size_t)(sp - arg) : strlen(arg);
    if (kl >= sizeof key) kl = sizeof key - 1;
    memcpy(key, arg, kl); key[kl] = 0;
    const int i = pc_lab_find(key);
    if (i < 0) { printf("APP lab unknown key=%s\n", key); return; }
    while (sp && *sp == ' ') sp++;
    if (!sp || !*sp) { list_one(i); return; }
    int v;
    const pc_lab_def_t *d = pc_lab_def(i);
    if (!pc_lab_parse_int(sp, &v) || !pc_lab_ok(i, v)) {
        printf("APP lab refused %s=%s range=%s%d..%d%s\n", d->key + 4, sp, d->zero_ok ? "0," : "", d->min, d->max, d->step > 1 ? " (even)" : "");
        return;
    }
    if (v == dflt(i)) cfg_del(d->key); else cfg_set_int(d->key, v);
    s_v[i] = v;
    push(i);
    printf("APP lab set %s=%d (%s%s)\n", d->key + 4, v, d->when == PC_LAB_LIVE ? "live" : d->when == PC_LAB_NEXT ? "takes effect at the next track start" : "takes effect after a reboot",
           d->when == PC_LAB_LIVE ? "" : "; stored");
}
