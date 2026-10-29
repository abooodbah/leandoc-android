# Progress

Updated with every merge. The next session starts here.

## Done

- Gradle project: Kotlin, classic Views, no AndroidX in the app.
  compileSdk and targetSdk 36 (Google Play requires 36 for new apps from
  31 August 2026), minSdk 26.
- Manifest: no permissions, open-with filters for .docx, .dotx, .docm and
  .dotm (VIEW and SEND), WebView metrics and Safe Browsing turned off.
- CI: lint, JVM tests, release APK and bundle, a check that the release APK
  requests no permissions, and instrumented tests on API 26 and API 34.

## In progress

- The reader: the desktop page (docx-preview) in a WebView, with the
  desktop's C++ core sanitizing each document first.

## Remaining

- Page view and reading view, the fidelity bar.
- Budgets measured on the emulator: start time, size, memory, jank.
- Store texts, screenshots, closed-test plan, signing steps.
- Release v0.1.0.

## Known gaps

- None recorded yet.

## Measured numbers

- None yet.
