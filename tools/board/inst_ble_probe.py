#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Play notes into the instrument app over BLE MIDI ("JukeDial MIDI") and time the round trip.

Note-on / note-off pairs every INTERVAL ms with real 13-bit BLE MIDI timestamps, write-without-response, on the
probe twin characteristic (the board echoes each write as a notification; PipeWire on the test PC claims the
standard MIDI service). Ends with CC 123 (all notes off). Board side: console `inst jitter` / `inst stat`.
Needs bleak (pip install bleak).
usage: inst_ble_probe.py [count] [interval_ms] [--std] [--chord]
  --std    the standard MIDI characteristic (no echo; works where nothing else holds the service)
  --chord  three notes per packet (running status), to check several messages in one packet
"""
import asyncio, statistics, sys, time
if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect (and without bleak)
    print(__doc__); sys.exit(0)
from bleak import BleakClient, BleakScanner

STD = "7772e5db-3868-4112-a1a9-f2669d106bf3"
PRB = "7872e5db-3868-4112-a1a9-f2669d106bf3"
T0 = time.monotonic()


def ms():
    return int((time.monotonic() - T0) * 1000)


def pkt(msgs):
    t = ms() & 0x1FFF
    out = bytearray([0x80 | (t >> 7)])
    for m in msgs:
        out.append(0x80 | (t & 0x7F))
        out += bytes(m)
    return bytes(out)


async def main(count, interval, use, chord):
    dev = await BleakScanner.find_device_by_name("JukeDial MIDI", timeout=20)
    if not dev:
        print("PROBE device not found")
        return
    sent, rtt = {}, []

    def on_echo(_, data):
        now = time.monotonic()
        if len(data) >= 5 and (data[2] & 0xF0) == 0x90:
            key = (data[3], data[4])
            if key in sent:
                rtt.append((now - sent.pop(key)) * 1000)

    async with BleakClient(dev) as c:
        print("PROBE connected")
        if use == PRB:
            await c.start_notify(use, on_echo)
        await asyncio.sleep(2.0)                          # connection-parameter update settles
        nxt = time.monotonic()
        for i in range(count):
            note, vel = 48 + i % 24, 40 + (i * 7) % 80
            sent[(note, vel)] = time.monotonic()
            msgs = [[0x90, note, vel]]
            if chord:
                msgs = [[0x90, note, vel], [0x90, note + 4, vel], [0x90, note + 7, vel]]
            await c.write_gatt_char(use, pkt(msgs), response=False)
            nxt += interval / 2000
            await asyncio.sleep(max(0, nxt - time.monotonic()))
            offs = [[0x80, note, 0]] + ([[0x80, note + 4, 0], [0x80, note + 7, 0]] if chord else [])
            await c.write_gatt_char(use, pkt(offs), response=False)
            nxt += interval / 2000
            await asyncio.sleep(max(0, nxt - time.monotonic()))
        await c.write_gatt_char(use, pkt([[0xB0, 123, 0]]), response=False)
        await asyncio.sleep(0.5)
    rtt.sort()
    if rtt:
        q = lambda p: rtt[min(len(rtt) - 1, int(p * len(rtt)))]
        print(f"PROBE sent={count} interval={interval}ms echoed={len(rtt)} rtt_ms min={rtt[0]:.1f} p50={q(.5):.1f} "
              f"p95={q(.95):.1f} p99={q(.99):.1f} max={rtt[-1]:.1f} mean={statistics.mean(rtt):.1f}")
    else:
        print(f"PROBE sent={count} interval={interval}ms (no echo on this characteristic)")


if __name__ == "__main__":
    pos = [a for a in sys.argv[1:] if not a.startswith("--")]
    asyncio.run(main(int(pos[0]) if pos else 500, float(pos[1]) if len(pos) > 1 else 20,
                     STD if "--std" in sys.argv else PRB, "--chord" in sys.argv))
