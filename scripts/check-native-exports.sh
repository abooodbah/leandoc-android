#!/usr/bin/env bash
# Fails when the release native library exports anything but its JNI entry
# points. Every exported function stays in the library whether the app uses
# it or not, because the linker must assume another library could call it.
# Run after assembleRelease.
set -euo pipefail

ndk=$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)
nm="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm"
status=0
for lib in $(find app/build/intermediates/stripped_native_libs/release -name 'libleandoc.so' | sort); do
  abi=$(basename "$(dirname "$lib")")
  exported=$("$nm" -D --defined-only "$lib" | awk '{ print $NF }' | sort)
  others=$(grep -v -E '^(Java_|JNI_OnLoad$)' <<<"$exported" || true)
  echo "$abi: $(grep -c . <<<"$exported") exported symbols, $(grep -c . <<<"$others") of them not JNI entry points"
  if [ -n "$others" ]; then
    head -n 5 <<<"$others" | sed 's/^/  /'
    status=1
  fi
done
exit $status
