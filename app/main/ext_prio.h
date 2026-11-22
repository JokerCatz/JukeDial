// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* 外接模式 T4: task priorities of the external mode (spec 2026-11-15-external-mode.md section 12: sound first, then the keys, then the drive; sound must never break up,
 * the drive may slow down). The TinyUSB task runs the class callbacks including the MSC card I/O, so it is the drive's level; the keys' sender runs above it;
 * T5's UAC pump (ring fill + feedback) and the audio task go above both. The idle / LVGL / BLE tasks keep their own numbers. */
#define EXT_PRIO_USB   5      /* tinyusb task (MSC card reads and writes, class requests) */
#define EXT_PRIO_HID   6      /* usb_keys: the HID / BLE sender */
#define EXT_PRIO_UAC   7      /* T5: uac_pump, above the keys; audio_svc's task is above that (its own priority) */
