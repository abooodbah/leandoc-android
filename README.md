# LeanDoc for Android

A read-only viewer for Word documents (.docx, .dotx, .docm, .dotm) on
Android. It is the Android companion to the desktop app at
https://github.com/abooodbah/leandoc and shows documents with the same
reader page.

Work in progress. Not yet on Google Play.

## Building

You need JDK 17 and the Android SDK (platform 36). Then:

```sh
./gradlew lint testDebugUnitTest assembleRelease bundleRelease
```

Instrumented tests need a running emulator or a connected device:

```sh
./gradlew connectedDebugAndroidTest
```

CI runs all of these on every pull request, with the instrumented tests on
API 26 and API 34 emulators.

## Privacy

The app requests no permissions, including no network permission. Files are
read with the access the opening app grants and are never changed. Macros
are never run.
