// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "esp_err.h"
#include <sys/time.h>
typedef struct { const char *server; void (*sync_cb)(struct timeval *tv); } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(s) ((esp_sntp_config_t){ .server = (s) })
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c);   /* shim_esp.c */
