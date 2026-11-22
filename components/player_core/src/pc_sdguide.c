// SPDX-License-Identifier: LGPL-2.1-or-later
/* Card guide keys and the USB link state (pc_sdguide.h). */
#include "pc/pc_sdguide.h"
#include <stddef.h>
#include <string.h>

const char *pc_sdrec_reason(int cls, const char *fs_word)
{
    switch (cls) {
    case PC_SDE_SILENT: return "nocard";
    case PC_SDE_FSFAIL: return fs_word && !strcmp(fs_word, "gpt") ? "gpt" : "nofs";
    case PC_SDE_INITFAIL: return "stuck";
    default: return "err";
    }
}

const char *pc_sd_guide_key(const char *r)
{
    if (!r) return NULL;
    if (!strcmp(r, "nocard")) return "insert";
    if (!strcmp(r, "gpt") || !strcmp(r, "nofs")) return "format";
    if (!strcmp(r, "stuck")) return "repower";
    return "reinsert";
}

bool pc_sd_guide_row(const char *reason, bool mounted, bool usb_mode) { return reason && !mounted && !usb_mode; }

int pc_usb_link(bool mounted, bool was_mounted, bool ejected, bool released, bool slept)
{
    if (released) return slept ? PC_USB_LINK_SLEEP : PC_USB_LINK_LOST;
    if (mounted) return ejected ? PC_USB_LINK_EJECTED : PC_USB_LINK_IN_USE;
    if (!was_mounted) return PC_USB_LINK_WAIT;
    return ejected ? PC_USB_LINK_EJECTED : PC_USB_LINK_LOST;
}

const char *pc_usb_release_why(bool mounted, bool was_mounted, bool ejected, bool released, bool suspended_2s, bool io_at_suspend, bool io_now)
{
    if (!was_mounted || ejected || released) return NULL;
    if (!mounted) return "lost";
    if (!suspended_2s) return NULL;
    return io_at_suspend || io_now ? "lost" : "suspend";
}

int pc_usb_wbuf_norm(int kb) { return kb == 16 || kb == 24 ? kb : 0; }
