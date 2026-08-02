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
series="$repo_root/manifests/amazon-oss-patches-lineage-18.1.tsv"
patch_repo="$android_root/patches"
expected_patch_revision=e2060e7985fdf61ebd0663f170aed8609dbb5e1e

[[ -f "$series" ]] || {
  echo "Missing Amazon-OSS patch series: $series" >&2
  exit 1
}
git -C "$patch_repo" rev-parse --git-dir >/dev/null 2>&1 || {
  echo "Missing pinned Amazon-OSS patch repository: $patch_repo" >&2
  exit 1
}
actual_patch_revision=$(git -C "$patch_repo" rev-parse HEAD)
[[ "$actual_patch_revision" == "$expected_patch_revision" ]] || {
  echo "Amazon-OSS patches must be at $expected_patch_revision; found $actual_patch_revision" >&2
  exit 1
}
[[ -z $(git -C "$patch_repo" status --porcelain) ]] || {
  echo "Amazon-OSS patch repository has local changes" >&2
  exit 1
}

mapfile -t declared_patches < <(
  while IFS=$'\t' read -r order _project _base patch; do
    [[ -z "$order" || "$order" == \#* ]] && continue
    printf '%s\n' "$patch"
  done < "$series" | sort
)
mapfile -t repository_patches < <(
  find "$patch_repo" -type f -name '*.patch' -not -path '*/.git/*' \
    -printf '%P\n' | sort
)
if ! diff -u \
  <(printf '%s\n' "${repository_patches[@]}") \
  <(printf '%s\n' "${declared_patches[@]}") >/dev/null; then
  echo "Declared Amazon-OSS patch series does not match pinned repository contents" >&2
  diff -u \
    <(printf '%s\n' "${repository_patches[@]}") \
    <(printf '%s\n' "${declared_patches[@]}") >&2 || true
  exit 1
fi

# This pinned bundle currently has one patch per project. Keeping that invariant
# makes it possible to preflight the complete series before mutating any tree.
declare -A seen_projects=()
while IFS=$'\t' read -r order project base patch; do
  [[ -z "$order" || "$order" == \#* ]] && continue
  [[ -z ${seen_projects[$project]:-} ]] || {
    echo "Amazon-OSS series contains multiple patches for $project" >&2
    exit 1
  }
  seen_projects[$project]=1

  project_root="$android_root/$project"
  git -C "$project_root" rev-parse --git-dir >/dev/null 2>&1 || {
    echo "Missing Git project required by Amazon-OSS patches: $project" >&2
    exit 1
  }
  actual=$(git -C "$project_root" rev-parse HEAD)
  [[ "$actual" == "$base" ]] || {
    echo "$project must be at $base; found $actual" >&2
    exit 1
  }
  [[ -z $(git -C "$project_root" status --porcelain) ]] || {
    echo "$project has local changes before Amazon-OSS patch application" >&2
    exit 1
  }
  [[ -f "$patch_repo/$patch" ]] || {
    echo "Missing Amazon-OSS patch: $patch" >&2
    exit 1
  }
  git -C "$project_root" apply --check "$patch_repo/$patch"
  printf 'checked Amazon-OSS patch for %s\n' "$project"
done < "$series"

if $check_only; then
  echo "All required Amazon-OSS patches apply cleanly."
  exit 0
fi

while IFS=$'\t' read -r order project _base patch; do
  [[ -z "$order" || "$order" == \#* ]] && continue
  project_root="$android_root/$project"
  git -C "$project_root" apply "$patch_repo/$patch"
  git -C "$project_root" diff --check
  [[ -n $(git -C "$project_root" status --porcelain) ]] || {
    echo "$patch produced no changes in $project" >&2
    exit 1
  }
  printf 'applied Amazon-OSS patch %s\n' "$patch"
done < "$series"

echo "Required Amazon-OSS patch series applied."
