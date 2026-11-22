#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""MIDI clock over BLE MIDI to / from the instrument app ("JukeDial MIDI"), on the probe twin characteristic
(PipeWire on the test PC holds the standard one). Needs bleak.
usage:
  inst_clock_probe.py send BPM SECONDS [--no-start] [--bpm2 B --at S]
      0xFA, then 24 0xF8 a beat with real 13-bit timestamps, then 0xFC. --bpm2/--at: change tempo at S seconds.
      Board side: `inst clock ext`, then `inst clkstat` / `inst metrostat`.
  inst_clock_probe.py listen SECONDS
      Receive the board's clock out (`inst clock int` + `inst metro on`); prints the tempo from the 0xF8
      timestamps and from their arrival times, and the spread of the tick spacing."""
import asyncio, statistics, sys, time
if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect (and without bleak)
    print(__doc__); sys.exit(0)
from bleak import BleakClient, BleakScanner

PRB = "7872e5db-3868-4112-a1a9-f2669d106bf3"
T0 = time.monotonic()


def ms():
    return int((time.monotonic() - T0) * 1000)


def pkt(t, byte):
    t &= 0x1FFF
    return bytes([0x80 | (t >> 7), 0x80 | (t & 0x7F), byte])


async def send(c, bpm, secs, start, bpm2, at):
    if start:
        await c.write_gatt_char(PRB, pkt(ms(), 0xFA), response=False)
    t_start = time.monotonic()
    nxt, n = t_start, 0
    while time.monotonic() - t_start < secs:
        b = bpm2 if bpm2 and time.monotonic() - t_start >= at else bpm
        await asyncio.sleep(max(0, nxt - time.monotonic()))
        await c.write_gatt_char(PRB, pkt(int((nxt - T0) * 1000), 0xF8), response=False)
        n += 1
        nxt += 60.0 / (b * 24)
    await c.write_gatt_char(PRB, pkt(ms(), 0xFC), response=False)
    print(f"CLOCK sent ticks={n} bpm={bpm}" + (f" then {bpm2} at {at}s" if bpm2 else ""))


async def listen(c, secs):
    ticks, other = [], []

    def on(_, d):
        now = time.monotonic()
        if len(d) < 3:
            return
        hi, i = d[0] & 0x3F, 1
        last = -1
        while i + 1 < len(d) + 1 and i < len(d):
            if d[i] & 0x80 and i + 1 < len(d) and d[i + 1] & 0x80:
                lo = d[i] & 0x7F
                if last >= 0 and lo < last:
                    hi = (hi + 1) & 0x3F
                last = lo
                ts, b = (hi << 7) | lo, d[i + 1]
                if b == 0xF8:
                    ticks.append((now, ts))
                elif b in (0xFA, 0xFB, 0xFC):
                    other.append((now, b))
                i += 2
            else:
                i += 1

    await c.start_notify(PRB, on)
    await asyncio.sleep(secs)
    await c.stop_notify(PRB)
    print(f"CLOCK received ticks={len(ticks)} transport={[hex(b) for _, b in other]}")
    if len(ticks) < 50:
        return
    # timestamps (ms, 13-bit wrap) and arrivals
    ts = [ticks[0][1]]
    for _, t in ticks[1:]:
        d = (t - ts[-1] % 8192) % 8192
        ts.append(ts[-1] + d)
    dts = [b - a for a, b in zip(ts, ts[1:])]
    arr = [(b[0] - a[0]) * 1000 for a, b in zip(ticks, ticks[1:])]
    span_ts = (ts[-1] - ts[0]) / (len(ts) - 1)
    span_arr = (ticks[-1][0] - ticks[0][0]) * 1000 / (len(ticks) - 1)
    q = lambda v, p: sorted(v)[min(len(v) - 1, int(p * len(v)))]
    print(f"CLOCK by timestamp: bpm={60000 / (24 * span_ts):.2f} tick={span_ts:.3f}ms spacing p5={q(dts, .05)} p95={q(dts, .95)} ms (1 ms steps)")
    print(f"CLOCK by arrival:   bpm={60000 / (24 * span_arr):.2f} spacing stdev={statistics.pstdev(arr):.2f}ms p5={q(arr, .05):.1f} p95={q(arr, .95):.1f} ms")


async def main():
    mode = sys.argv[1]
    dev = await BleakScanner.find_device_by_name("JukeDial MIDI", timeout=20)
    if not dev:
        print("CLOCK device not found")
        return
    async with BleakClient(dev) as c:
        print("CLOCK connected")
        await asyncio.sleep(2.0)
        if mode == "send":
            a = sys.argv
            bpm2 = float(a[a.index("--bpm2") + 1]) if "--bpm2" in a else 0
            at = float(a[a.index("--at") + 1]) if "--at" in a else 0
            await send(c, float(a[2]), float(a[3]), "--no-start" not in a, bpm2, at)
        else:
            await listen(c, float(sys.argv[2]))
        await asyncio.sleep(0.5)


if __name__ == "__main__":
    asyncio.run(main())
