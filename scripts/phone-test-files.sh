#!/usr/bin/env bash
# Writes the documents for the phone test in PROGRESS.md into one zip:
#   scripts/phone-test-files.sh out.zip
# They come from the desktop repository's tests/fixtures, the document its
# own store screenshots use, and the two-column paper its reader tests
# build (fixtures/reader/).
set -euo pipefail
out=$(realpath -m "$1")
fixtures=app/src/androidTest/assets/fixtures
(cd "$fixtures" && zip -q -X -j "$out" features/styles.docx features/long-300-pages.docx store/library-renovation-report.docx \
  reader/two-column-floats.docx)
