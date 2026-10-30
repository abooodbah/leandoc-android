# LeanDoc for Android

A read-only viewer for Word documents (.docx, .dotx, .docm, .dotm) on
Android. It is the Android companion to the desktop app at
https://github.com/abooodbah/leandoc and shows documents with the same
reader page.

Work in progress. Not yet on Google Play.

## Try it (closed test)

LeanDoc for Android is in a closed test on Google Play before its public release.
To try it, follow the steps below or on the [LeanDoc for Android site](https://abooodbah.github.io/leandoc-android/):

1. Join the [Lean Apps testers group](https://groups.google.com/g/lean-apps-testers) with the Google account you use
   on your phone.
2. On your phone, open the [LeanDoc tester page](https://play.google.com/apps/testing/io.github.abooodbah.leandoc), tap
   **Become a tester**, then install LeanDoc from Google Play.

If the tester page says the app is not available yet, Google is still reviewing
the test; try again a day later. Google asks testers to keep the app installed
for 14 days. The APK is also attached to each
[Android release](https://github.com/abooodbah/leandoc-android/releases).

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

## Signing for Google Play

Release builds from CI are signed with a throwaway debug key, so they install
for testing. Google Play needs a bundle signed with your own upload key. The
key stays on your computer: never commit it, and never put its password in
this repository. `.gitignore` already excludes `*.jks`, `*.keystore` and
`keystore.properties`.

1. Create the upload key once. `keytool` comes with the JDK:

   ```sh
   keytool -genkeypair -v -keystore ~/keys/leandoc-upload.jks \
     -alias upload -keyalg RSA -keysize 4096 -validity 10000
   ```

   Keep a backup of the file and its password somewhere safe. If you lose it,
   Google can reset the upload key, but that takes a support request.

2. Tell Gradle where it is. Create `keystore.properties` in the repository
   root (it is ignored by git):

   ```properties
   storeFile=/home/you/keys/leandoc-upload.jks
   storePassword=...
   keyAlias=upload
   keyPassword=...
   ```

3. Build the signed bundle:

   ```sh
   ./gradlew :app:bundleRelease
   ```

   The file is `app/build/outputs/bundle/release/app-release.aab`.

4. In Play Console, create the app, accept Play App Signing, and upload that
   bundle. Google keeps the app signing key; your key only proves uploads come
   from you.

The bundle attached to a GitHub release is signed with CI's throwaway key,
so build the one for Play yourself as above.

## Store listing

`store/` holds what Play Console asks for: the short and full
descriptions, the data safety and content rating answers, the closed-test
plan (`store/closed-test.md`), and light and dark screenshots taken on an
emulator by the `screenshots` CI job (`scripts/screenshots.sh`).
