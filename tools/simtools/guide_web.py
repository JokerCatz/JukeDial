#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Web-tool screenshots for the guide: headless Chrome against tools/web served on localhost with the pretend serial port (?mockserial), as webcheck does.
usage: guide_web.py [--out DIR]   -> docs/guide/<lang>/img/web-{stations,settings,fonts,flash}.png  (mock data only, no device, no Wi-Fi settings read)"""
import argparse, os, socket, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from guide_share import share
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LANGS = {"en": "en", "zh-TW": "tw", "zh-CN": "cn"}
# id: (hash tab, extra query, window height)
TABS = {"web-stations": ("radio", "", 900), "web-settings": ("dev", "", 1500), "web-fonts": ("fonts", "", 1100),
        "web-flash": ("flash", "&manifest=testdata/firmware/manifest.json", 1100)}


def main():
    a = argparse.ArgumentParser(); a.add_argument("--out", default=os.path.join(REPO, "docs", "guide")); o = a.parse_args()
    s = socket.socket(); s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]; s.close()
    srv = subprocess.Popen(["python3", "-m", "http.server", str(port), "--bind", "127.0.0.1"], cwd=os.path.join(REPO, "tools", "web"),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1); prof = tempfile.mkdtemp(prefix="gw_"); n = 0
    try:
        for lang, code in LANGS.items():
            d = os.path.join(o.out, lang, "img"); os.makedirs(d, exist_ok=True)
            for sid, (tab, q, h) in TABS.items():
                p = os.path.join(d, sid + ".png")
                subprocess.run(["timeout", "60", "google-chrome", "--headless=new", "--disable-gpu", "--no-sandbox", f"--user-data-dir={prof}",
                                "--virtual-time-budget=10000", f"--window-size=1000,{h}", f"--screenshot={p}",
                                f"http://127.0.0.1:{port}/?lang={code}&mockserial{q}#{tab}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                n += os.path.exists(p)
    finally:
        srv.terminate()
    share(o.out)
    print(f"{n} web shots")


if __name__ == "__main__":
    main()
