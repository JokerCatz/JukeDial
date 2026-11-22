# aptx_bench

Feasibility (user 2026-11-10: implement and measure first, trade off only when measured): how much of one core libfreeaptx needs for aptX / aptX HD on the ESP32-S31, plus a decode round trip (the peer S31 can decode to verify a real link).

Run on the **peer** S31 only (`/dev/serial/by-id/usb-1a86_USB_Single_Serial_<your id>-if00`).

libfreeaptx is LGPL-2.1+ (see `components/libfreeaptx/COPYING`, `SOURCE`). Personal use is fine; a shipped product would have to allow relinking, and aptX is a Qualcomm trademark that needs a licence for products.

The clock is pinned in `sdkconfig.defaults` (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_320=y`, 2026-11-13; it was already the SDK default, so the 2026-11-10 numbers were 320 MHz too).
