#!/usr/bin/env bash
# Measures release builds on a running emulator and prints a Markdown table.
#
#   scripts/measure.sh base=path/to/base.apk branch=path/to/branch.apk
#
# The instrumented test APK must be installed first: its fixture provider
# serves the documents as content:// URIs. Builds take turns, so drift in the
# emulator affects both alike. Every number comes from am start -W, the
# "Fully drawn" line the app logs through reportFullyDrawn(), dumpsys meminfo
# and dumpsys gfxinfo.
set -euo pipefail

PKG=io.github.abooodbah.leandoc
ACTIVITY=$PKG/.MainActivity
FIXTURES=content://io.github.abooodbah.leandoc.fixtures
SMALL=features/styles.docx
LARGE=features/long-300-pages.docx
TYPE=application/vnd.openxmlformats-officedocument.wordprocessingml.document
ROUNDS=${ROUNDS:-3}
STARTS_PER_ROUND=${STARTS_PER_ROUND:-3}

declare -a LABELS=()
declare -A APKS=()
for argument in "$@"; do
  LABELS+=("${argument%%=*}")
  APKS["${argument%%=*}"]="${argument#*=}"
done

log() { echo "[measure] $*" >&2; }

install() {
  adb install -r -d "${APKS[$1]}" >/dev/null
}

open_document() {
  adb shell am force-stop "$PKG"
  adb logcat -c
  adb shell am start-activity -W -n "$ACTIVITY" -a android.intent.action.VIEW \
    -d "$FIXTURES/$1" -t "$TYPE" --grant-read-uri-permission | tr -d '\r'
}

# Milliseconds from "+1s234ms" or "+987ms".
to_ms() {
  sed -E 's/^\+//; s/^([0-9]+)s([0-9]+)ms$/\1 \2/; s/^([0-9]+)ms$/0 \1/' <<<"$1" |
    awk '{ print $1 * 1000 + $2 }'
}

# Waits for the app's reportFullyDrawn() and prints its time, or NA.
fully_drawn() {
  local limit=$1 line
  for _ in $(seq 1 $((limit * 10))); do
    line=$(adb logcat -d | grep -o "Fully drawn $PKG/\.MainActivity: +[0-9a-z]*" | head -n 1 || true)
    if [ -n "$line" ]; then
      to_ms "${line##*: }"
      return
    fi
    sleep 0.1
  done
  echo NA
}

median() {
  local values
  values=$(grep -E '^[0-9]+$' | sort -n || true)
  if [ -z "$values" ]; then echo NA; return; fi
  awk '{ v[NR] = $1 } END { if (NR % 2) print v[(NR + 1) / 2]; else print int((v[NR / 2] + v[NR / 2 + 1]) / 2) }' <<<"$values"
}

declare -A FIRST_FRAME=() DRAWN=()
for round in $(seq 1 "$ROUNDS"); do
  for label in "${LABELS[@]}"; do
    install "$label"
    open_document "$SMALL" >/dev/null # Warm-up after install.
    fully_drawn 10 >/dev/null
    for _ in $(seq 1 "$STARTS_PER_ROUND"); do
      total=$(open_document "$SMALL" | sed -n 's/^TotalTime: *//p')
      drawn=$(fully_drawn 10)
      log "round $round $label: first frame ${total:-NA} ms, drawn $drawn ms"
      FIRST_FRAME[$label]+="${total:-NA}"$'\n'
      DRAWN[$label]+="$drawn"$'\n'
    done
  done
done

pss_of() {
  # The summary line reads "TOTAL PSS: 12345 ..." on current Android and the
  # table row "TOTAL 12345 ..." on older versions.
  adb shell dumpsys meminfo "$1" | tr -d '\r' |
    awk '/TOTAL PSS:/ { print $3; exit } /^ *TOTAL +[0-9]/ { print $2; exit }'
}

renderer_pss() {
  local total=0 pid value
  for pid in $(adb shell ps -A -o PID,NAME | tr -d '\r' | awk '/sandboxed_process/ { print $1 }'); do
    value=$(pss_of "$pid")
    total=$((total + ${value:-0}))
  done
  echo "$total"
}

declare -A APP_PSS=() RENDERER_PSS=() JANK=() FRAMES=() P90=() SCREENS=()
for label in "${LABELS[@]}"; do
  install "$label"
  for run in 1 2 3; do
    open_document "$LARGE" >/dev/null
    drawn=$(fully_drawn 120)
    sleep 5
    app=$(pss_of "$PKG")
    renderer=$(renderer_pss)
    log "$label memory run $run: drawn $drawn ms, app ${app:-NA} KB, renderer $renderer KB"
    APP_PSS[$label]+="${app:-NA}"$'\n'
    RENDERER_PSS[$label]+="$renderer"$'\n'
  done

  # Scrolling: ten swipes through the long document, as a reader would.
  adb shell dumpsys gfxinfo "$PKG" reset >/dev/null
  for _ in $(seq 1 10); do
    adb shell input swipe 540 1800 540 600 250
    sleep 0.5
  done
  gfx=$(adb shell dumpsys gfxinfo "$PKG" | tr -d '\r')
  FRAMES[$label]=$(sed -n 's/^Total frames rendered: *//p' <<<"$gfx" | head -n 1)
  JANK[$label]=$(sed -n 's/^Janky frames: *//p' <<<"$gfx" | head -n 1)
  P90[$label]=$(sed -n 's/^90th percentile: *//p' <<<"$gfx" | head -n 1)
  log "$label scrolling: frames ${FRAMES[$label]:-NA}, janky ${JANK[$label]:-NA}, p90 ${P90[$label]:-NA}"
done

row() {
  local name=$1; shift
  printf '| %s |' "$name"
  for value in "$@"; do printf ' %s |' "${value:-NA}"; done
  printf '\n'
}

{
  printf '| Measure |'; for label in "${LABELS[@]}"; do printf ' %s |' "$label"; done; printf '\n'
  printf '| --- |'; for _ in "${LABELS[@]}"; do printf ' ---: |'; done; printf '\n'
  sizes=(); first=(); drawn=(); app=(); renderer=(); frames=(); jank=(); p90=()
  for label in "${LABELS[@]}"; do
    sizes+=("$(stat -c %s "${APKS[$label]}")")
    first+=("$(median <<<"${FIRST_FRAME[$label]}")")
    drawn+=("$(median <<<"${DRAWN[$label]}")")
    app+=("$(median <<<"${APP_PSS[$label]}")")
    renderer+=("$(median <<<"${RENDERER_PSS[$label]}")")
    frames+=("${FRAMES[$label]:-NA}")
    jank+=("${JANK[$label]:-NA}")
    p90+=("${P90[$label]:-NA}")
  done
  row "Release APK (bytes)" "${sizes[@]}"
  row "Cold start to first frame, styles.docx (ms, median of $((ROUNDS * STARTS_PER_ROUND)))" "${first[@]}"
  row "Cold start to document drawn, styles.docx (ms, median of $((ROUNDS * STARTS_PER_ROUND)))" "${drawn[@]}"
  row "App PSS after long-300-pages.docx (KB, median of 3)" "${app[@]}"
  row "WebView renderer PSS after long-300-pages.docx (KB, median of 3)" "${renderer[@]}"
  row "Frames rendered over 10 swipes" "${frames[@]}"
  row "Janky frames over 10 swipes" "${jank[@]}"
  row "90th percentile frame time" "${p90[@]}"
}
