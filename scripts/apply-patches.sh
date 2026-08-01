#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 [--check] ANDROID_SOURCE_ROOT" >&2
  exit 2
}

check_only=false
if [[ ${1:-} == --check ]]; then
  check_only=true
  shift
fi
[[ $# -eq 1 ]] || usage

android_root=$(realpath "$1")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bundle="$repo_root/patches/lineage-18.1"
series="$bundle/series.tsv"

[[ -f "$series" ]] || {
  echo "Missing patch series: $series" >&2
  exit 1
}

# Preflight every project before changing any worktree.
while IFS=$'\t' read -r order project _upstream branch base patch; do
  [[ -z "$order" || "$order" == \#* ]] && continue
  project_root="$android_root/$project"
  git -C "$project_root" rev-parse --git-dir >/dev/null 2>&1 || {
    echo "Missing Git project: $project" >&2
    exit 1
  }
  actual=$(git -C "$project_root" rev-parse HEAD)
  [[ "$actual" == "$base" ]] || {
    echo "$project must be at $base ($branch); found $actual" >&2
    exit 1
  }
  [[ -z $(git -C "$project_root" status --porcelain) ]] || {
    echo "$project has local changes" >&2
    exit 1
  }
  git -C "$project_root" apply --check "$bundle/$patch"
  printf 'checked %s\n' "$project"
done < "$series"

if $check_only; then
  echo "All patches apply cleanly."
  exit 0
fi

while IFS=$'\t' read -r order project _upstream _branch _base patch; do
  [[ -z "$order" || "$order" == \#* ]] && continue
  git -C "$android_root/$project" apply "$bundle/$patch"
  printf 'applied %s\n' "$patch"
done < "$series"

echo "Camera patch series applied. Extract Checkers proprietary files before building."
