# lc3_bench

LC3 encode cost on the ESP32-S31 (2026-11-13), the player's Auracast path: `esp_audio_codec` 2.6.2 LC3 (same version as
the player's lock file), one mono encoder per channel, 10 ms frames, 48 kHz at 120 B (48_4, 96 kbps/ch) and 155 B (48_6,
124 kbps/ch), plus 24 kHz 60 B (24_2) for reference. Runs pinned to core 1, then core 0 (the player log reports its cast task on core 0). Signals are
synthesised (sine, music-like, silence; same generator as `tools/bench/ldac_bench`). Peer board only:

    idf.py --preview -B buildqio -DSDKCONFIG=buildqio/sdkconfig "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.qio" set-target esp32s31 && idf.py -B buildqio -DSDKCONFIG=buildqio/sdkconfig build && idf.py -B buildqio -DSDKCONFIG=buildqio/sdkconfig -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_<your id>-if00 flash monitor

Use the QIO build (`sdkconfig.qio`, as the player): with DIO flash LC3 costs about 25 % more (its code does not fit the
cache). Pinning to core 0 does not keep it there: the encoder ends up on core 1 (PIE), the printed core id says so.
Task watchdog off (long busy loop).
