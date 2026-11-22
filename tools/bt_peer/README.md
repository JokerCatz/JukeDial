# Bluetooth test peer (second ESP32-S31 devkit)

Copies of ESP-IDF v6.1 examples, flashed to the peer board
(`/dev/serial/by-id/usb-1a86_USB_Single_Serial_<your id>-if00`) to test the player's Bluetooth modes.
Each one appends its settings to `sdkconfig.defaults`. Build: `idf.py --preview set-target esp32s31 build`.

| Folder | Role | Tests player mode |
|---|---|---|
| `a2dp_sink` | "JC Peer", SBC sink | headphones (SBC) |
| `a2dp_sink_aac` | "JC Peer AAC", AAC+SBC sink | headphones (AAC) |
| `a2dp_sink_ldac` | "JC Peer LDAC": vendor-codec sink, LDAC + aptX HD + aptX (44.1/48, stereo) + SBC; `idf.py -DPEER_CODECS=N` picks the vendor codecs (1 LDAC, 2 aptX HD, 4 aptX; default 7). LDAC is not decoded but every packet is checked (sync byte 0xAA per frame, rate/channel ids, frame sizes, frame count = media header; bad frames counted). aptX / aptX HD are decoded (libfreeaptx, core 1) and the left channel is checked as a test tone (frequency, level, SNR; player console `tone 1000`). Prints codec, packets/s, kbps, timestamp gaps every 5 s. Needs `tools/bt_ldac/make_bt.sh` | headphones (`make build-ldac`: LDAC / aptX HD / aptX) |
| `le_unicast_sink` | "JC Peer LEA": LE Audio unicast server (BAP, PACS/ASCS, CIS peripheral), LC3 48 kHz 1-2 ch decoded on core 1; checks the test tone of both channels. Prints `PEER lea stats` (SDUs/s, kbps, lost, invalid, tone) about every 13 s | an LE Audio unicast client (not part of this tree) (LE Audio unicast client spike, not in the app) |
| `a2dp_source` | SBC source, connects to "JukeDial" | speaker |
| `broadcast_sink` | Auracast receiver for "JukeDial" (16/24/48 kHz, 1–2 ch); prints per BIS every 10 s: rx (SDUs delivered, cumulative), valid, lost (ISO "lost" flag), err, len, and `maxgap_ms` = largest gap between two receive callbacks in that window (2026-11-13) | broadcast |
| `broadcast_source_lc3` | Auracast source "JC Peer Cast": real LC3 sine (48 kHz, 10 ms, 100 B, unencrypted) to test listening mode | listen |
| `avrc_peer` | "JC Peer AVRC": SBC sink + AVRCP target with absolute volume + controller for keys, a pretend headset with a UART console (`pt`, `vol`, `echo`, answer options `ansdelay`/`ansk`/`ansfix`/`bit7`/`hold`, `nocap`, `st`). The answer options need its own patched Bluedroid (`avrc_peer/make_bt.sh`, one hook in `btc_avrc.c`). | headphones (headset keys, absolute volume) |
| `a2dp_source_aac` | AAC source (external codec) | speaker — does not work: the example's own source caps come out empty (`Can't parse src cap ret = 13`), a peer-side problem |

Known quirk of `a2dp_sink_ldac` (2026-11-10): after several codec changes in one boot (LDAC -> aptX HD -> aptX
configurations, as the player's codec-fallback test does), a following SBC stream arrives at only about a third of
its rate (L2CAP congested). Resetting the peer fixes it; the player is not involved. Other codec sets build in their
own directory, e.g. `idf.py --preview -B build-c2 -DPEER_CODECS=2 build` (aptX HD + SBC), `-B build-c0
-DPEER_CODECS=0` (SBC only).

Debug traces: build with an extra defaults file that sets `CONFIG_BT_LOG_APPL_TRACE_LEVEL_DEBUG`,
`CONFIG_BT_LOG_BTC_TRACE_LEVEL_DEBUG`, `CONFIG_LOG_DEFAULT_LEVEL_DEBUG`, `CONFIG_LOG_MAXIMUM_LEVEL_DEBUG`.
On the player, `loglvl BT_AVDT 4` (debug builds) turns on AVDTP traces.
