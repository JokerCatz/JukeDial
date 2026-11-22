// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdbool.h>
#include <stdint.h>
#include "esp_timer.h"
#include "soc/usb_serial_jtag_struct.h"

/* USB-Serial-JTAG "is a host there?" (an internal note).
 *
 * IDF's answer (usb_serial_jtag_connection_monitor.c) comes from a core-0 tick hook that reads and clears the SOF
 * interrupt bit each tick and calls the port unplugged after a tolerance of 3 ticks without a SOF - but the tolerance
 * is only refilled on a reconnect, so every 4th tick that saw no SOF over the whole uptime (two ticks inside one 1 ms
 * USB frame: a late tick interrupt caught up in a burst, or the two 1 kHz clocks drifting) flips it to "unplugged" for
 * one tick. Meanwhile the VFS write() returns EIO and printf drops the whole line: the lost "APP put ok" / CTL replies
 * (measured: every lost reply had printf -> EIO, the SOF frame counter kept counting through each "unplug").
 *
 * Linked over the IDF function (-Wl,--wrap, app/main/CMakeLists.txt): connected while the SOF frame index keeps
 * changing; unplugged after 20 ms (20 frames) without a new one, so with no host the console still drops output
 * instead of blocking, as before. Lock-free: racing callers at worst refresh the timestamp twice. */

#define USJ_NO_SOF_US 20000      /* no new SOF frame index for this long: no host */
#define USJ_POLL_US   1500       /* a frame is 1 ms: polled once after a quiet spell (the 11-bit index wraps every 2.048 s) */
#define USJ_DOWN_US   100000     /* while unplugged, poll again at most this often (printf must not stall without a host) */

bool __real_usb_serial_jtag_is_connected(void);

bool __wrap_usb_serial_jtag_is_connected(void)
{
    static volatile uint32_t last_frame = 0xFFFFFFFFu, last_us, down_us;
    static volatile bool down;
    uint32_t fr = USB_SERIAL_JTAG.fram_num.sof_frame_index, now = (uint32_t)esp_timer_get_time();
    if (fr == last_frame && now - last_us >= USJ_NO_SOF_US) {
        if (down && now - down_us < USJ_DOWN_US) return false;
        uint32_t t0 = now;                         /* same index after a quiet spell: a wrap, or really no host */
        while (fr == last_frame && now - t0 < USJ_POLL_US) {
            fr = USB_SERIAL_JTAG.fram_num.sof_frame_index;
            now = (uint32_t)esp_timer_get_time();
        }
        if (fr == last_frame) { down = true; down_us = now; return false; }
    }
    if (fr != last_frame) { last_frame = fr; last_us = now; down = false; }
    return true;
}
