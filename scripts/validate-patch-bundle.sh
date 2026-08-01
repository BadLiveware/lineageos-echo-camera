#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 ANDROID_SOURCE_ROOT" >&2
  exit 2
fi

android_root=$(realpath "$1")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bundle="$repo_root/patches/lineage-18.1"
series="$bundle/series.tsv"
work_root=$(mktemp -d "${TMPDIR:-/tmp}/checkers-camera-patches.XXXXXX")
declare -a source_repos=()
declare -a worktrees=()

cleanup() {
  for ((index=${#worktrees[@]}-1; index>=0; --index)); do
    git -C "${source_repos[$index]}" worktree remove --force \
      "${worktrees[$index]}" >/dev/null 2>&1 || true
  done
  rm -rf "$work_root"
}
trap cleanup EXIT

(
  cd "$bundle"
  sha256sum -c SHA256SUMS
)

while IFS=$'\t' read -r order project _upstream _branch base patch; do
  [[ -z "$order" || "$order" == \#* ]] && continue
  source_repo="$android_root/$project"
  worktree="$work_root/$order"
  git -C "$source_repo" rev-parse --git-dir >/dev/null 2>&1 || {
    echo "Missing Git project: $project" >&2
    exit 1
  }

  git -C "$source_repo" cat-file -e "$base^{commit}"
  git -C "$source_repo" worktree add --detach "$worktree" "$base" >/dev/null
  source_repos+=("$source_repo")
  worktrees+=("$worktree")

  git -C "$worktree" apply --check "$bundle/$patch"
  git -C "$worktree" apply "$bundle/$patch"
  git -C "$worktree" diff --check
  [[ -n $(git -C "$worktree" status --porcelain) ]] || {
    echo "$patch produced no changes" >&2
    exit 1
  }
  printf 'validated %s at %s\n' "$project" "$base"
done < "$series"

echo "Patch bundle validation passed."
