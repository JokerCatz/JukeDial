#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Unit test of sim_diff.compare (I6: the Pillow path must see any one-level RGB change, and take only 8-bit RGB PNGs). Run: python3 tools/simtools/test_sim_diff.py"""
import os, sys, tempfile, unittest
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim_diff as sd

try:
    from PIL import Image
except ImportError:
    Image = None


@unittest.skipIf(Image is None, "Pillow missing: the pure-Python path is the one in use (it compares bytes)")
class Compare(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory(); self.addCleanup(self.d.cleanup)
        self.out = os.path.join(self.d.name, "out.png")

    def png(self, name, px, mode="RGB", size=(4, 3)):
        p = os.path.join(self.d.name, name); Image.new(mode, size, px).save(p); return p

    def one_pixel(self, base, other):
        a = self.png("a.png", base); im = Image.new("RGB", (4, 3), base); im.putpixel((2, 1), other)
        b = os.path.join(self.d.name, "b.png"); im.save(b)
        return sd.compare(a, b, self.out)

    def test_identical(self): self.assertEqual(sd.compare(self.png("a.png", (9, 9, 9)), self.png("b.png", (9, 9, 9)), self.out), (0, (0, 0, 0, 0)))

    def test_one_level_blue(self): self.assertEqual(self.one_pixel((10, 10, 10), (10, 10, 11)), (1, (2, 1, 1, 1)))

    def test_one_level_blue_four(self): self.assertEqual(self.one_pixel((10, 10, 10), (10, 10, 14))[0], 1)

    def test_one_level_red(self): self.assertEqual(self.one_pixel((10, 10, 10), (11, 10, 10))[0], 1)

    def test_one_level_green(self): self.assertEqual(self.one_pixel((10, 10, 10), (10, 11, 10))[0], 1)

    def test_size_mismatch(self): self.assertIsNone(sd.compare(self.png("a.png", (1, 2, 3)), self.png("b.png", (1, 2, 3), size=(5, 3)), self.out))

    def test_rgba_refused(self):
        with self.assertRaises(AssertionError): sd.compare(self.png("a.png", (1, 2, 3, 255), "RGBA"), self.png("b.png", (1, 2, 3)), self.out)


if __name__ == "__main__": unittest.main()
