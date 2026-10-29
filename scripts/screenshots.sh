#!/usr/bin/env bash
# Captures the store screenshots on a running emulator:
#
#   scripts/screenshots.sh light|dark
#
# The debug app and the instrumented test APK are installed first; the test
# APK's file provider serves the document as a content:// URI, the way a
# file manager shares it. The status bar is put in demo mode so it shows a
# fixed time and no notifications.
set -euo pipefail

PKG=io.github.abooodbah.leandoc
FIXTURES=content://io.github.abooodbah.leandoc.fixtures
DOCUMENT=store/library-renovation-report.docx
TYPE=application/vnd.openxmlformats-officedocument.wordprocessingml.document
theme=$1

mkdir -p screenshots
adb install -r app/build/outputs/apk/debug/app-debug.apk >/dev/null
adb install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk >/dev/null

adb shell cmd uimode night "$([ "$theme" = dark ] && echo yes || echo no)"
adb shell settings put global sysui_demo_allowed 1
demo() { adb shell am broadcast -a com.android.systemui.demo -e command "$@" >/dev/null; }
demo enter
demo clock -e hhmm 0900
demo battery -e level 100 -e plugged false
demo network -e wifi hide -e mobile hide -e airplane hide
demo notifications -e visible false

adb shell am force-stop "$PKG"
adb shell am start-activity -W -n "$PKG/.MainActivity" -a android.intent.action.VIEW \
  -d "$FIXTURES/$DOCUMENT" -t "$TYPE" --grant-read-uri-permission >/dev/null
# Rendering under the emulator's software GPU is slow; wait for it to settle.
sleep 15
adb exec-out screencap -p > "screenshots/$theme.png"
echo "captured screenshots/$theme.png"
