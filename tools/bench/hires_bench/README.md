# hires_bench

Peer-board bench (2026-11-13, hi-res spec §11 items B0 and E0). It changes nothing in the player. It measures:

- **clk**: which I2S TX clocks the ESP32-S31 makes with XTAL and with APLL, from 44.1 kHz to 384 kHz. Slots are 32-bit stereo Philips, on I2S1 (the player's DAC port).
  - Return codes, divider registers read back, and WS/BCK counted by PCNT.
  - An APLL switch test (44.1k ↔ 192k ×10) and a shared-APLL test.
  - Internal RAM per DMA shape, and a 20 s zero-block write test at 192k and 384k.
- **flac**: dr_flac decode cost of host-encoded 4 s fixtures (44.1k/16, 96k/24, 192k/24, 384k/24) at ffmpeg FLAC level 5 and level 8.
  - Each file is timed decode-only, then with the player's FIR decimator at dec 2 (`dec2`) and at today's dec to ≤ 48 kHz (`today`).
- **dsp**: cycle cost per 10 ms block of the player's `pc_dec2_run` (192k and 96k in), `pc_int2_run` (48k in) and `pc_rsq_process` (44.1 → 48, 48 → 24), plus the `pc_rsq_init` time: `HB dsp <name> pct=<%> us_per_block=<min>/<avg>/<max>`. The sources are taken by path (`main/CMakeLists.txt`).

## Pins (bare devkit, nothing wired)
BCK goes out on GPIO53, WS on GPIO51 and DOUT on GPIO52. These are the player's own I2S pads (`components/jc3636f118_bsp/include/bsp/bsp_pins.h:38-40`).

They are safe on the devkit because none of them is:
- a strapping pin: those are 36-40, 60 and 61 (`IDF/components/soc/esp32s31/register/soc/io_mux_reg.h:349-498`, "Strapping" comments);
- a flash pin: 26-32 (`io_mux_reg.h:304-329`);
- a USB pin: 33 and 34 (`io_mux_reg.h:142-143`);
- a UART0 pin: 58 and 59 (`soc/uart_pins.h:10-11`).

The same pad is input-enabled and routed through the GPIO matrix to two PCNT units, which count rising edges with no glitch filter. MCLK is not routed out; its value comes from the divider registers.

## Build, flash, run (peer port only)
    . $HOME/.espressif/tools/activate_idf_v6.1.sh
    D=tools/bench/hires_bench; P=/dev/serial/by-id/usb-1a86_USB_Single_Serial_<your id>-if00
    idf.py --preview -C $D -B $D/build -DSDKCONFIG=$D/build/sdkconfig set-target esp32s31
    idf.py --preview -C $D -B $D/build -DSDKCONFIG=$D/build/sdkconfig -p $P -b 921600 flash
    $HOME/.espressif/tools/python/v6.1/venv/bin/python $D/capture.py $P run_l5.txt 900
    # level 8 fixtures: separate build dir, same app
    idf.py --preview -C $D -B $D/build-l8 -DSDKCONFIG=$D/build-l8/sdkconfig -DHB_LEVEL=8 set-target esp32s31
    idf.py --preview -C $D -B $D/build-l8 -DSDKCONFIG=$D/build-l8/sdkconfig -DHB_LEVEL=8 -p $P -b 921600 flash

About the build and the fixtures:
- `gen_fixtures.py` builds the fixture blob with ffmpeg at build time. The output lands in the build dir, which git ignores. The blob is flashed into the `fix` partition.
- Flashing ~11 MB with the ROM loader takes about 4 min. Esptool has no stub for the S31.
- The build uses QIO flash, 320 MHz and a player-like PSRAM heap (malloc > 1 KB goes to PSRAM).

## Timebase and limits
- **Timebase.** Everything is timed with esp_timer, which runs from the same 40 MHz crystal as XTAL and APLL. The ppm figures are therefore divider and PLL error relative to the crystal; they do not include the crystal's own error.
- **WS window.** WS is counted over 2 s, so one count is ±5.7 ppm at 44.1 kHz and ±1.3 ppm at 384 kHz.
- **BCK.** BCK uses the same window. `bck_ws` (BCK/WS) checks whether PCNT kept up: it must read 64.
- **FLAC fixtures.** They are synthetic (partials plus pink noise). They are encoded by ffmpeg's FLAC encoder, not libFLAC, with a fixed block size of 4096 (plus one 192k file at 16384).
- **Not covered.** DAC lock and listening are not part of this bench.


