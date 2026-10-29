# Third-party and shared files

These files are copied, not written here. `scripts/update-reader-assets.sh`
copies them from a checkout of the desktop app and records their SHA-256 sums
in `reader-assets.sha256`. A unit test fails if a copied file changes without
the sums file changing with it.

| Files | From | Version |
| --- | --- | --- |
| `app/src/main/assets/reader/reader.css`, `fixups.js`, `fidelity.js` | desktop LeanDoc `assets/` | commit in `reader-assets.sha256` |
| `app/src/main/assets/reader/reader.js` | desktop LeanDoc `assets/reader.js`, with the lowest fit zoom changed from 50% to 25% so a page fits a phone | same |
| `app/src/main/assets/reader/reader.html` | desktop LeanDoc `assets/reader.html`, plus one `<script>` line for `android-host.js` | same |
| `app/src/main/cpp/core/` | desktop LeanDoc `src/core/` | same |
| `native-tests/CoreTests.cpp`, `TestZip.*` | desktop LeanDoc `tests/core/` | same |
| `app/src/androidTest/assets/fixtures/` | desktop LeanDoc `tests/fixtures/` | same |
| `app/src/main/cpp/miniz/` | miniz, through the desktop's `third_party/` | 3.1.2 |
| `app/src/main/cpp/pugixml/` | pugixml, through the desktop's `third_party/` | 1.16 |
| `app/src/main/assets/reader/vendor/docx-preview.min.js` | npm `docx-preview` | 0.4.1 |
| `app/src/main/assets/reader/vendor/jszip.min.js` | npm `jszip` | 3.10.2 |
| `app/src/main/assets/reader/fonts/` | npm `@fontsource/ibm-plex-*` | 5.3.0 |

`app/src/main/assets/reader/android-host.js` is the only reader file written
for Android. Licenses are in `app/src/main/assets/THIRD_PARTY_NOTICES.md`,
which ships inside the app.
