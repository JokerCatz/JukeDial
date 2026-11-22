# LDAC encoder cost on the ESP32-S31

Feasibility only (user request 2026-11-10): how much of one core the AOSP libldac encoder needs on this chip.
Runs on the spare S31 board (`/dev/serial/by-id/usb-1a86_USB_Single_Serial_<your id>-if00`).

    idf.py --preview -B build320 -DSDKCONFIG=build320/sdkconfig set-target esp32s31 && idf.py -B build320 -DSDKCONFIG=build320/sdkconfig build
    idf.py -B build320 -DSDKCONFIG=build320/sdkconfig -p <peer port> flash monitor

The clock is pinned in `sdkconfig.defaults` (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_320=y`, added 2026-11-13; it was already the
SDK default, so the 2026-11-10 numbers were 320 MHz too). Variants, each in its own build folder with its own sdkconfig:
`-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.240"` (240 MHz), `"sdkconfig.defaults;sdkconfig.psram"` (PSRAM on,
blocks > 1 KB from PSRAM like the player), `"sdkconfig.defaults;sdkconfig.qio"` (QIO flash like the player; default DIO), `-DLDAC_FLOAT=1` (floating-point libldac), `-DLDAC_PROF=1` (`prof/libldac`:
the same libldac sources with cycle timers around the encoder stages; the shared component is untouched and the printed
checksum must match the normal build). Signals (no TF on the peer, all synthesised): the 2026-11-10 1 kHz sine, a
music-like stereo mix, digital silence — see the comment at the top of `main/main.c`. Each sweep runs on core 0 and on core 1.

The library is `components/libldac` (shared with the player). It builds fixed point; `-DLDAC_FLOAT=1` at configure time builds the floating-point variant. LDAC in a product needs Sony's certification (NOTICE).

Read `enc` (time inside `ldacBT_encode`), not `loop`: `loop` is the 2026-11-10 number and includes generating the sine
with double-precision `sin()`, which is software on this chip (about 44 % of a core at 44.1 kHz). The task watchdog is
off (`CONFIG_ESP_TASK_WDT_INIT=n`): the bench task runs for minutes and the watchdog prints disturbed the timings.

