#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 [--check] DEVICE ANDROID_SOURCE_ROOT" >&2
  echo "DEVICE is one of: checkers, crown" >&2
  exit 2
}

check_only=false
if [[ ${1:-} == --check ]]; then
  check_only=true
  shift
fi
[[ $# -eq 2 ]] || usage

device=$1
case "$device" in
  checkers|crown) ;;
  *) usage ;;
esac
android_root=$(realpath "$2")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bundle="$repo_root/patches/lineage-18.1"
series="$bundle/$device/series.tsv"

[[ -f "$series" ]] || {
  echo "Missing $device patch series: $series" >&2
  exit 1
}
[[ -f "$bundle/SHA256SUMS" ]] || {
  echo "Missing patch checksums: $bundle/SHA256SUMS" >&2
  exit 1
}
(
  cd "$bundle"
  sha256sum -c SHA256SUMS
)

declare -A project_bases=()
declare -a projects=()
while IFS=$'\t' read -r order project _upstream branch base patch _scope; do
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
  [[ -f "$bundle/$patch" ]] || {
    echo "Missing patch payload: $patch" >&2
    exit 1
  }
  if [[ -n ${project_bases[$project]:-} ]]; then
    [[ ${project_bases[$project]} == "$base" ]] || {
      echo "$project has conflicting patch bases" >&2
      exit 1
    }
  else
    project_bases[$project]=$base
    projects+=("$project")
  fi
done < "$series"

if $check_only; then
  work_root=$(mktemp -d "${TMPDIR:-/tmp}/${device}-camera-patches.XXXXXX")
  declare -A project_worktrees=()
  declare -a source_repos=()
  declare -a worktrees=()
  # Invoked by the EXIT trap below.
  # shellcheck disable=SC2329
  cleanup() {
    for ((index=${#worktrees[@]}-1; index>=0; --index)); do
      git -C "${source_repos[$index]}" worktree remove --force \
        "${worktrees[$index]}" >/dev/null 2>&1 || true
    done
    rm -rf "$work_root"
  }
  trap cleanup EXIT

  while IFS=$'\t' read -r order project _upstream _branch base patch _scope; do
    [[ -z "$order" || "$order" == \#* ]] && continue
    worktree=${project_worktrees[$project]:-}
    if [[ -z "$worktree" ]]; then
      worktree="$work_root/$order"
      source_repo="$android_root/$project"
      git -C "$source_repo" worktree add --detach "$worktree" "$base" >/dev/null
      project_worktrees[$project]=$worktree
      source_repos+=("$source_repo")
      worktrees+=("$worktree")
    fi
    git -C "$worktree" apply --check "$bundle/$patch"
    git -C "$worktree" apply "$bundle/$patch"
    printf 'checked %s for %s\n' "$patch" "$project"
  done < "$series"

  for project in "${projects[@]}"; do
    worktree=${project_worktrees[$project]}
    git -C "$worktree" diff --check
    [[ -n $(git -C "$worktree" status --porcelain) ]] || {
      echo "$device patch series produced no changes in $project" >&2
      exit 1
    }
  done
  echo "All $device patches apply cleanly."
  exit 0
fi

while IFS=$'\t' read -r order project _upstream _branch _base patch _scope; do
  [[ -z "$order" || "$order" == \#* ]] && continue
  project_root="$android_root/$project"
  git -C "$project_root" apply --check "$bundle/$patch"
  git -C "$project_root" apply "$bundle/$patch"
  printf 'applied %s\n' "$patch"
done < "$series"

echo "$device camera patch series applied."
