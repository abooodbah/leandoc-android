# Changelog

All notable changes to LeanDoc for Android. The project follows semantic
versioning. Each entry is also the text of that version's GitHub release.

## [0.3.0] - 2026-11-18

LeanDoc for Android 0.3.0 lays pages out as LeanDoc desktop 0.3.2 does,
closer to Microsoft Word. It is a pre-release; on Google Play the app is in a
closed test.

### Changed

- From LeanDoc desktop 0.3.0: tables, figures and text boxes that Word floats
  at the top or foot of a page sit there, across the columns, and text no
  longer runs over them.
- Two-column papers keep their two columns, and their references keep a
  hanging indent.
- Contents pages keep their dot leaders and right-aligned page numbers.
- Numbered headings take Word's indents. A section without a header of its
  own takes the previous one. A footnote with its own mark shows that mark.
  Long equations wrap inside their column. A page that starts inside a word
  ends with a hyphen. Small capitals are drawn at Word's size.
- Files saved in Word's Strict format keep their styles, lists and notes.
- From LeanDoc desktop 0.3.1: fewer near-empty extra pages.

### Fixed

- At the zoom that fits a page to the phone, the last line of a column or
  the last row of a table could be cut off at the page's edge. The page now
  grows to hold it.

### Added

- The APK and the bundle of each release carry a build provenance
  attestation. Check a download with
  `gh attestation verify <file> --repo abooodbah/leandoc-android`. It shows
  where a file was built; it is not code signing.

### Measured

On an API 34 emulator without a GPU, release builds taking turns. With the
desktop 0.3.2 reader, against 0.2.0: opening 30 test documents took between
0.5% less and 2.1% more time in four runs (two runs of one build differ by
up to 2.7%), and memory after the 300-page document is about 3 MB (2%)
higher. Keeping text on the page added nothing measurable (0.8% less in one
run). The APK is 1,568,805 bytes, 24,976 more than 0.2.0. Start time,
scrolling and memory on a real phone were not measured; the phone test is
in PROGRESS.md.

### Known gaps

- Android draws Times New Roman in the phone's own serif font, which is
  wider, so a page can hold less than in Word: the desktop's two-column test
  paper takes 5 pages where Word gives 4.
- A contents entry that nearly fills its line can wrap, and its page number
  then stops short of the right edge.
- On Android System WebView older than 128 the outline's marked section can
  move on later than the heading passes.
- The keyboard shortcut list of the desktop is not shown; a hardware
  keyboard's shortcuts still work.

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
