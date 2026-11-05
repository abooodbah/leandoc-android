#!/usr/bin/env bash
# Fails when the release APK carries files the app never reads: the Kotlin
# compiler's metadata, which only kotlin-reflect uses (the app does not).
# Run after assembleRelease.
set -euo pipefail
apk=app/build/outputs/apk/release/app-release.apk
extras=$(unzip -Z1 "$apk" | grep -E '^kotlin/|^kotlin-tooling-metadata\.json$|^META-INF/.*\.kotlin_module$' || true)
if [ -n "$extras" ]; then
  echo "The release APK carries Kotlin metadata the app does not read:"
  sed 's/^/  /' <<<"$extras"
  exit 1
fi
echo "No Kotlin metadata in the release APK."
