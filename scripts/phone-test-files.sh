#!/usr/bin/env bash
# Writes the documents for the phone test in PROGRESS.md into one zip:
#   scripts/phone-test-files.sh out.zip
# All three come from the desktop repository's tests/fixtures and the
# document its own store screenshots use.
set -euo pipefail
out=$(realpath -m "$1")
fixtures=app/src/androidTest/assets/fixtures
(cd "$fixtures" && zip -q -X -j "$out" features/styles.docx features/long-300-pages.docx store/library-renovation-report.docx)
