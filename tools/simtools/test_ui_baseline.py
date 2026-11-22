#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Unit test of the fx.txt content comparison in ui_baseline.py (batch C board finding C4). Run: python3 tools/simtools/test_ui_baseline.py"""
import os, sys, unittest
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_baseline as u


class FxKept(unittest.TestCase):
    OLD = b"format=1\ngain.on=0\ngain.gain_db=3\n"

    def test_identical(self): self.assertEqual(u.fx_kept(self.OLD, self.OLD), (True, []))

    def test_new_keys_are_allowed(self):
        ok, added = u.fx_kept(self.OLD + b"tube.on=0\ntube.drive_db=6\n", self.OLD)
        self.assertTrue(ok); self.assertEqual(added, ["tube.drive_db", "tube.on"])

    def test_reordered_whitespace_crlf(self):
        self.assertEqual(u.fx_kept(b"gain.gain_db=3\r\n\ngain.on=0\nformat=1", self.OLD), (True, []))

    def test_comment_and_blank_lines_skipped(self):
        self.assertEqual(u.fx_kept(b"# written by a firmware\n\n" + self.OLD, self.OLD), (True, []))
        self.assertEqual(u.fx_parse(b"# a=1\nb=2\n"), {"b": "2"})

    def test_changed_value(self):
        a = self.OLD.replace(b"=3", b"=4")
        self.assertFalse(u.fx_kept(a, self.OLD)[0])
        self.assertEqual(u.fx_first_diff(a, self.OLD), "gain.gain_db: before '3', after '4'")

    def test_lost_key(self):
        a = b"format=1\ngain.on=0\n"
        self.assertFalse(u.fx_kept(a, self.OLD)[0])
        self.assertEqual(u.fx_first_diff(a, self.OLD), "gain.gain_db: before '3', after missing")

    def test_none_is_empty(self):
        self.assertEqual(u.fx_kept(None, b""), (True, []))
        self.assertFalse(u.fx_kept(None, self.OLD)[0])


class FlatShots(unittest.TestCase):
    """the flat-picture guard (2026-11-18: blank HUD baselines passed sim-diff because black equals black)"""
    def test_flat_fails_unless_listed(self):
        import tempfile
        try: from PIL import Image
        except ImportError: self.skipTest("no Pillow")
        with tempfile.TemporaryDirectory() as d:
            Image.new("RGB", (8, 8), (4, 4, 16)).save(os.path.join(d, "blank_page.png"))
            Image.new("RGB", (8, 8), (255, 255, 255)).save(os.path.join(d, "tools_torch.png"))
            im = Image.new("RGB", (8, 8)); im.putpixel((1, 1), (9, 9, 9)); im.save(os.path.join(d, "real.png"))
            self.assertEqual(u.flat_shots(d), ["blank_page"])
            self.assertEqual(u.flat_shots(d, {"real"}), [])


if __name__ == "__main__":
    unittest.main()
