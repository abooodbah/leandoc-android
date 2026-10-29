#!/usr/bin/env bash
# Measures release builds on a running emulator and prints a Markdown table.
#
#   scripts/measure.sh empty=path/to/empty.apk base=path/to/base.apk branch=path/to/branch.apk
#
# The instrumented test APK must be installed first: its fixture provider
# serves the files as content:// URIs. "empty" is the baseline module, an app
# with one empty activity; its start time is subtracted, because on an
# emulator without a GPU even an empty app takes over a second. Builds take
# turns, so drift in the emulator affects both alike. Every number comes from
# am start -W, the "Fully drawn" line the app logs through reportFullyDrawn(),
# dumpsys meminfo and dumpsys gfxinfo.
set -euo pipefail

PKG=io.github.abooodbah.leandoc
ACTIVITY=$PKG/.MainActivity
EMPTY_PKG=$PKG.baseline
FIXTURES=content://$PKG.fixtures
SMALL=features/styles.docx
SMALL_NAME="styles.docx"
SMALL_TYPE=application/vnd.openxmlformats-officedocument.wordprocessingml.document
DRAWN_WHAT=document
LARGE=features/long-300-pages.docx
LARGE_NAME="long-300-pages.docx"
LARGE_TYPE=application/vnd.openxmlformats-officedocument.wordprocessingml.document
WEBVIEW=yes
INDEXED=no

ROUNDS=${ROUNDS:-3}
STARTS_PER_ROUND=${STARTS_PER_ROUND:-3}
STARTS=$((ROUNDS * STARTS_PER_ROUND))

declare -a LABELS=()
declare -A APKS=()
EMPTY_APK=""
for argument in "$@"; do
  label="${argument%%=*}"
  if [ "$label" = empty ]; then
    EMPTY_APK="${argument#*=}"
  else
    LABELS+=("$label")
    APKS["$label"]="${argument#*=}"
  fi
done

log() { echo "[measure] $*" >&2; }

install() {
  adb install -r -d "${APKS[$1]}" >/dev/null
}

open_document() {
  adb shell am force-stop "$PKG"
  adb logcat -c
  adb shell am start-activity -W -n "$ACTIVITY" -a android.intent.action.VIEW \
    -d "$FIXTURES/$1" -t "$2" --grant-read-uri-permission | tr -d '\r'
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

minus() {
  if [[ "$1" =~ ^[0-9]+$ && "$2" =~ ^[0-9]+$ ]]; then echo $(($1 - $2)); else echo NA; fi
}

plus() {
  if [[ "$1" =~ ^[0-9]+$ && "$2" =~ ^[0-9]+$ ]]; then echo $(($1 + $2)); else echo NA; fi
}

# The empty app first, on the same emulator: its start time is subtracted.
EMPTY_FIRST=NA
if [ -n "$EMPTY_APK" ]; then
  adb install -r -d "$EMPTY_APK" >/dev/null
  values=""
  for _ in $(seq 0 "$STARTS"); do
    adb shell am force-stop "$EMPTY_PKG"
    total=$(adb shell am start-activity -W -n "$EMPTY_PKG/.EmptyActivity" | tr -d '\r' | sed -n 's/^TotalTime: *//p')
    values+="${total:-NA}"$'\n'
  done
  # The first start after install is a warm-up and is left out.
  EMPTY_FIRST=$(tail -n +2 <<<"$values" | median)
  log "empty app: first frame $EMPTY_FIRST ms (median of $STARTS)"
fi

declare -A FIRST_FRAME=() DRAWN=()
for round in $(seq 1 "$ROUNDS"); do
  for label in "${LABELS[@]}"; do
    install "$label"
    open_document "$SMALL" "$SMALL_TYPE" >/dev/null # Warm-up after install.
    fully_drawn 10 >/dev/null
    for _ in $(seq 1 "$STARTS_PER_ROUND"); do
      total=$(open_document "$SMALL" "$SMALL_TYPE" | sed -n 's/^TotalTime: *//p')
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
  # WebView renderers are isolated processes; their PSS is read from the
  # system-wide "Total PSS by process" list rather than by pid.
  adb shell dumpsys meminfo | tr -d '\r' | awk '
    /Total PSS by process:/ { inside = 1; next }
    inside && /^ *$/ { inside = 0 }
    inside && /sandboxed_process/ { value = $1; gsub(/[,K:]/, "", value); total += value }
    END { print total + 0 }'
}

indexed_ms() {
  local limit=$1 line
  for _ in $(seq 1 $((limit * 10))); do
    line=$(adb logcat -d -s LeanRows:I | grep -o "indexed [0-9]* rows in [0-9]* ms" | head -n 1 || true)
    if [ -n "$line" ]; then
      awk '{ print $5 }' <<<"$line"
      return
    fi
    sleep 0.1
  done
  echo NA
}

renderer_count() {
  adb shell ps -A -o NAME | tr -d '\r' | grep -c sandboxed_process || true
}

# Memory with the small file, to tell the WebView's own cost from the document's.
declare -A SMALL_APP=() SMALL_RENDERER=()
for label in "${LABELS[@]}"; do
  install "$label"
  open_document "$SMALL" "$SMALL_TYPE" >/dev/null
  fully_drawn 30 >/dev/null
  sleep 5
  SMALL_APP[$label]=$(pss_of "$PKG")
  SMALL_RENDERER[$label]=0
  if [ "$WEBVIEW" = yes ]; then SMALL_RENDERER[$label]=$(renderer_pss); fi
  log "$label small file: app ${SMALL_APP[$label]:-NA} KB, renderer ${SMALL_RENDERER[$label]} KB in $(renderer_count) renderer process(es)"
done

# The fixture provider writes generated files into its cache the first time.
open_document "$LARGE" "$LARGE_TYPE" >/dev/null
sleep 20

declare -A APP_PSS=() RENDERER_PSS=() TOTAL_PSS=() LARGE_DRAWN=() INDEXED=() JANK=() FRAMES=() P90=()
for label in "${LABELS[@]}"; do
  install "$label"
  for run in 1 2 3; do
    open_document "$LARGE" "$LARGE_TYPE" >/dev/null
    drawn=$(fully_drawn 90)
    indexed=NA
    if [ "$INDEXED" = yes ]; then indexed=$(indexed_ms 60); fi
    sleep 5
    app=$(pss_of "$PKG")
    renderer=0
    if [ "$WEBVIEW" = yes ]; then renderer=$(renderer_pss); fi
    total=$(plus "${app:-NA}" "$renderer")
    log "$label large run $run: drawn $drawn ms, indexed $indexed ms, app ${app:-NA} KB, renderer $renderer KB in $(renderer_count) process(es), total $total KB"
    LARGE_DRAWN[$label]+="$drawn"$'\n'
    INDEXED[$label]+="$indexed"$'\n'
    APP_PSS[$label]+="${app:-NA}"$'\n'
    RENDERER_PSS[$label]+="$renderer"$'\n'
    TOTAL_PSS[$label]+="$total"$'\n'
  done

  # Scrolling: ten swipes through the large file, as a reader would.
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
  sizes=(); first=(); first_net=(); drawn=(); drawn_net=(); large=(); indexed=(); app=(); renderer=(); total=(); frames=(); jank=(); p90=()
  for label in "${LABELS[@]}"; do
    sizes+=("$(stat -c %s "${APKS[$label]}")")
    f=$(median <<<"${FIRST_FRAME[$label]}")
    d=$(median <<<"${DRAWN[$label]}")
    first+=("$f"); first_net+=("$(minus "$f" "$EMPTY_FIRST")")
    drawn+=("$d"); drawn_net+=("$(minus "$d" "$EMPTY_FIRST")")
    large+=("$(median <<<"${LARGE_DRAWN[$label]}")")
    indexed+=("$(median <<<"${INDEXED[$label]}")")
    app+=("$(median <<<"${APP_PSS[$label]}")")
    renderer+=("$(median <<<"${RENDERER_PSS[$label]}")")
    total+=("$(median <<<"${TOTAL_PSS[$label]}")")
    frames+=("${FRAMES[$label]:-NA}")
    jank+=("${JANK[$label]:-NA}")
    p90+=("${P90[$label]:-NA}")
  done
  row "Release APK, all ABIs (bytes)" "${sizes[@]}"
  row "Cold start to first frame, $SMALL_NAME (ms, median of $STARTS)" "${first[@]}"
  row "**First frame minus the empty app** (ms)" "${first_net[@]}"
  row "Cold start to $DRAWN_WHAT drawn, $SMALL_NAME (ms, median of $STARTS)" "${drawn[@]}"
  row "**Drawn minus the empty app's first frame** (ms)" "${drawn_net[@]}"
  row "$LARGE_NAME drawn after a cold start (ms, median of 3)" "${large[@]}"
  if [ "$INDEXED" = yes ]; then row "Indexing all rows of $LARGE_NAME (ms, median of 3)" "${indexed[@]}"; fi
  small_app=(); small_renderer=(); small_total=()
  for label in "${LABELS[@]}"; do
    small_app+=("${SMALL_APP[$label]:-NA}")
    small_renderer+=("${SMALL_RENDERER[$label]:-NA}")
    small_total+=("$(plus "${SMALL_APP[$label]:-NA}" "${SMALL_RENDERER[$label]:-0}")")
  done
  if [ "$WEBVIEW" = yes ]; then
    row "Total PSS after $SMALL_NAME, app and renderer (KB)" "${small_total[@]}"
  else
    row "App process PSS after $SMALL_NAME (KB)" "${small_app[@]}"
  fi
  row "App process PSS after $LARGE_NAME (KB, median of 3)" "${app[@]}"
  if [ "$WEBVIEW" = yes ]; then
    row "WebView renderer PSS after $LARGE_NAME (KB, median of 3)" "${renderer[@]}"
    row "**Total PSS, app and renderer** (KB, median of 3)" "${total[@]}"
  fi
  row "Frames over 10 swipes (software GPU, not a budget check)" "${frames[@]}"
  row "Janky frames over 10 swipes (software GPU, not a budget check)" "${jank[@]}"
  row "90th percentile frame time (software GPU, not a budget check)" "${p90[@]}"
  echo
  echo "Empty app on the same emulator: $EMPTY_FIRST ms to first frame (median of $STARTS)."
  echo "Start time and jank on this emulator are not the budget check: it has no GPU,"
  echo "and an empty app alone takes the time above. The budget needs a real phone;"
  echo "see the phone test in PROGRESS.md."
}
