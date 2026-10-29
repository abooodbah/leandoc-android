# Progress

Updated with every merge. The next session starts here.

## Done

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

- Release v0.1.0.

## Remaining

- The phone test below, on real hardware.
- Play Console setup and the closed test (see `store/closed-test.md`);
  these need the maintainer's account.

## Known gaps

- On a phone the default 50% zoom leaves a Letter or A4 page slightly wider
  than the screen, so the right edge of each page is cut until the reader
  zooms out. A fit-to-width default would fix it; not done yet.
- The desktop toolbar wraps to three rows on a phone and takes about a
  quarter of the screen.
- Images and documents linked from outside the file are not shown, by
  design: they would need the network.
- The desktop's 0.1.4 reader is not faster than 0.1.3 on the emulator for
  the 300-page document (6,776 against 6,313 ms in one comparison run);
  whether it is on a phone is part of the phone test.
- Release builds from CI are signed with a throwaway key that changes with
  every run, so one release cannot be installed over another; uninstall
  first.
- The emulator has no GPU, so start time and jank from CI are not the
  budget check, and in some runs the scrolling measure did not register
  frames at all.

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
