// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_sdguide.h"
#include "pc/pc_fmtguard.h"
#include <string.h>
#include "pc_test.h"

static void test_reason_words(void)
{
    CHECK(!strcmp(pc_sdrec_reason(PC_SDE_SILENT, NULL), "nocard"));
    CHECK(!strcmp(pc_sdrec_reason(PC_SDE_FSFAIL, "gpt"), "gpt"));
    CHECK(!strcmp(pc_sdrec_reason(PC_SDE_FSFAIL, "nofs"), "nofs"));
    CHECK(!strcmp(pc_sdrec_reason(PC_SDE_FSFAIL, NULL), "nofs"));
    CHECK(!strcmp(pc_sdrec_reason(PC_SDE_INITFAIL, NULL), "stuck"));   /* the card answered: never "nocard" */
    CHECK(!strcmp(pc_sdrec_reason(PC_SDE_OTHER, NULL), "err"));
}

static void test_guide_keys(void)
{
    CHECK(pc_sd_guide_key(NULL) == NULL);
    CHECK(!strcmp(pc_sd_guide_key("nocard"), "insert"));
    CHECK(!strcmp(pc_sd_guide_key("gpt"), "format"));
    CHECK(!strcmp(pc_sd_guide_key("nofs"), "format"));
    CHECK(!strcmp(pc_sd_guide_key("stuck"), "repower"));
    CHECK(!strcmp(pc_sd_guide_key("err"), "reinsert"));
    CHECK(!strcmp(pc_sd_guide_key("whatever"), "reinsert"));
}

static void test_stuck_never_formats(void)
{
    CHECK(!pc_fmt_entry_allowed("stuck", false, false));               /* the card is not broken: no format offer */
    CHECK(pc_fmt_entry_allowed("nofs", false, false));
    pc_fmtguard_t g;
    CHECK(!pc_fmtguard_begin(&g, "stuck", false, false));
}

static void test_guide_row(void)
{
    CHECK(pc_sd_guide_row("stuck", false, false));
    CHECK(pc_sd_guide_row("nocard", false, false));
    CHECK(pc_sd_guide_row("err", false, false));
    CHECK(pc_sd_guide_row("nofs", false, false));
    CHECK(!pc_sd_guide_row(NULL, false, false));        /* still mounting / a good mount */
    CHECK(!pc_sd_guide_row("stuck", true, false));
    CHECK(!pc_sd_guide_row("stuck", false, true));      /* the card is the computer's */
}

static void test_usb_link(void)
{
    CHECK_EQ_INT(pc_usb_link(false, false, false, false, false), PC_USB_LINK_WAIT);
    CHECK_EQ_INT(pc_usb_link(true, true, false, false, false), PC_USB_LINK_IN_USE);
    CHECK_EQ_INT(pc_usb_link(true, true, true, false, false), PC_USB_LINK_EJECTED);
    CHECK_EQ_INT(pc_usb_link(false, true, true, false, false), PC_USB_LINK_EJECTED);    /* ejected, then the plug came out: safe */
    CHECK_EQ_INT(pc_usb_link(false, true, false, false, false), PC_USB_LINK_LOST);      /* dropped without an eject */
    CHECK_EQ_INT(pc_usb_link(true, true, false, true, false), PC_USB_LINK_LOST);        /* released stays lost even when the host comes back */
    CHECK_EQ_INT(pc_usb_link(true, false, true, true, false), PC_USB_LINK_LOST);
    CHECK_EQ_INT(pc_usb_link(true, true, false, true, true), PC_USB_LINK_SLEEP);   /* released by a clean suspend: its own state, not a cable fault */
    CHECK_EQ_INT(pc_usb_link(false, true, false, true, true), PC_USB_LINK_SLEEP);
    CHECK_EQ_INT(pc_usb_link(true, true, false, false, true), PC_USB_LINK_IN_USE);  /* slept without a release is nothing */
}

static void test_release_why(void)
{
    /* (mounted, was, ejected, released, susp2s, io_at, io_now) */
    CHECK(pc_usb_release_why(true, true, false, false, false, false, false) == NULL);        /* healthy */
    CHECK_STR(pc_usb_release_why(false, true, false, false, false, false, false), "lost");   /* unconfigured */
    CHECK_STR(pc_usb_release_why(false, true, false, false, true, false, false), "lost");    /* unconfigured wins over a suspend */
    CHECK_STR(pc_usb_release_why(true, true, false, false, true, false, false), "suspend");  /* quiet bus, no I/O: a sleeping computer */
    CHECK_STR(pc_usb_release_why(true, true, false, false, true, true, false), "lost");      /* I/O in flight when it went quiet */
    CHECK_STR(pc_usb_release_why(true, true, false, false, true, false, true), "lost");      /* I/O in flight at the check */
    CHECK(pc_usb_release_why(true, true, true, false, true, false, false) == NULL);          /* ejected first: clean, left alone */
    CHECK(pc_usb_release_why(false, true, true, false, false, false, false) == NULL);
    CHECK(pc_usb_release_why(false, false, false, false, false, false, false) == NULL);      /* never mounted */
    CHECK(pc_usb_release_why(true, true, false, true, true, false, false) == NULL);          /* already released */
    CHECK_EQ_INT(pc_usb_wbuf_norm(0), 0); CHECK_EQ_INT(pc_usb_wbuf_norm(16), 16); CHECK_EQ_INT(pc_usb_wbuf_norm(24), 24);
    CHECK_EQ_INT(pc_usb_wbuf_norm(32), 0); CHECK_EQ_INT(pc_usb_wbuf_norm(-16), 0); CHECK_EQ_INT(pc_usb_wbuf_norm(8), 0);
}

int main(void)
{
    RUN(test_reason_words);
    RUN(test_guide_keys);
    RUN(test_stuck_never_formats);
    RUN(test_guide_row);
    RUN(test_usb_link);
    RUN(test_release_why);
    return 0;
}
