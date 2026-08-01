#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 ANDROID_DEVELOPMENT_ROOT" >&2
  exit 2
fi

android_root=$(realpath "$1")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bundle="$repo_root/patches/lineage-18.1"
series="$bundle/series.tsv"

while IFS=$'\t' read -r order project _upstream _branch base patch; do
  [[ -z "$order" || "$order" == \#* ]] && continue
  project_root="$android_root/$project"
  git -C "$project_root" rev-parse --git-dir >/dev/null 2>&1 || {
    echo "Missing Git project: $project" >&2
    exit 1
  }
  git -C "$project_root" merge-base --is-ancestor "$base" HEAD || {
    echo "$project HEAD does not descend from recorded base $base" >&2
    exit 1
  }

  alternate_index=$(mktemp)
  rm -f "$alternate_index"
  trap 'rm -f "${alternate_index:-}"' EXIT
  (
    cd "$project_root"
    GIT_INDEX_FILE="$alternate_index" git read-tree HEAD
    GIT_INDEX_FILE="$alternate_index" git add -A -- .
    GIT_INDEX_FILE="$alternate_index" \
      git diff --cached --binary --full-index --no-ext-diff "$base" -- \
      > "$bundle/$patch"
  )
  rm -f "$alternate_index"
  alternate_index=
  [[ -s "$bundle/$patch" ]] || {
    echo "Generated empty patch for $project" >&2
    exit 1
  }
  printf 'generated %s\n' "$patch"
done < "$series"

(
  cd "$bundle"
  sha256sum ./*.patch | sed 's#  \./#  #' > SHA256SUMS
  sha256sum -c SHA256SUMS
)
