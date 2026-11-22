#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""cfg_keep: a shot or measure tool puts back the settings its steps move (board batch A F6), on the board and in the simulator.

  saved = cfg_keep.read(send, keys)      before the first step: `cfg <key>` per key ("APP cfg <key>=<value>"; an empty value = not set)
  cfg_keep.restore(send, saved)          at the end and on error: `cfgdel <key>` for a key that was not set, else `cfgset <key> <value>`
                                         (a key in LIVE also gets its console command first, so the running value follows, not only the stored one)
send(command) -> the console's answer as bytes, str or a list of lines. Standard library only."""
import re

ANS = re.compile(r"^APP cfg (\S+?)=(.*)$", re.M)
LIVE = {"volume": "vol {v}"}          # stored and live: the audio volume


def _text(out):
    if isinstance(out, bytes): return out.decode("utf-8", "replace")
    if isinstance(out, (list, tuple)): return "\n".join(out)
    return out or ""


def read(send, keys):
    saved = {}
    for k in keys:
        got = {m.group(1): m.group(2).rstrip("\r") for m in ANS.finditer(_text(send(f"cfg {k}")))}
        if k not in got: raise RuntimeError(f"cfg_keep: no answer for setting {k}")   # never restore from a value that was not read
        saved[k] = got[k] or None
    return saved


def restore(send, saved):
    for k, v in saved.items():
        if v is None:
            send(f"cfgdel {k}")
        else:
            if k in LIVE: send(LIVE[k].format(v=v))
            send(f"cfgset {k} {v}")
