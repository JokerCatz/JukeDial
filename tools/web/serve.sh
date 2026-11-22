#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# The settings + flashing page on this computer only: http://localhost:8000/ in Chrome or Edge (Web Serial needs one of them).
# localhost counts as a secure context, and an http page may preview http radio streams.  Binds to 127.0.0.1 only.
cd "$(dirname "$0")" || exit 1
URL=http://localhost:8000/
(sleep 1; xdg-open "$URL" || open "$URL") >/dev/null 2>&1 &   # best effort: open the default browser
echo "$URL  (Ctrl+C to stop)"
exec python3 -m http.server 8000 --bind 127.0.0.1
