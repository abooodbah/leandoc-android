"""Prints what an APK is made of, grouped, as a Markdown table.

    python3 scripts/apk-contents.py app-release.apk

"Stored" is the size inside the APK (what is downloaded), "Unpacked" the size
once installed or loaded. Native libraries are stored uncompressed so Android
can map them straight from the APK, so both columns match for them.
"""
import sys
import zipfile
from collections import defaultdict


def group(name):
    parts = name.split("/")
    if parts[0] == "lib" and len(parts) > 2:
        return "lib/" + parts[1] + "/" + parts[-1]
    if parts[0] == "assets" and len(parts) > 2:
        return "assets/" + "/".join(parts[1:-1]) + "/"
    if parts[0] in ("res", "META-INF"):
        return parts[0] + "/"
    if name.endswith(".dex"):
        return "classes*.dex"
    return name


def main(path):
    stored = defaultdict(int)
    unpacked = defaultdict(int)
    with zipfile.ZipFile(path) as apk:
        for info in apk.infolist():
            key = group(info.filename)
            stored[key] += info.compress_size
            unpacked[key] += info.file_size
    total = sum(stored.values())
    print("| Part | Stored bytes | Unpacked bytes | Share of APK |")
    print("| --- | ---: | ---: | ---: |")
    for key in sorted(stored, key=lambda k: -stored[k]):
        print(f"| `{key}` | {stored[key]:,} | {unpacked[key]:,} | {100 * stored[key] / total:.1f}% |")
    print(f"| total (entries) | {total:,} | {sum(unpacked.values()):,} | |")


if __name__ == "__main__":
    main(sys.argv[1])
