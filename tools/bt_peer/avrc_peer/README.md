# avrc_peer: "JC Peer AVRC" (headset keys + absolute volume bench)

A copy of ESP-IDF v6.1 `examples/bluetooth/bluedroid/classic_bt/avrcp_absolute_volume` (2026-11-14), changed into a
pretend headset for the player's headphones mode (bt-remote): A2DP sink (SBC, audio discarded), AVRCP target with
absolute volume, AVRCP controller for passthrough keys. The example's 10 s volume-change simulation is gone: the peer
sends VOLUME_CHANGE (CHANGED) only when told to. Every line it prints starts with `PEER ` and ends with `t=<ms since boot>`.

Build and flash (the patched Bluedroid copy first):

    tools/bt_peer/avrc_peer/make_bt.sh            # .local/bt_avrc/bt (git-ignored, never commit it)
    cd tools/bt_peer/avrc_peer && idf.py --preview set-target esp32s31 build flash -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_<your id>-if00

Console (UART0 on the devkit's USB bridge, 115200; prompt `peer>`; open the port with DTR/RTS low:
`tools/boardport.py open_port(..., lines_low=True)`):

| Command | Does |
|---|---|
| `pt <play\|pause\|stop\|forward\|backward>` | passthrough press, 60 ms, release (controller) |
| `vol <0..127>` | sets the peer's volume and sends CHANGED(n) now (only while the player has VOLUME_CHANGE registered) |
| `echo <ms> [cmd]` | after each SetAbsoluteVolume answer (with `cmd`: after each command) send CHANGED with the value set, `<ms>` later; 0 off |
| `ansdelay <ms> [n]` | delay the SetAbsoluteVolume answer; `n` = only the next n answers (0 = all) |
| `ansgap <ms>` | an answer queued behind a delayed one goes this long after it (default 1) |
| `unordered` | toggle: answer at once even while a delayed answer is still out (default off: answers stay in order, as a headset's) |
| `ansk <k>` | answer request + k, clamped 0..127; 0 off |
| `ansfix <n>` | always answer n; no argument / -1 off |
| `bit7` | toggle: OR 0x80 into the answer and into CHANGED |
| `hold` | toggle: never answer SetAbsoluteVolume |
| `nocap` | toggle: do not offer VOLUME_CHANGE in the capabilities (applied at the next connection) |
| `reset` | every answer/echo option off (`nocap` kept) |
| `st` | connected, registered, peer volume, last set / answered, counts, options |

The answer options need `avrc_bench.patch` (applied by `make_bt.sh`). Stock Bluedroid answers SetAbsoluteVolume
itself, with the requested value, in `btc/profile/std/avrc/btc_avrc.c` (`btc_rc_upstreams_evt`), before the
application sees the command. The patch changes only that place (25 lines, one file):
- the answer goes out only if `btc_avrc_bench_answer(label, ctype, &volume)` returns true; it may change `volume`;
- a weak default of that hook returns true and leaves the value alone (the stock behaviour);
- new `btc_avrc_bench_abs_vol_rsp(label, ctype, volume)` sends an answer later (any task: it ends in `BTA_AvMetaRsp`).

`main/main.c` defines the strong hook (BTC task) and a small timer task for delayed answers and echoes.

Known: on this devkit, opening the port can reset the peer (CH34x DTR/RTS glitch at open, seen 5 times in 9 opens on
2026-11-14). The player reconnects by itself (bt_hp remembered) in about 2–3 s; options are lost (set them again).
