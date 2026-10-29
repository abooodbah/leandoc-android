#!/usr/bin/env python3
"""Checks the Google Play graphics in store/ against Play's rules.

    python3 scripts/check-store-graphics.py [store-dir]

- store/icon-512.png: 512 x 512, 32-bit PNG (8-bit RGBA), at most 1 MB.
- store/feature-graphic.png or .jpg: 1024 x 500, 24-bit PNG (8-bit RGB, no
  transparency) or JPEG with three components, at most 15 MB.
- store/screenshots/: 2 to 8 phone screenshots, each 24-bit PNG or JPEG,
  9:16 or 16:9, each side 320 to 3840 px, at most 8 MB.

Prints one line per problem and exits with 1 if there is any. Uses only the
standard library, so CI needs nothing installed.
"""

import pathlib
import struct
import sys

MB = 1024 * 1024
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class Image:
    def __init__(self, kind, width, height, bits, transparent):
        self.kind = kind  # "png" or "jpeg"
        self.width = width
        self.height = height
        self.bits = bits  # bits per pixel: 24 for RGB, 32 for RGBA
        self.transparent = transparent


def read_png(data):
    if not data.startswith(PNG_SIGNATURE):
        return None
    position = len(PNG_SIGNATURE)
    width = height = bits = None
    transparent = False
    while position + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color, 0)
            # A palette image (color type 3) counts as its index depth.
            bits = depth * channels
            transparent = color in (4, 6)
        elif kind == b"tRNS":
            transparent = True
        elif kind == b"IEND":
            break
        position += 12 + length
    if width is None:
        return None
    return Image("png", width, height, bits, transparent)


def read_jpeg(data):
    if not data.startswith(b"\xff\xd8"):
        return None
    position = 2
    while position + 4 <= len(data):
        if data[position] != 0xFF:
            return None
        marker = data[position + 1]
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            position += 2
            continue
        length = struct.unpack(">H", data[position + 2:position + 4])[0]
        # Start-of-frame markers, except DHT (C4), JPG (C8) and DAC (CC).
        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            precision, height, width, components = struct.unpack(
                ">BHHB", data[position + 4:position + 10])
            return Image("jpeg", width, height, precision * components, False)
        position += 2 + length
    return None


def read(path):
    data = path.read_bytes()
    return read_png(data) or read_jpeg(data)


def check(store):
    problems = []

    icon = store / "icon-512.png"
    if not icon.is_file():
        problems.append(f"{icon}: missing")
    else:
        image = read(icon)
        if image is None or image.kind != "png":
            problems.append(f"{icon}: not a PNG file")
        else:
            if (image.width, image.height) != (512, 512):
                problems.append(f"{icon}: {image.width} x {image.height}, needs 512 x 512")
            if image.bits != 32:
                problems.append(f"{icon}: {image.bits}-bit, needs a 32-bit PNG with alpha")
        if icon.stat().st_size > MB:
            problems.append(f"{icon}: {icon.stat().st_size:,} bytes, needs at most 1 MB")

    features = [store / name for name in ("feature-graphic.png", "feature-graphic.jpg", "feature-graphic.jpeg")]
    present = [path for path in features if path.is_file()]
    if not present:
        problems.append(f"{store / 'feature-graphic.png'}: missing (PNG or JPEG)")
    elif len(present) > 1:
        problems.append(f"{store}: more than one feature graphic: {', '.join(p.name for p in present)}")
    else:
        feature = present[0]
        image = read(feature)
        if image is None:
            problems.append(f"{feature}: not a PNG or JPEG file")
        else:
            if (image.width, image.height) != (1024, 500):
                problems.append(f"{feature}: {image.width} x {image.height}, needs 1024 x 500")
            if image.bits != 24:
                problems.append(f"{feature}: {image.bits}-bit, needs 24-bit")
            if image.transparent:
                problems.append(f"{feature}: has transparency, which Play does not allow")
        if feature.stat().st_size > 15 * MB:
            problems.append(f"{feature}: {feature.stat().st_size:,} bytes, needs at most 15 MB")

    folder = store / "screenshots"
    shots = sorted(path for path in folder.glob("*") if path.suffix.lower() in (".png", ".jpg", ".jpeg")) \
        if folder.is_dir() else []
    if not 2 <= len(shots) <= 8:
        problems.append(f"{folder}: {len(shots)} phone screenshots, needs 2 to 8")
    for shot in shots:
        image = read(shot)
        if image is None:
            problems.append(f"{shot}: not a PNG or JPEG file")
            continue
        width, height = image.width, image.height
        if not (width * 16 == height * 9 or width * 9 == height * 16):
            problems.append(f"{shot}: {width} x {height} is not 9:16 or 16:9")
        if not (320 <= min(width, height) and max(width, height) <= 3840):
            problems.append(f"{shot}: {width} x {height}, each side needs 320 to 3840 px")
        if image.bits != 24:
            problems.append(f"{shot}: {image.bits}-bit, needs a 24-bit PNG or JPEG")
        if image.transparent:
            problems.append(f"{shot}: has an alpha channel or transparency, which Play does not allow")
        if shot.stat().st_size > 8 * MB:
            problems.append(f"{shot}: {shot.stat().st_size:,} bytes, needs at most 8 MB")
    return problems


def main():
    store = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "store")
    problems = check(store)
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print(f"{store}: icon, feature graphic and screenshots meet Google Play's rules")
    return 0


if __name__ == "__main__":
    sys.exit(main())
