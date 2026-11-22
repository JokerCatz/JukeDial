// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Card guide (cardsafe): every failed-mount reason of the SD layer has a plain "what to do" line on the 記憶卡 page.
 * The key is what the console prints (APP sd mount failed reason=... guide=...); the page picks its text by the same key. */
typedef enum { PC_SDE_SILENT, PC_SDE_FSFAIL, PC_SDE_INITFAIL, PC_SDE_OTHER } pc_sde_class_t;
/* what a failed mount means: SILENT = nothing answered (timeout / not found: no card); FSFAIL = the card initialised, the file system did not
 * mount (fs_word "gpt" or "nofs"); INITFAIL = the card answered and its init failed (the unclean-hand-back case: "stuck"); OTHER = "err" */
const char *pc_sdrec_reason(int cls, const char *fs_word);
/* the guide key of a reason (NULL = mounted): "insert" (nocard), "format" (gpt, nofs: the format offer), "repower" (stuck), "reinsert" (err and unknown) */
const char *pc_sd_guide_key(const char *reason);
/* the settings list's first row (card unusable + its reason, opening the card page with the guide): any failed mount while the card is not mounted
 * and the USB drive mode is not on (reason NULL = still mounting or a good mount: no row) */
bool pc_sd_guide_row(const char *reason, bool mounted, bool usb_mode);

/* USB drive mode link state for the status line (usb_mode_link; the board and sim/fake_usb_mode.c share the rule). released: the card was released
 * because the computer went away without an eject (any loss counts at once, no debounce). */
typedef enum { PC_USB_LINK_WAIT, PC_USB_LINK_IN_USE, PC_USB_LINK_EJECTED, PC_USB_LINK_LOST, PC_USB_LINK_SLEEP } pc_usb_link_t;
/* slept: the release was a clean bus suspend (the computer went to sleep with no I/O in flight): a message of its own, not a cable fault */
int pc_usb_link(bool mounted, bool was_mounted, bool ejected, bool released, bool slept);
/* Why the card should be released now (NULL = not): "lost" = the device was unconfigured (cable, VBUS, bus reset), or the bus was suspended for 2 s with a READ / WRITE command open or
 * writes still waiting for the card (a fault: data may be missing); "suspend" = the bus was suspended for 2 s with no I/O in flight (a sleeping computer: harmless, the card is released and
 * the user taps Reconnect). Never after an eject, before the first mount, or when already released. io_at_suspend = the I/O state at the first moment of the suspend, io_now = at the check. */
const char *pc_usb_release_why(bool mounted, bool was_mounted, bool ejected, bool released, bool suspended_2s, bool io_at_suspend, bool io_now);
/* the experimental gather size of the drive's writes, in KB (cfg usb_wbuf_kb): 0 off (default), 16 or 24 (two buffers of that size in internal RAM; 32 would leave too little) */
int pc_usb_wbuf_norm(int kb);
#ifdef __cplusplus
}
#endif
