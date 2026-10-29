"""Writes the Google Play icon and feature graphic from the launcher icon.

    python3 tools/store-graphics/make.py
    python3 tools/store-graphics/make.py --screenshots-only

Reads the adaptive launcher icon (res/drawable/ic_launcher_foreground.xml and
the ic_launcher_background color), turns it into SVG, draws it with Chromium
through Playwright, and saves:

- store/icon-512.png: 512 x 512, 32-bit PNG. The launcher background and mark,
  cropped to the 72 dp a launcher shows of the 108 dp adaptive icon. Play
  rounds the corners itself.
- store/feature-graphic.png: 1024 x 500, 24-bit PNG. The same icon on a tile,
  centered on the launcher background color with a soft glow of the mark's
  blue. No text. Everything that matters is inside the middle 70%, because
  Play may crop 15% from each edge.

It also saves the phone screenshots in store/screenshots/ as 24-bit PNG when
they have an alpha channel that is fully opaque, so no pixel changes. Play
does not accept screenshots with alpha. scripts/fetch-screenshots.sh runs it
with --screenshots-only after each capture.

Needs Python with Pillow, and Node with Playwright and its Chromium.
"""

import json
import pathlib
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
RES = ROOT / "app/src/main/res"
STORE = ROOT / "store"
ANDROID = "{http://schemas.android.com/apk/res/android}"

# The blue every Lean app mark uses.
BLUE = "#1D4ED8"


def launcher_background():
    colors = ElementTree.parse(RES / "values/colors.xml").getroot()
    for color in colors.iter("color"):
        if color.get("name") == "ic_launcher_background":
            return color.text.strip()
    raise SystemExit("no ic_launcher_background color")


def svg_of(element):
    """The SVG for one element of an Android vector drawable."""
    if element.tag == "group":
        sx = element.get(ANDROID + "scaleX", "1")
        sy = element.get(ANDROID + "scaleY", "1")
        tx = element.get(ANDROID + "translateX", "0")
        ty = element.get(ANDROID + "translateY", "0")
        inner = "".join(svg_of(child) for child in element)
        return f'<g transform="translate({tx} {ty}) scale({sx} {sy})">{inner}</g>'
    if element.tag == "path":
        attributes = {
            "d": element.get(ANDROID + "pathData"),
            "fill": element.get(ANDROID + "fillColor", "none"),
            "stroke": element.get(ANDROID + "strokeColor"),
            "stroke-width": element.get(ANDROID + "strokeWidth"),
            "stroke-linecap": element.get(ANDROID + "strokeLineCap"),
            "stroke-linejoin": element.get(ANDROID + "strokeLineJoin"),
        }
        text = " ".join(f'{name}="{value}"' for name, value in attributes.items() if value is not None)
        return f"<path {text}/>"
    raise SystemExit(f"unexpected element in the launcher icon: {element.tag}")


def foreground():
    vector = ElementTree.parse(RES / "drawable/ic_launcher_foreground.xml").getroot()
    if vector.get(ANDROID + "viewportWidth") != "108" or vector.get(ANDROID + "viewportHeight") != "108":
        raise SystemExit("the launcher foreground is not a 108 x 108 adaptive icon")
    return "".join(svg_of(child) for child in vector)


def icon_svg(size, background, mark):
    # The middle 72 of the 108 units, which is what a launcher shows.
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="18 18 72 72">'
        f'<rect x="0" y="0" width="108" height="108" fill="{background}"/>{mark}</svg>'
    )


def feature_svg(background, mark):
    tile = 300
    x = (1024 - tile) // 2
    y = (500 - tile) // 2
    radius = round(tile * 0.22)
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="500" viewBox="0 0 1024 500">
  <defs>
    <radialGradient id="glow" cx="512" cy="250" r="420" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="{BLUE}" stop-opacity="0.42"/>
      <stop offset="0.55" stop-color="{BLUE}" stop-opacity="0.10"/>
      <stop offset="1" stop-color="{BLUE}" stop-opacity="0"/>
    </radialGradient>
    <filter id="shadow" x="-30%" y="-30%" width="160%" height="160%">
      <feDropShadow dx="0" dy="14" stdDeviation="22" flood-color="#000000" flood-opacity="0.45"/>
    </filter>
    <clipPath id="tile"><rect x="{x}" y="{y}" width="{tile}" height="{tile}" rx="{radius}"/></clipPath>
  </defs>
  <rect width="1024" height="500" fill="{background}"/>
  <rect width="1024" height="500" fill="url(#glow)"/>
  <rect x="{x}" y="{y}" width="{tile}" height="{tile}" rx="{radius}" fill="{background}" filter="url(#shadow)"/>
  <g clip-path="url(#tile)">
    <svg x="{x}" y="{y}" width="{tile}" height="{tile}" viewBox="18 18 72 72">
      <rect x="0" y="0" width="108" height="108" fill="{background}"/>{mark}
    </svg>
  </g>
  <rect x="{x + 0.5}" y="{y + 0.5}" width="{tile - 1}" height="{tile - 1}" rx="{radius}" fill="none"
        stroke="#FFFFFF" stroke-opacity="0.10"/>
</svg>"""


RENDER = r"""
const { chromium } = require('playwright');
(async () => {
  const jobs = JSON.parse(process.argv[1]);
  const browser = await chromium.launch();
  const page = await browser.newPage({ deviceScaleFactor: 1 });
  for (const job of jobs) {
    await page.setViewportSize({ width: job.width, height: job.height });
    await page.setContent('<!doctype html><html><body style="margin:0;background:transparent">' + job.svg + '</body></html>');
    await page.screenshot({ path: job.out, omitBackground: true, clip: { x: 0, y: 0, width: job.width, height: job.height } });
  }
  await browser.close();
})().catch((error) => { console.error(error); process.exit(1); });
"""


def render(jobs):
    subprocess.run(["node", "-e", RENDER, json.dumps(jobs)], check=True)


def opaque_screenshots():
    for shot in sorted((STORE / "screenshots").glob("*.png")):
        image = Image.open(shot)
        if image.mode != "RGBA":
            continue
        if image.getchannel("A").getextrema() != (255, 255):
            raise SystemExit(f"{shot} has transparent pixels; it needs a new capture, not a conversion")
        # Every pixel is fully opaque, so dropping the alpha channel keeps the
        # colors exactly.
        image.convert("RGB").save(shot, optimize=True)
        print(f"{shot.relative_to(ROOT)}: saved as 24-bit PNG, pixels unchanged")


def main():
    if "--screenshots-only" in sys.argv[1:]:
        opaque_screenshots()
        return
    background = launcher_background()
    if not re.fullmatch(r"#[0-9A-Fa-f]{6}", background):
        raise SystemExit(f"unexpected launcher background {background}")
    mark = foreground()
    with tempfile.TemporaryDirectory() as work:
        icon = pathlib.Path(work) / "icon.png"
        feature = pathlib.Path(work) / "feature.png"
        render([
            {"svg": icon_svg(512, background, mark), "width": 512, "height": 512, "out": str(icon)},
            {"svg": feature_svg(background, mark), "width": 1024, "height": 500, "out": str(feature)},
        ])
        Image.open(icon).convert("RGBA").save(STORE / "icon-512.png", optimize=True)
        Image.open(feature).convert("RGB").save(STORE / "feature-graphic.png", optimize=True)
    for name in ("icon-512.png", "feature-graphic.png"):
        path = STORE / name
        image = Image.open(path)
        print(f"{path.relative_to(ROOT)}: {image.size[0]} x {image.size[1]} {image.mode}, {path.stat().st_size:,} bytes")
    opaque_screenshots()


if __name__ == "__main__":
    main()
