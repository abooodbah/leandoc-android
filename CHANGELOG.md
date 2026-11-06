# Changelog

All notable changes to LeanDoc for Android. The project follows semantic
versioning. Each entry is also the text of that version's GitHub release.

## [0.2.0] - 2026-11-06

LeanDoc for Android 0.2.0 looks and works like the other Lean Apps, with
LeanMark's five themes and a settings sheet. It is a pre-release; on Google
Play the app is in a closed test.

### Added

- Five themes: System, Light, Dark, Dim and Dusk, with LeanMark's colours.
  Dim and Dusk are softer dark themes for long reading. The theme button
  shows a dot in the theme's colour, and its name says the theme, for
  example "Theme: System (Dark)".
- A settings button that opens a sheet with the theme, the view, the page
  colour and the zoom.
- The page colour: white like paper (as before), or the theme's colours.
  LeanDoc remembers the choice.
- On a phone the outline opens as a sheet from the bottom edge, with the
  section you are reading marked and kept in view. Before, a phone in
  portrait had no outline. In landscape the outline pane returns.
- Back closes an open sheet before it closes the app.

### Fixed

- When the system switched between light and dark while a document was
  open, LeanDoc read the file again and lost your place. It now keeps the
  document and the place, and only the colours of System change: on the
  emulator 0.3 to 0.4 s to the new colours, against 2.8 to 3.4 s.
- After a long jump through the document the outline marked a section
  several entries away.
- The download from Google Play to an arm64 phone is about 541 KB instead
  of 735 KB, and the APK on GitHub is 1.5 MB instead of 3.4 MB.
- The app no longer keeps a second copy of the open document: about 24 MB
  less memory for a 22.5 MB document full of photos.
- From LeanDoc desktop: Find waits for a pause in typing, zoom steps are
  laid out once, and switching back to a view reuses the parsed document.

### Measured

On an API 34 emulator without a GPU, release builds. Start time, scrolling
and memory on a real phone were not measured; the phone test is in
PROGRESS.md.

### Known gaps

- On Android System WebView older than 128 the outline's marked section can
  move on later than the heading passes.
- The keyboard shortcut list of the desktop is not shown; a hardware
  keyboard's shortcuts still work.

## [0.1.2] - 2026-10-30

- The APK is signed with a release key that stays the same from now on.
  The app itself is unchanged.

## [0.1.1] - 2026-10-29

- A Letter or A4 page fits the phone's width when a document opens, and
  the controls take two rows on a phone.

## [0.1.0] - 2026-10-29

- First pre-release: Word documents (.docx, .dotx, .docm, .dotm) shown with
  LeanDoc desktop 0.1.4's reader, checked and cleaned by the desktop's core.
  No permissions and no network.
