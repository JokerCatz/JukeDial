// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Flash / NVS writes (nvs_set_*, nvs_commit, nvs_erase_*, esp_partition_write/erase, esp_flash_write/erase) freeze the cache; a task whose stack is
 * in PSRAM (the LVGL task) asserts there (cache_utils.c, seen on the peer 2026-11-15). Such code goes through flash_safe_call: fn(arg) runs on a
 * small task with an internal-RAM stack and the caller waits up to timeout_ms. Returns true when fn finished; false = it was not done in time
 * (it may still run later and finish: arg must then stay valid, so pass a value or a static/heap object, never the caller's stack). Allowed
 * direct writers are listed in tools/check/check_flash_writes.py. The simulator runs fn inline. */
bool flash_safe_call(void (*fn)(void *), void *arg, unsigned timeout_ms);
#ifdef __cplusplus
}
#endif
