"""Tests for check-store-graphics.py: one broken rule at a time.

    python3 scripts/test_check_store_graphics.py

The checker reads only the PNG and JPEG headers, so each test file here is a
valid header with a minimal body.
"""

import importlib.util
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("check", HERE / "check-store-graphics.py")
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)

MB = 1024 * 1024


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def png(width, height, color=2, depth=8, extra=b"", padding=0):
    header = chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, 0))
    body = chunk(b"IDAT", zlib.compress(b"\0"))
    filler = chunk(b"zPAD", b"\0" * padding) if padding else b""
    return check.PNG_SIGNATURE + header + extra + filler + body + chunk(b"IEND", b"")


def jpeg(width, height, components=3, padding=0):
    frame = struct.pack(">BHHB", 8, height, width, components) + b"\x01\x11\x00" * components
    comment = b"\xff\xfe" + struct.pack(">H", 2 + padding) + b"\0" * padding if padding else b""
    return b"\xff\xd8" + comment + b"\xff\xc0" + struct.pack(">H", 2 + len(frame)) + frame + b"\xff\xd9"


class StoreGraphicsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.store = pathlib.Path(self.temporary.name)
        (self.store / "screenshots").mkdir()
        self.write("icon-512.png", png(512, 512, color=6))
        self.write("feature-graphic.png", png(1024, 500))
        self.write("screenshots/light.png", png(1080, 1920))
        self.write("screenshots/dark.png", png(1080, 1920))

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, name, data):
        (self.store / name).write_bytes(data)

    def problems(self):
        return check.check(self.store)

    def assertProblem(self, words):
        problems = self.problems()
        self.assertTrue(any(words in problem for problem in problems), problems)

    def test_a_complete_set_passes(self):
        self.assertEqual([], self.problems())

    def test_a_jpeg_feature_graphic_passes(self):
        (self.store / "feature-graphic.png").unlink()
        self.write("feature-graphic.jpg", jpeg(1024, 500))
        self.assertEqual([], self.problems())

    def test_jpeg_screenshots_pass(self):
        self.write("screenshots/dark.png", jpeg(1080, 1920))
        self.assertEqual([], self.problems())

    def test_the_icon_must_exist(self):
        (self.store / "icon-512.png").unlink()
        self.assertProblem("icon-512.png: missing")

    def test_the_icon_must_be_512_square(self):
        self.write("icon-512.png", png(511, 512, color=6))
        self.assertProblem("511 x 512, needs 512 x 512")

    def test_the_icon_must_have_alpha(self):
        self.write("icon-512.png", png(512, 512, color=2))
        self.assertProblem("24-bit, needs a 32-bit PNG with alpha")

    def test_the_icon_must_be_a_png(self):
        self.write("icon-512.png", jpeg(512, 512))
        self.assertProblem("icon-512.png: not a PNG file")

    def test_the_icon_must_be_at_most_1_mb(self):
        self.write("icon-512.png", png(512, 512, color=6, padding=MB))
        self.assertProblem("needs at most 1 MB")

    def test_the_feature_graphic_must_exist(self):
        (self.store / "feature-graphic.png").unlink()
        self.assertProblem("feature-graphic.png: missing")

    def test_only_one_feature_graphic(self):
        self.write("feature-graphic.jpg", jpeg(1024, 500))
        self.assertProblem("more than one feature graphic")

    def test_the_feature_graphic_must_be_1024_by_500(self):
        self.write("feature-graphic.png", png(1024, 512))
        self.assertProblem("1024 x 512, needs 1024 x 500")

    def test_the_feature_graphic_must_not_have_alpha(self):
        self.write("feature-graphic.png", png(1024, 500, color=6))
        self.assertProblem("needs 24-bit")
        self.assertProblem("has transparency")

    def test_the_feature_graphic_must_not_have_a_transparent_color(self):
        self.write("feature-graphic.png", png(1024, 500, extra=chunk(b"tRNS", b"\0\0\0\0\0\0")))
        self.assertProblem("has transparency")

    def test_a_grayscale_jpeg_feature_graphic_fails(self):
        (self.store / "feature-graphic.png").unlink()
        self.write("feature-graphic.jpg", jpeg(1024, 500, components=1))
        self.assertProblem("8-bit, needs 24-bit")

    def test_the_feature_graphic_must_be_at_most_15_mb(self):
        self.write("feature-graphic.png", png(1024, 500, padding=15 * MB))
        self.assertProblem("needs at most 15 MB")

    def test_at_least_two_screenshots(self):
        (self.store / "screenshots/dark.png").unlink()
        self.assertProblem("1 phone screenshots, needs 2 to 8")

    def test_at_most_eight_screenshots(self):
        for index in range(7):
            self.write(f"screenshots/extra-{index}.png", png(1080, 1920))
        self.assertProblem("9 phone screenshots, needs 2 to 8")

    def test_screenshots_must_be_9_by_16_or_16_by_9(self):
        self.write("screenshots/dark.png", png(1080, 1440))
        self.assertProblem("is not 9:16 or 16:9")

    def test_landscape_screenshots_pass(self):
        self.write("screenshots/dark.png", png(1920, 1080))
        self.assertEqual([], self.problems())

    def test_screenshot_sides_must_be_at_least_320(self):
        self.write("screenshots/dark.png", png(180, 320))
        self.assertProblem("each side needs 320 to 3840 px")

    def test_screenshot_sides_must_be_at_most_3840(self):
        self.write("screenshots/dark.png", png(2250, 4000))
        self.assertProblem("each side needs 320 to 3840 px")

    def test_screenshots_must_not_have_alpha(self):
        self.write("screenshots/dark.png", png(1080, 1920, color=6))
        self.assertProblem("has an alpha channel or transparency")

    def test_screenshots_must_be_at_most_8_mb(self):
        self.write("screenshots/dark.png", png(1080, 1920, padding=8 * MB))
        self.assertProblem("needs at most 8 MB")


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False).result.wasSuccessful() else 1)
