#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 DEVICE ANDROID_DEVELOPMENT_ROOT" >&2
  echo "DEVICE is one of: checkers, crown" >&2
  exit 2
fi

device=$1
case "$device" in
  checkers|crown) ;;
  *)
    echo "unsupported device: $device" >&2
    exit 2
    ;;
esac
android_root=$(realpath "$2")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bundle="$repo_root/patches/lineage-18.1"
series="$bundle/$device/series.tsv"
staging=$(mktemp -d "${TMPDIR:-/tmp}/${device}-camera-patches.new.XXXXXX")
backup=$(mktemp -d "${TMPDIR:-/tmp}/${device}-camera-patches.old.XXXXXX")
alternate_index=
replacing=false

declare -a generated_patches=()
declare -a stale_patches=()
declare -A generated_patch_set=()

cleanup() {
  status=$?
  rm -f "${alternate_index:-}"
  if $replacing; then
    for patch in "${generated_patches[@]}"; do
      if [[ -f "$backup/$patch" ]]; then
        mkdir -p "$bundle/$(dirname "$patch")"
        cp "$backup/$patch" "$bundle/$patch"
      else
        rm -f "$bundle/$patch"
      fi
    done
    for patch in "${stale_patches[@]}"; do
      mkdir -p "$bundle/$(dirname "$patch")"
      cp "$backup/$patch" "$bundle/$patch"
    done
    if [[ -f "$backup/SHA256SUMS" ]]; then
      cp "$backup/SHA256SUMS" "$bundle/SHA256SUMS"
    fi
  fi
  rm -rf "$staging" "$backup"
  return "$status"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

[[ -f "$series" ]] || {
  echo "Missing $device patch series: $series" >&2
  exit 1
}

# Verify every project and scope before generating replacement payloads.
while IFS=$'\t' read -r order project _upstream _branch base patch scope; do
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
  [[ -n "$scope" ]] || {
    echo "$device series has an empty scope for $patch" >&2
    exit 1
  }
  generated_patches+=("$patch")
  generated_patch_set[$patch]=1
done < "$series"

while IFS=$'\t' read -r order project _upstream _branch base patch scope; do
  [[ -z "$order" || "$order" == \#* ]] && continue
  project_root="$android_root/$project"
  alternate_index=$(mktemp)
  rm -f "$alternate_index"
  IFS=',' read -r -a pathspecs <<< "$scope"
  mkdir -p "$staging/$(dirname "$patch")"
  (
    cd "$project_root"
    GIT_INDEX_FILE="$alternate_index" git read-tree HEAD
    GIT_INDEX_FILE="$alternate_index" git add -A -- "${pathspecs[@]}"
    GIT_INDEX_FILE="$alternate_index" \
      git diff --cached --binary --full-index --no-ext-diff "$base" -- \
      "${pathspecs[@]}" > "$staging/$patch"
  )
  rm -f "$alternate_index"
  alternate_index=
  [[ -s "$staging/$patch" ]] || {
    echo "Generated empty patch for $project scope $scope" >&2
    exit 1
  }
  printf 'generated %s\n' "$patch"
done < "$series"

# Replace only the selected device series. Shared payloads are regenerated from
# the same owned source scopes and remain referenced by both device series.
while IFS= read -r -d '' payload; do
  patch=${payload#"$bundle/"}
  if [[ -z ${generated_patch_set[$patch]:-} ]]; then
    stale_patches+=("$patch")
  fi
done < <(find "$bundle/$device" -type f -name '*.patch' -print0)

for patch in "${generated_patches[@]}" "${stale_patches[@]}"; do
  mkdir -p "$backup/$(dirname "$patch")" "$bundle/$(dirname "$patch")"
  if [[ -f "$bundle/$patch" ]]; then
    cp "$bundle/$patch" "$backup/$patch"
  fi
done
cp "$bundle/SHA256SUMS" "$backup/SHA256SUMS"
replacing=true
for patch in "${generated_patches[@]}"; do
  cp "$staging/$patch" "$bundle/$patch"
done
for patch in "${stale_patches[@]}"; do
  rm -f "$bundle/$patch"
  printf 'removed %s\n' "$patch"
done
(
  cd "$bundle"
  find shared checkers crown -type f -name '*.patch' -print0 |
    sort -z |
    xargs -0 sha256sum > SHA256SUMS
  sha256sum -c SHA256SUMS
)
replacing=false
rm -rf "$staging" "$backup"
staging=
backup=
trap - EXIT HUP INT TERM
