# Progress

Updated with every merge. The next session starts here.

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
| 1 | Let go of the package once the page has it | next |
| 2 | #19 The outline as a sheet on phones; the highlight after a long jump | merged |
| 3 | Five themes and the theme dot; settings button and sheet | planned |
| 4 | Port LeanDoc desktop 0.2.0's reader | waits for the desktop merge |
| 5 | Release 0.2.0 | last |

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
| The app keeps the sanitized package after the page fetched it | The document's size in the Java heap for as long as it is open; the page keeps its own copy | Java heap 25,120 KB, app PSS 151,833 KB after the 22.5 MB `large-photos.docx` | Drop it once served; a new page reads the file again | next pull request |
| Open: WebView, page and document | Cold start to the first page | First frame 145 ms over the empty app; drawn 1,282 ms over its first frame (`styles.docx`); 300 pages drawn in 3,634 ms. WebView created at 211 ms, page loaded at 677, drawn at 1,597 | No clear win: the page's scripts load in about 470 ms and docx-preview renders in about 900. Budget is for a phone (under 1 s); see the phone test | for you to decide |
| Scroll | The reader's scroll handler filters the outline entries and reads page boxes on every scroll event | 87 to 90% janky frames on the software GPU, the same on main and branch | None measurable without a GPU | phone test |
| Find on every keystroke (desktop's known issue) | Searches the whole document per letter | 23 ms per letter (median of 7), slowest 32 ms, 300 pages | Small here; the desktop's fix comes with the port | after the port |
| Zoom step (desktop's known issue) | Lays out the document again | 34 ms to the frame (median of 4); pages out of view are skipped (content-visibility) | None needed on Android | none |
| View switch (desktop's known issue) | On Android the page keeps the package and parses it again; the app does not read the file again | 838 ms render (median of 2, page and reading view, 300 pages) | The desktop's fix comes with the port | after the port |
| Theme switch | Restyles the whole page | 84 ms to the frame (median of 3) | None | none |
| Rotation | The activity is kept; the fit zoom changes and the page is laid out again after the reader's 150 ms settle | 340 ms to the frame with the new zoom (median of 2) | None | none |
| System dark mode switch while reading | The activity is created again: the file is read and cleaned again, the page reloads, and the reading position is lost | not measured yet | Keep the WebView, or keep the position | to measure |
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
- #19: on a phone the outline opens as a sheet from the bottom edge (the
  desktop page hides it below 760 px), with the current section highlighted
  and kept in view; a tapped entry jumps and closes it; back, the scrim,
  Escape and the close button close it; in landscape the pane returns with
  the current entry in view. After a long jump the highlight is checked
  again once scrolling stops (it stayed several sections off). On a phone
  "Open in another app" is an icon. The phone layout moved to `android.css`.

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

- Round 0.2.0, above.

## Remaining

- The phone test below, on real hardware.
- Play Console setup and the closed test (see `store/closed-test.md`);
  these need the maintainer's account.

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
  zoomed page are reported unzoomed. The reader compares them with the
  document area's edge, so there the highlight can lag the page by a
  heading or two at a low zoom. WebView 128 and later are not affected.

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
