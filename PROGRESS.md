# Progress

Updated with every merge. The next session starts here.

## Round 0.3.0 (November 2026)

The request: bring LeanDoc for Android in line with LeanDoc desktop 0.3.2,
add build provenance attestations to the release workflow, and release
0.3.0. The reader and core come from desktop tag v0.3.2 through
`scripts/update-reader-assets.sh`.

### Desktop changes since Android 0.2.0

Android 0.2.0 carries the reader from desktop main a5c1a73. Desktop 0.2.0
(tag v0.2.0, released 2026-11-06) has the same reader; only a timing record
(#34) came between. Desktop releases after 2026-11-06: 0.3.0 and 0.3.1
(2026-11-15) and 0.3.2 (2026-11-16). Desktop main after v0.3.2 adds build
provenance attestations (#46). Between a5c1a73 and v0.3.2 the desktop
changed `assets/reader.js`, `fixups.js` and `reader.css` only: `src/core/`,
`third_party/`, the core tests and `tests/fixtures/` are unchanged.

| Desktop change | Release | On a phone | Plan |
| --- | --- | --- | --- |
| Tables, figures and text boxes that Word floats at the top or foot of a page (wrapped top and bottom) are placed there, across the columns, and text no longer runs over them | 0.3.0 | Yes | Port; emulator tests at phone width |
| Two-column papers lay out in two columns; references keep two columns and a hanging indent | 0.3.0 | Yes | Port; emulator tests at phone width |
| Tab stops sized by the reader: contents pages keep dot leaders and right-aligned page numbers | 0.3.0 | Yes | Port; emulator test |
| Numbered headings take Word's indents and stops; centred labels | 0.3.0 | Yes | Port |
| A section without its own header takes the previous one; page number size and place | 0.3.0 | Yes | Port |
| A footnote with the author's own mark shows that mark | 0.3.0 | Yes | Port |
| An equation wider than its column wraps inside the column | 0.3.0 | Yes | Port |
| A page that starts inside a word ends with a hyphen | 0.3.0 | Yes | Port |
| Small capitals at Word's size, in the run's own font | 0.3.0 | Yes | Port |
| Strict OOXML files keep their styles, lists, notes and tab stops | 0.3.0 | Yes | Port; `kinds/strict.docx` is already a fixture |
| Some documents open more slowly (two-column papers 6 to 11%, Strict files 18% on the desktop) | 0.3.0 | Measure | Open time on the emulator for main and the branch, with the fixtures and the 300-page document, in the port's pull request |
| Fewer near-empty extra pages, also across a section boundary | 0.3.1 | Yes | Port |
| The Microsoft Store lists the app as "LeanDoc Word Viewer" | 0.3.2 | No | Skipped: the Android app keeps its name, package id and launcher label. The Play listing is the maintainer's |
| Store screenshots, the "open .docx without Word" site page, winget and Store manifests, AppStream metadata | 0.3.0 to 0.3.2 | No | Skipped: desktop packaging and site |
| Build provenance attestations for release files (#46) | main after 0.3.2 | Yes, for the release workflow | Own pull request: `release.yml` and a README line |

### Plan

Pull requests, in order. Each one updates this file when it merges.

1. This plan.
2. The budgets job times opening each test document from a cold start
   (the feature fixtures, the desktop's reader test documents once they are
   fixtures, the Strict OOXML file and the store document), for main and
   the branch, next to the 300-page document it already times.
3. The reader and core from desktop v0.3.2 through
   `scripts/update-reader-assets.sh`. The repository's own fixtures have no
   two-column page and no floating table, so the tests open the documents
   the desktop's reader tests build at test time (`tests/reader/make-*.mjs`),
   which the update script now writes into `fixtures/reader/` with the
   desktop's own generators. Failing emulator tests first, at the phone's
   width and fit zoom: a floating table at the top of its page and a figure
   at its foot, across both columns, with no text under them; two columns
   kept; references at their hanging indent; a contents line with its dot
   leader; wide tables inside the page; Word's page count where 0.3.1 fixed
   near-empty pages; Strict OOXML lists. Also on a small phone, in
   landscape and after scrolling by touch. Then the port. The open-time
   numbers for main and the branch go in the pull request; over 10% slower
   needs a reason and, where reasonable, a fix.
4. No text cut off at the fit zoom. In a headless Chromium 141 check at
   phone widths, the desktop's two-column test paper lost the last line of
   a column past the page's edge at 36%, 43% and 66% zoom with the main
   reader, and with the v0.3.2 reader a long table lost its last row at 31%
   to 66%: pages are split at 100% and drawn smaller, where lines take a
   little more room. The first try (#28, closed) put the tests before the
   port; on the CI emulator (WebView 113) the main reader did not lose
   those lines, and its tests failed only on a text box that the 0.2.0
   reader draws 3 px past the page, which the port fixes. So the tests and
   the fix come after the port.
5. Build provenance attestations in `release.yml` (actions/attest-build-provenance
   v4.2.2, pinned to 4d101475d8b20a2381f78447822ac1eab6504dd8) and the README
   line on `gh attestation verify`.
6. Version 0.3.0 (code 5): `CHANGELOG.md`, `README.md`, fastlane changelog
   `5.txt`, release notes in `docs/releases/v0.3.0.md`. The tag, the GitHub
   release and the Play bundle run are left to the maintainer: this session
   cannot push tags.

## Round 0.2.0 (November 2026)

The request: make LeanDoc for Android look and work like the other Lean
Apps, with LeanMark desktop 0.6.0 as the reference design.

1. Efficiency review of what runs on open, scroll, find, zoom, view and
   theme switch and rotation; memory; dead code; dependencies; APK size.
   Numbers from release builds in CI. Clear wins in their own pull
   requests.
2. The section highlight on a phone, including the outline shown as a
   sheet, styled like LeanMark's in every theme.
3. The five themes (System, Light, Dark, Dim, Dusk) with LeanMark's
   colours, the theme button's coloured dot with its accessible name, and
   a settings button that opens a sheet with theme, zoom and view. Text
   width does not apply: page view keeps the document's page width, and
   reading view already fills a phone.
4. Port LeanDoc desktop's changes from this round once they are merged:
   themes, the page colour setting (white like paper, or follow the theme,
   remembered) and the highlight look. Reconcile with step 3.
5. Version 0.2.0: version bump, tag v0.2.0, GitHub pre-release with the
   APK, the bundle and their .sha256 files ("Release files" workflow), and
   the "Play bundle" workflow for the tag. Nothing goes to Google Play.

Pull requests, in order:

| Step | Pull request | State |
| --- | --- | --- |
| 1 | #15 Measure reader actions and a large document on the emulator | merged |
| 1 | #16 Export only the JNI entry point from the native library | merged |
| 1 | #17 Leave the Kotlin metadata files out of the APK | merged |
| 1 | #18 Let go of the package once the page has it | merged |
| 2 | #19 The outline as a sheet on phones; the highlight after a long jump | merged |
| 3 | #20 Five themes and the theme dot; settings button and sheet | merged |
| 4 | #21 Port LeanDoc desktop's merged reader changes (Dim and Dusk, settle pass, find, zoom and view fixes) | merged |
| 4 | #23 Port the desktop's theme dot, settings panel, toolbar, page colour and highlight look | merged |
| 5 | #24 Release 0.2.0: version 0.2.0 (code 4), CHANGELOG.md, store and site texts | merged; tag and pre-release from the maintainer's PC |

### Efficiency review

Measured in the "Budgets on the emulator" job: release builds of main and
the branch on one API 34 emulator (x86_64, SwiftShader, no GPU), taking
turns. The action times come from `android-host.js` (#15): from the key or
typed letter to the second animation frame after it. Run 41981911611 for
#15, 41982100237 for #16, 41982189190 for #17.

| Place | What it costs | Measured (main) | Proposed fix | State |
| --- | --- | --- | --- | --- |
| Native library: the C++ runtime is linked statically and all its symbols were exported | Exported code cannot be dropped by `--gc-sections` | 1,977 exported symbols; arm64 `libleandoc.so` 1,122,384 bytes; APK 3,352,010 bytes; arm64 download 735,329 bytes | Export only the JNI entry point (version script) | fixed in #16: 1 symbol; 426,032; 1,540,690; 535,974 |
| Kotlin compiler metadata in the APK | Files only kotlin-reflect reads | 13,728 bytes of the APK, 13,345 of the arm64 download | Leave them out | fixed in #17: 13,728 bytes less, measured on main before #16 |
| The app keeps the sanitized package after the page fetched it | The document's size in the Java heap for as long as it is open; the page keeps its own copy | Java heap 25,120 KB, app PSS 151,833 KB after the 22.5 MB `large-photos.docx` | Drop it once served; a new page reads the file again | fixed in #18: Java heap 1,352 KB |
| Open: WebView, page and document | Cold start to the first page | First frame 145 ms over the empty app; drawn 1,282 ms over its first frame (`styles.docx`); 300 pages drawn in 3,634 ms. WebView created at 211 ms, page loaded at 677, drawn at 1,597 | No clear win: the page's scripts load in about 470 ms and docx-preview renders in about 900. Budget is for a phone (under 1 s); see the phone test | for you to decide |
| Scroll | The reader's scroll handler filters the outline entries and reads page boxes on every scroll event | 87 to 90% janky frames on the software GPU, the same on main and branch | None measurable without a GPU | phone test |
| Find on every keystroke (desktop's known issue) | Searches the whole document per letter | 23 ms per letter (median of 7), slowest 32 ms, 300 pages | Small here; the desktop's fix (a 120 ms pause) came with the port | ported in #21 |
| Zoom step (desktop's known issue) | Lays out the document again | 34 ms to the frame (median of 4); pages out of view are skipped (content-visibility) | None needed on Android | none |
| View switch (desktop's known issue) | On Android the page keeps the package and parses it again; the app does not read the file again | 838 ms render (median of 2, page and reading view, 300 pages) | The desktop's fix (reuse the parsed document) came with the port | ported in #21 |
| Theme switch | Restyles the whole page | 84 ms to the frame (median of 3) | None | none |
| Rotation | The activity is kept; the fit zoom changes and the page is laid out again after the reader's 150 ms settle | 340 ms to the frame with the new zoom (median of 2) | None | none |
| System dark mode switch while reading | The activity is created again: the file is read and cleaned again, the page reloads, and the reading position is lost | 2,789 to 3,435 ms to the document drawn again (one sample per run) | Keep the WebView (uiMode in configChanges) | fixed in #22 |
| Memory while opening | The file, the core's copy and the package are in memory together | not measured separately | The core needs the bytes; none now | none |
| Dead code | The reader page is the desktop's file: tabs, recent files and keyboard shortcuts stay, unused on a phone | none removed | Keep the desktop file unchanged | none |
| Dependencies | The app has no runtime libraries; tests use JUnit, org.json and AndroidX Test | none unused | none | none |
| Fonts | IBM Plex Sans Arabic (88,536 bytes) is for Arabic file names in the toolbar | 88,564 bytes stored, about 6% of the APK after #16 | Keep (same look as desktop), or use the phone's Arabic font | for you to decide |

### Desktop releases after 2026-10-29

None yet: the newest desktop release is v0.1.4 (2026-10-28), and the
reader here is the desktop's 0.1.4 page. Desktop main after it has the
Chocolatey route, the Android closed test sign-up and site changes, none of
which touch the reader. LeanDoc desktop 0.2.0 is being made in this round;
its features are listed here when it is merged.

Desktop features from 0.1.4 that the app lacks, and why:

- Tabs and a later launch opening its files as tabs: Android shows one
  document per window; the system's recent apps list does the rest.
- Recent files: Android's file picker and the opening app keep that list.
- Reload when the file changes: a shared file comes with read access only,
  and no change notice.
- Drag and drop, `leandoc-inspect` and the command-line test hooks: desktop
  only.
- "Open in Word, then LibreOffice": the app offers the file to any other
  app instead.

## Done

- Releases 0.1.0, 0.1.1 and 0.1.2 (2026-10-29 and 30); 0.1.2 is signed with
  the stable release key.
- #15: the budgets job also times find, zoom, the theme and view switch and
  rotation on the 300-page document, and measures memory after a 22.5 MB
  document with two photo-sized pictures (`large-photos.docx`, made by the
  test file provider). The mutation workflow can run one suite.
- #16: the native library exports only its JNI entry point. The release
  APK went from 3,352,010 to 1,540,690 bytes, the arm64 library from
  1,122,384 to 426,032, and the arm64 download from 735,329 to 535,974.
  CI checks the exports (`scripts/check-native-exports.sh`).
- #17: the Kotlin compiler's metadata files are left out of the APK
  (13,728 bytes); CI checks it (`scripts/check-apk-extras.sh`).
- #18: the app lets go of the sanitized package once the page has it.
  After the 22.5 MB `large-photos.docx` the app's Java heap went from
  25,080 to 1,300 KB and the total PSS with the renderer from 221,846 to
  197,739 KB (run 41987890797). The app's PSS after that document varies by
  tens of MB between runs of the same build, so the Java heap is the steadier
  number. The time to draw that document is one sample per build and varied
  (2,112 and 3,143 ms in that run, 1,744 and 1,909 in an earlier one); not
  measured further.
- #19: on a phone the outline opens as a sheet from the bottom edge (the
  desktop page hides it below 760 px), with the current section highlighted
  and kept in view; a tapped entry jumps and closes it; back, the scrim,
  Escape and the close button close it; in landscape the pane returns with
  the current entry in view. After a long jump the highlight is checked
  again once scrolling stops (it stayed several sections off). On a phone
  "Open in another app" is an icon. The phone layout moved to `android.css`.
- #20: LeanMark's five themes (System, Light, Dark, Dim, Dusk) with
  LeanMark 0.6.0's colours for every shared token; the LeanDoc-only tokens
  as LeanDoc desktop's Dim and Dusk choose them (leandoc #27). The theme
  button shows LeanMark's dot and its name says the theme ("Theme: System
  (Dark)"). A settings button opens a sheet with the theme, view and zoom
  (on a phone from the bottom, above 760 px at the right edge). The
  current section is marked as LeanMark marks it. The system bars take the
  theme's toolbar colour. Tab stays inside an open sheet. On a phone the
  view switch is in the sheet and the controls take two rows down to
  320 dp. The reader's theme functions reach the desktop's reader.js
  through `scripts/android-reader.patch` until the desktop has them.
- #21: the reader page from LeanDoc desktop main c43cd1c: Dim and Dusk in
  reader.css (the same values as #20, so Android's copies went), the second
  look for the current section once scrolling stops (Android's workaround
  went), Find after a 120 ms pause in typing, zoom steps that arrive
  together laid out once, and a switch back to a view that reuses the
  parsed document. The patch now adds only the theme dot and its name.
- #22: the system's light and dark switch keeps the activity, the WebView,
  the document and the reading position. System recolours the page and the
  bars in place; a chosen theme does not change.
- #23: the reader page from LeanDoc desktop main a5c1a73: the theme dot
  (#28), the settings panel with theme, view, page colour and zoom (#29),
  the restyled toolbar (#30) and the page colour setting, white like paper
  or the theme's colours (#31), and the highlight look matched to LeanMark
  (#32), which replaces Android's copy. Android's own settings sheet and
  `scripts/android-reader.patch` went. On a phone the desktop's panel is a
  sheet at the bottom without the shortcut list, the document keeps its
  width, back closes it, and the file name and the theme dot stay in the
  toolbar. The app keeps the page colour (`pageColor`: `paper` or `theme`).

- Gradle project: Kotlin, classic Views, no AndroidX or other libraries in
  the app. compileSdk and targetSdk 36, minSdk 26.
- Manifest: no permissions; open-with filters for .docx, .dotx, .docm and
  .dotm (VIEW and SEND); WebView metrics and Safe Browsing off; backup off.
- Reader (PR #2): the desktop reader page from LeanDoc v0.1.4 (docx-preview
  0.4.1, JSZip 3.10.2) in the system WebView, with the desktop's C++ core
  checking and cleaning every document first: macros, ActiveX, OLE objects,
  external relationships, remote templates and images, embedded fonts and
  comments are removed, and tracked changes are accepted. Only the reader
  files and the current document's cleaned package are served; navigation
  is refused; content:// only; 64 MiB limit; legacy .doc files get the
  desktop fallback page with an action that offers them to other apps; a
  crashed renderer is replaced; WebView older than 80 gets a plain message.
- CI: lint, JVM tests, the desktop core's tests with the desktop fixtures,
  release APK and bundle, the permission check, instrumented tests on API 26
  (WebView 69) and API 34 (WebView 113), budgets against the base branch and
  an empty app, APK contents, the arm64 download size and a native-library
  breakdown. Mutation check: `mutations.yml` with `mutations/*.json`.
- Store texts, data safety, content rating, closed-test plan and light and
  dark screenshots in `store/`; signing steps in the README.

## In progress

- Round 0.3.0: plan merged (#27); step 2, timing the test documents.
- Round 0.2.0 is done: v0.2.0 was released on 2026-11-06 from the
  maintainer's PC.

## Remaining

- The phone test below, on real hardware.
- Play Console setup and the closed test (see `store/closed-test.md`);
  these need the maintainer's account.

### Start time against 0.1.2

One emulator run (run 42218047019, draft #25, closed), builds taking turns,
15 cold starts of `styles.docx` each, release builds. The empty app's first
frame: 448 ms (median of 15).

| Build | First frame (median) | Drawn (median) | Drawn minus the empty app | Drawn, lowest to highest |
| --- | ---: | ---: | ---: | --- |
| v0.1.2 | 505 ms | 1,406 ms | 958 ms | 834 to 1,944 ms |
| main 7a6ba41 | 508 ms | 1,368 ms | 920 ms | 1,184 to 1,784 ms, and one start at 2,283 |
| main again (same APK) | 517 ms | 1,364 ms | 916 ms | 777 to 1,508 ms |

Main is 4% faster than 0.1.2 to the drawn document, as close as main is to
itself. In earlier runs the starts fell into two groups about a second
apart; here 44 of 45 were in the fast group. The 1,674 against 1,512 ms in
the last port's budgets were main before and after that port, not 0.1.2.

## Known gaps

- Images and documents linked from outside the file are not shown, by
  design: they would need the network.
- The desktop's 0.1.4 reader is not faster than 0.1.3 on the emulator for
  the 300-page document (6,776 against 6,313 ms in one comparison run);
  whether it is on a phone is part of the phone test.
- The emulator has no GPU, so start time and jank from CI are not the
  budget check, and in some runs the scrolling measure did not register
  frames at all. Start times on it are also bimodal: the same APK measured
  1,622 and 2,434 ms to the drawn document in one run (PR #14).
- On WebView before 128 (the API 34 emulator has 113), boxes inside the
  zoomed page are reported unzoomed (the phone layout and outline tests
  correct for it). The reader's highlight compares those boxes with the
  document area's edge as they are, so on such a WebView the highlight
  moves on later than the heading passes; by how much was not measured.
  Current WebView (128 and later) is not affected.

## Measured numbers

From the budgets job on an API 34 emulator (SwiftShader, no GPU), PR #2,
run 40845337523. The empty app took 1,718 ms to its first frame there.

| Measure | Value |
| --- | ---: |
| Release APK, all ABIs | 3,351,134 bytes |
| What Play delivers to an arm64-v8a phone | 1,527,669 bytes installed, 734,516 download |
| First frame minus the empty app, styles.docx | 229 ms |
| Document drawn minus the empty app's first frame, styles.docx | 3,779 ms |
| long-300-pages.docx drawn after a cold start | 8,497 ms |
| Total PSS after styles.docx, app and renderer | 122,461 KB |
| App PSS after long-300-pages.docx | 71,773 KB |
| WebView renderer PSS after long-300-pages.docx | 81,493 KB |
| Total PSS after long-300-pages.docx | 153,274 KB |

Where the start time goes with styles.docx (ms from the activity's
creation): WebView created 658, page loaded 2,259, document sent 2,270,
document drawn 5,031.

## Phone test

The emulator in CI has no GPU, so its start times and jank do not show what
a phone does. This is the check to run on a real phone. It takes about ten
minutes.

### Install

From the release page on GitHub, download onto the phone:

- `LeanDoc-0.1.0.apk`: the release build (minified), signed with a throwaway
  key. Use this one for timing.
- `LeanDoc-0.1.0-debug.apk`: the debug build, for comparison only. It is slower
  by design.
- `empty-app.apk`: an app with one empty screen, for the start-time
  baseline with adb (it has no launcher icon).
- `LeanDoc-0.1.0-phone-test-files.zip`: the files below. Unzip it into
  Downloads.

Open the APK from the browser's downloads or the Files app and allow the
install when Android asks. Both APKs have the same package name, so the
second replaces the first. To check a download, compare it with its
`.sha256` file.

### Start time

1. Settings > Apps > LeanDoc > Force stop.
2. Start a screen recording (Quick Settings > Screen record), then in the
   Files app tap `styles.docx` in Downloads and choose LeanDoc.
3. Stop the recording when the first page appears. Step through the recording frame
   by frame and note the time from the tap to the first page on screen.
4. Repeat steps 1 to 3 three times and keep the middle value.
5. Do the same with an app that does very little (for example the
   Calculator), and subtract its time: that difference is the number the
   budget means. The budget is under 1 second.

With a computer and adb the system measures it:

```sh
# Start from the launcher icon to the first frame (TotalTime, in ms):
adb shell am force-stop io.github.abooodbah.leandoc
adb shell am start-activity -W -n io.github.abooodbah.leandoc/.MainActivity

# Opening styles.docx: force-stop, clear the log, open the file from the
# Files app as in step 2, then read the time the app reports when the
# content is on screen:
adb shell am force-stop io.github.abooodbah.leandoc
adb logcat -c
adb logcat -d | grep "Fully drawn io.github.abooodbah.leandoc"
```

For the baseline, install `empty-app.apk` from the same release (one
empty screen, no libraries; the same app CI subtracts) and time it the same
way:

```sh
adb install empty-app.apk
adb shell am force-stop io.github.abooodbah.leandoc.baseline
adb shell am start-activity -W -n io.github.abooodbah.leandoc.baseline/.EmptyActivity
```

### Scrolling

1. Settings > System > Developer options > Profile HWUI rendering > On
   screen as bars. (Developer options appear after tapping Build number
   seven times in Settings > About phone.)
2. Open `long-300-pages.docx`. Scroll from the first page to page 50 of long-300-pages.docx with quick flicks.
3. Note how often bars cross the green line, and whether anything stutters
   or shows blank areas.

With adb: `adb shell dumpsys gfxinfo io.github.abooodbah.leandoc reset`, scroll,
then `adb shell dumpsys gfxinfo io.github.abooodbah.leandoc` and note "Janky
frames" and "90th percentile".

### Memory

After scrolling `long-300-pages.docx`: `adb shell dumpsys meminfo io.github.abooodbah.leandoc`
(TOTAL PSS), and for the WebView renderer `adb shell dumpsys meminfo | grep -A40 "Total PSS by process" | grep sandboxed_process`.

### What to send back

Phone model, Android version, the three start times and the empty-app time,
what the scrolling bars looked like, and the memory numbers if you used adb.
