#!/usr/bin/env bash
# Puts the store screenshots back together from the check runs the
# screenshots CI job attached to a commit, and checks their SHA-256.
#   scripts/fetch-screenshots.sh <commit> [owner/repo]
set -euo pipefail
sha=$1
repo=${2:-$(git remote get-url origin | sed -E 's#.*github.com[:/]##; s#\.git$##')}
out=store/screenshots
mkdir -p "$out"
runs=$(gh api --paginate "repos/$repo/commits/$sha/check-runs?per_page=100" \
  --jq '.check_runs[] | select(.name | startswith("screenshot ")) | [.name, (.id|tostring)] | @tsv')
for name in $(cut -f1 <<<"$runs" | awk '{ print $2 }' | sort -u); do
  : > "$out/$name.b64"
  count=$(grep -P "^screenshot \Q$name\E " <<<"$runs" | head -n 1 | awk '{ print $3 }' | cut -d/ -f2)
  for index in $(seq 1 "$count"); do
    id=$(grep -P "^screenshot \Q$name\E $index/$count\t" <<<"$runs" | cut -f2 | tail -n 1)
    gh api "repos/$repo/check-runs/$id" --jq '.output.text' | tr -d '\n' >> "$out/$name.b64"
    sum=$(gh api "repos/$repo/check-runs/$id" --jq '.output.summary' | awk '{ print $2 }')
  done
  base64 -d "$out/$name.b64" > "$out/$name"
  rm "$out/$name.b64"
  actual=$(sha256sum "$out/$name" | cut -d' ' -f1)
  if [ "$actual" != "$sum" ]; then echo "$name: sha256 $actual, expected $sum" >&2; exit 1; fi
  echo "$out/$name $(stat -c %s "$out/$name") bytes, sha256 ok"
done
