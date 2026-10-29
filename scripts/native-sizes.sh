#!/usr/bin/env bash
# Shows where the bytes of the arm64-v8a native library go, from the
# unstripped copy the build keeps: the C++ runtime that is linked in
# statically, against the app's own code and the libraries it copies from
# the desktop app. Run after assembleRelease.
set -euo pipefail

ndk=$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)
bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
stripped=$(find app/build/intermediates/stripped_native_libs/release -path '*arm64-v8a/*.so' | head -n 1)
full=$(find app/build/intermediates/cxx -path '*obj/arm64-v8a/*.so' | head -n 1)

echo "Shipped (stripped) $(basename "$stripped"): $(stat -c %s "$stripped") bytes"
echo
echo '```'
"$bin/llvm-size" -A "$stripped" | awk '$1 ~ /^\.(text|rodata|data|data\.rel\.ro|eh_frame|eh_frame_hdr|gcc_except_table|dynsym|dynstr|rela\.dyn)$/ { print $1, $2 }'
echo '```'
echo
# Function and data sizes by origin. Names are demangled; libc++ and
# libc++abi live in std:: and __cxxabiv1, or start with __cxa or __gxx.
"$bin/llvm-nm" -C -S --size-sort --defined-only "$full" | python3 -c '
import re, sys
runtime = own = 0
largest = []
pattern = re.compile(r"(^|[^A-Za-z_])std::|__cxxabiv1|^__cxa|^__gxx|^__dynamic_cast|demangle")
for line in sys.stdin:
    parts = line.rstrip("\n").split(" ", 3)
    if len(parts) < 4:
        continue
    size, name = int(parts[1], 16), parts[3]
    if pattern.search(name):
        runtime += size
    else:
        own += size
    largest.append((size, name))
print("| Code and data from | Bytes |")
print("| --- | ---: |")
print(f"| C++ standard library and runtime (libc++, libc++abi; linked statically) | {runtime:,} |")
print(f"| The app and the desktop code it copies | {own:,} |")
print()
print("Largest symbols:")
print()
print("```")
for size, name in sorted(largest)[-15:]:
    print(f"{size:8d} {name[:110]}")
print("```")
'
