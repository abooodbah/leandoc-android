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
# the action times the page reports (android-host.js), dumpsys meminfo and
# dumpsys gfxinfo.
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
# A valid 24 MB document with two photo-sized pictures, made by the test APK's
# fixture provider.
BIG=large-photos.docx
BIG_NAME="large-photos.docx (24 MB)"
BIG_TYPE=application/vnd.openxmlformats-officedocument.wordprocessingml.document
WEBVIEW=yes
MEASURE_INDEXING=no
TIMING_TAG=LeanDocTiming

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

declare -A FIRST_FRAME=() DRAWN=() PHASES=() PHASE_NAMES=()
for round in $(seq 1 "$ROUNDS"); do
  for label in "${LABELS[@]}"; do
    install "$label"
    open_document "$SMALL" "$SMALL_TYPE" >/dev/null # Warm-up after install.
    fully_drawn 10 >/dev/null
    for _ in $(seq 1 "$STARTS_PER_ROUND"); do
      total=$(open_document "$SMALL" "$SMALL_TYPE" | sed -n 's/^TotalTime: *//p')
      drawn=$(fully_drawn 10)
      log "round $round $label: first frame ${total:-NA} ms, drawn $drawn ms"
      # The app's own "phase NAME N ms" lines, from creation to each step.
      while read -r name value; do
        [ -n "$name" ] || continue
        PHASES["$label|$name"]+="$value"$'\n'
        PHASE_NAMES["$name"]=1
      done < <(adb logcat -d -v raw -s "${TIMING_TAG:-none}:I" | sed -n 's/^phase \(.*\) \([0-9][0-9]*\) ms$/\1 \2/p' | sed 's/ \([0-9][0-9]*\)$/|\1/' | tr ' ' '_' | tr '|' ' ')
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

java_heap_of() {
  # "Java Heap:" in the App Summary, in KB.
  adb shell dumpsys meminfo "$1" | tr -d '\r' | awk '/^ *Java Heap:/ { print $3; exit }'
}

# The App Summary rows, "Java Heap: 123 Native Heap: 456 ...", in KB.
app_summary_of() {
  adb shell dumpsys meminfo "$1" | tr -d '\r' | awk '
    /App Summary/ { inside = 1; next }
    inside && /^ *(Java Heap|Native Heap|Code|Stack|Graphics|Private Other|System):/ {
      name = $0; sub(/:.*/, "", name); gsub(/^ +/, "", name)
      value = $0; sub(/^[^:]*: */, "", value); split(value, parts, " ")
      printf "%s %s, ", name, parts[1]
    }
    inside && /TOTAL PSS/ { exit }'
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

# Memory with a large valid document: what the app and the renderer keep.
# The fixture provider makes the file into its cache the first time.
open_document "$BIG" "$BIG_TYPE" >/dev/null
fully_drawn 120 >/dev/null
sleep 5
declare -A BIG_APP=() BIG_JAVA=() BIG_TOTAL=() BIG_DRAWN=()
for label in "${LABELS[@]}"; do
  install "$label"
  open_document "$BIG" "$BIG_TYPE" >/dev/null
  BIG_DRAWN[$label]=$(fully_drawn 120)
  sleep 8
  BIG_APP[$label]=$(pss_of "$PKG")
  BIG_JAVA[$label]=$(java_heap_of "$PKG")
  renderer=0
  if [ "$WEBVIEW" = yes ]; then renderer=$(renderer_pss); fi
  BIG_TOTAL[$label]=$(plus "${BIG_APP[$label]:-NA}" "$renderer")
  log "$label big file: drawn ${BIG_DRAWN[$label]} ms, app ${BIG_APP[$label]:-NA} KB, Java heap ${BIG_JAVA[$label]:-NA} KB, renderer $renderer KB"
  log "$label big file, app summary in KB: $(app_summary_of "$PKG")"
done

declare -A APP_PSS=() RENDERER_PSS=() TOTAL_PSS=() LARGE_DRAWN=() INDEXED=() JANK=() FRAMES=() P90=()
for label in "${LABELS[@]}"; do
  install "$label"
  for run in 1 2 3; do
    open_document "$LARGE" "$LARGE_TYPE" >/dev/null
    drawn=$(fully_drawn 90)
    indexed=NA
    if [ "$MEASURE_INDEXING" = yes ]; then indexed=$(indexed_ms 60); fi
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

  # Scrolling: ten swipes through the large file, as a reader would. adb input
  # goes to the window with focus. When the emulator's launcher has kept focus
  # in front of the app, the swipes would miss it, so nothing is reported.
  focus=$(adb shell dumpsys window | tr -d '\r' | grep -m 1 'mCurrentFocus=' || true)
  if ! grep -q "$PKG" <<<"$focus"; then
    log "$label scrolling not measured, another window has focus: $focus"
    FRAMES[$label]="not measured"
    JANK[$label]="not measured (app not in front)"
    P90[$label]=NA
    continue
  fi
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

# Reader actions on the large document, as keys from a hardware keyboard:
# each time is what the page reports from the key (or typed letter) to the
# frame that shows the result. View switches use the reader's own render
# time. The app's data is cleared first, so every build starts in the System
# theme, page view and automatic zoom.
timing_lines() {
  adb logcat -d -v raw -s "$TIMING_TAG:I" | tr -d '\r'
}

action_ms() {
  sed -n "s/^action $1 \([0-9][0-9]*\) ms$/\1/p"
}

key() {
  adb shell input keycombination "$@" >/dev/null 2>&1 || log "input keycombination $* failed"
}

wait_renders() {
  local want=$1
  for _ in $(seq 1 600); do
    if [ "$(timing_lines | grep -c '^rendered ')" -ge "$want" ]; then return; fi
    sleep 0.1
  done
}

declare -A ZOOM_MS=() THEME_MS=() FIND_MS=() FIND_MAX=() VIEW_MS=() RESIZE_MS=() NIGHT_MS=()
for label in "${LABELS[@]}"; do
  install "$label"
  adb shell pm clear "$PKG" >/dev/null || log "pm clear failed"
  open_document "$LARGE" "$LARGE_TYPE" >/dev/null
  fully_drawn 90 >/dev/null
  sleep 5
  focus=$(adb shell dumpsys window | tr -d '\r' | grep -m 1 'mCurrentFocus=' || true)
  if ! grep -q "$PKG" <<<"$focus"; then
    log "$label actions not measured, another window has focus: $focus"
    continue
  fi
  # In one run the first keys after the start reached the page but no time
  # came back; each step is tried a second time when it reports nothing.
  for attempt in 1 2; do
    adb logcat -c
    for keys in "KEYCODE_EQUALS" "KEYCODE_EQUALS" "KEYCODE_MINUS" "KEYCODE_MINUS"; do
      key KEYCODE_CTRL_LEFT $keys
      sleep 3
    done
    ZOOM_MS[$label]=$(timing_lines | action_ms zoom | median)
    [ "${ZOOM_MS[$label]}" = NA ] || break
    log "$label zoom: no times on attempt $attempt"
  done
  # Back to the automatic zoom, which follows a rotation.
  key KEYCODE_CTRL_LEFT KEYCODE_0
  sleep 3
  for attempt in 1 2; do
    adb logcat -c
    for _ in 1 2 3; do
      key KEYCODE_CTRL_LEFT KEYCODE_SHIFT_LEFT KEYCODE_T
      sleep 3
    done
    THEME_MS[$label]=$(timing_lines | action_ms theme | median)
    [ "${THEME_MS[$label]}" = NA ] || break
    log "$label theme: no times on attempt $attempt"
  done
  for attempt in 1 2; do
    key KEYCODE_CTRL_LEFT KEYCODE_F
    sleep 2
    adb logcat -c
    for letter in s e c t i o n; do
      adb shell input text "$letter" || log "input text failed"
      sleep 2
    done
    FIND_MS[$label]=$(timing_lines | action_ms find | median)
    FIND_MAX[$label]=$(timing_lines | action_ms find | sort -n | tail -n 1)
    adb shell input keyevent KEYCODE_ESCAPE || true
    sleep 2
    [ "${FIND_MS[$label]}" = NA ] || break
    log "$label find: no times on attempt $attempt"
  done
  adb logcat -c
  key KEYCODE_CTRL_LEFT KEYCODE_SHIFT_LEFT KEYCODE_V
  wait_renders 1
  sleep 3
  key KEYCODE_CTRL_LEFT KEYCODE_SHIFT_LEFT KEYCODE_V
  wait_renders 2
  VIEW_MS[$label]=$(timing_lines | sed -n 's/^rendered [0-9]* pages in \([0-9][0-9]*\) ms$/\1/p' | median)
  sleep 3
  adb shell settings put system accelerometer_rotation 0
  adb logcat -c
  adb shell settings put system user_rotation 1
  sleep 6
  adb shell settings put system user_rotation 0
  sleep 6
  RESIZE_MS[$label]=$(timing_lines | action_ms resize | median)
  # The system's dark mode switched while reading, as at sunset: Android
  # creates the activity again, which reads, cleans and draws the file
  # again. Timed from the new activity's creation to the drawn document, as
  # the app logs it.
  adb logcat -c
  adb shell cmd uimode night yes >/dev/null 2>&1 || log "cmd uimode failed"
  NIGHT_MS[$label]=NA
  for _ in $(seq 1 900); do
    night=$(timing_lines | sed -n 's/^phase drawn \([0-9][0-9]*\) ms$/\1/p' | head -n 1)
    if [ -n "$night" ]; then NIGHT_MS[$label]=$night; break; fi
    sleep 0.1
  done
  sleep 3
  adb shell cmd uimode night no >/dev/null 2>&1 || true
  sleep 5
  log "$label actions: zoom ${ZOOM_MS[$label]:-NA} ms, theme ${THEME_MS[$label]:-NA} ms, find ${FIND_MS[$label]:-NA} ms (max ${FIND_MAX[$label]:-NA}), view ${VIEW_MS[$label]:-NA} ms, rotation ${RESIZE_MS[$label]:-NA} ms, night mode ${NIGHT_MS[$label]:-NA} ms"
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
  if [ "$MEASURE_INDEXING" = yes ]; then row "Indexing all rows of $LARGE_NAME (ms, median of 3)" "${indexed[@]}"; fi
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
  big_app=(); big_java=(); big_total=(); big_drawn=()
  zoom=(); theme=(); find=(); find_max=(); view=(); resize=(); night=()
  for label in "${LABELS[@]}"; do
    big_app+=("${BIG_APP[$label]:-NA}"); big_java+=("${BIG_JAVA[$label]:-NA}")
    big_total+=("${BIG_TOTAL[$label]:-NA}"); big_drawn+=("${BIG_DRAWN[$label]:-NA}")
    zoom+=("${ZOOM_MS[$label]:-NA}"); theme+=("${THEME_MS[$label]:-NA}")
    find+=("${FIND_MS[$label]:-NA}"); find_max+=("${FIND_MAX[$label]:-NA}")
    view+=("${VIEW_MS[$label]:-NA}"); resize+=("${RESIZE_MS[$label]:-NA}"); night+=("${NIGHT_MS[$label]:-NA}")
  done
  row "$BIG_NAME drawn after a cold start (ms)" "${big_drawn[@]}"
  row "App process PSS after $BIG_NAME (KB)" "${big_app[@]}"
  row "App Java heap after $BIG_NAME (KB)" "${big_java[@]}"
  if [ "$WEBVIEW" = yes ]; then
    row "**Total PSS after $BIG_NAME, app and renderer** (KB)" "${big_total[@]}"
  fi
  row "Zoom step on $LARGE_NAME (ms to the frame, median of 4)" "${zoom[@]}"
  row "Theme switch on $LARGE_NAME (ms to the frame, median of 3)" "${theme[@]}"
  row "Find, one typed letter, $LARGE_NAME (ms to the frame, median of 7)" "${find[@]}"
  row "Find, slowest letter of the 7 (ms)" "${find_max[@]}"
  row "View switch on $LARGE_NAME (render ms, median of 2)" "${view[@]}"
  row "Rotation on $LARGE_NAME (ms to the frame with the new zoom, median of 2)" "${resize[@]}"
  row "System dark mode switched on $LARGE_NAME (ms from the new activity to the document drawn again)" "${night[@]}"
  row "Frames over 10 swipes (software GPU, not a budget check)" "${frames[@]}"
  row "Janky frames over 10 swipes (software GPU, not a budget check)" "${jank[@]}"
  row "90th percentile frame time (software GPU, not a budget check)" "${p90[@]}"
  if [ "${#PHASE_NAMES[@]}" -gt 0 ]; then
    echo
    echo "Where the time goes when $SMALL_NAME is opened: milliseconds from the activity's"
    echo "creation to each step, as the app logs them (median of $STARTS)."
    echo
    printf '| Step |'; for label in "${LABELS[@]}"; do printf ' %s |' "$label"; done; printf '\n'
    printf '| --- |'; for _ in "${LABELS[@]}"; do printf ' ---: |'; done; printf '\n'
    for name in "webview_created" "page_loaded" "reader_ready" "document_sent" "rendered" "drawn"; do
      [ -n "${PHASE_NAMES[$name]:-}" ] || continue
      printf '| %s |' "${name//_/ }"
      for label in "${LABELS[@]}"; do printf ' %s |' "$(median <<<"${PHASES[$label|$name]:-}")"; done
      printf '\n'
    done
  fi
  echo
  echo "Empty app on the same emulator: $EMPTY_FIRST ms to first frame (median of $STARTS)."
  echo "Start time and jank on this emulator are not the budget check: it has no GPU,"
  echo "and an empty app alone takes the time above. The budget needs a real phone;"
  echo "see the phone test in PROGRESS.md."
}
