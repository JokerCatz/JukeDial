// SPDX-License-Identifier: LGPL-2.1-or-later
/* Minimal esp_err.h for the host build (extended when a firmware file needs more). */
#pragma once
typedef int esp_err_t;
#define ESP_OK                 0
#define ESP_FAIL              -1
#define ESP_ERR_NO_MEM         0x101
#define ESP_ERR_INVALID_ARG    0x102
#define ESP_ERR_INVALID_STATE  0x103
#define ESP_ERR_INVALID_SIZE   0x104
#define ESP_ERR_NOT_FOUND      0x105
#define ESP_ERR_TIMEOUT        0x107
const char *esp_err_to_name(esp_err_t e);   /* shim_esp.c */
