#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 ANDROID_DEVELOPMENT_ROOT" >&2
  exit 2
fi

android_root=$(realpath "$1")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bundle="$repo_root/patches/lineage-18.1"
bundle_parent=$(dirname "$bundle")
series="$bundle/series.tsv"
staging=$(mktemp -d "$bundle_parent/.lineage-18.1.new.XXXXXX")
backup=
alternate_index=

cleanup() {
  local status=$1
  rm -f "${alternate_index:-}"
  rm -rf "${staging:-}"
  if [[ -n "${backup:-}" && -d "$backup" ]]; then
    rm -rf "$bundle"
    mv "$backup" "$bundle"
  fi
  return "$status"
}
trap 'cleanup $?' EXIT
trap 'exit 130' HUP INT TERM

[[ -f "$series" ]] || {
  echo "Missing patch series: $series" >&2
  exit 1
}
cp "$series" "$bundle/README.md" "$staging/"

# Check every source project before producing any replacement artifacts.
while IFS=$'\t' read -r order project _upstream _branch base _patch; do
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
done < "$series"

while IFS=$'\t' read -r order project _upstream _branch base patch; do
  [[ -z "$order" || "$order" == \#* ]] && continue
  project_root="$android_root/$project"
  alternate_index=$(mktemp)
  rm -f "$alternate_index"
  (
    cd "$project_root"
    GIT_INDEX_FILE="$alternate_index" git read-tree HEAD
    GIT_INDEX_FILE="$alternate_index" git add -A -- .
    GIT_INDEX_FILE="$alternate_index" \
      git diff --cached --binary --full-index --no-ext-diff "$base" -- \
      > "$staging/$patch"
  )
  rm -f "$alternate_index"
  alternate_index=
  [[ -s "$staging/$patch" ]] || {
    echo "Generated empty patch for $project" >&2
    exit 1
  }
  printf 'generated %s\n' "$patch"
done < "$series"

(
  cd "$staging"
  sha256sum ./*.patch | sed 's#  \./#  #' > SHA256SUMS
  sha256sum -c SHA256SUMS
)

# Replace the complete bundle only after every artifact is ready. The EXIT
# trap restores the previous directory if either rename is interrupted.
backup="$bundle_parent/.lineage-18.1.old.$$"
mv "$bundle" "$backup"
mv "$staging" "$bundle"
staging=
rm -rf "$backup"
backup=
trap - EXIT HUP INT TERM
