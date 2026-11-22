// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Control protocol for the local settings page over Web Serial (spec 2026-11-09-web-design.md, rev. 2026-11-10).
 * A console line starting with '{' is a JSON request {"id":n,"op":"...",...}; the answer is one line
 * "CTL {json}" (the page picks these out of the log). LVGL context. */
void ctl_run(void *line);   /* takes ownership of the strdup'd line */
const char *ctl_codecs(void);   /* "ldac,aptxhd,aptx,aac,sbc" (vendor-codec image, the default) or "aac,sbc" (make build-stock) */
