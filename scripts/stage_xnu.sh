#!/usr/bin/env bash
# Fetch the pinned XNU source and stage the karnak board support.
#
#   scripts/stage_xnu.sh [xnu-dir] [--check]
#
# Default xnu-dir: vendor/xnu.  If it does not exist, the pinned tag is fetched
# (blobless clone, single commit) instead of cloning all of XNU history.
# --check is non-destructive and is what CI runs on a fresh tree.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECK=0
XNU_DIR="$ROOT/vendor/xnu"
have_dir=0
for arg in "$@"; do
  case "$arg" in
    --check) CHECK=1 ;;
    -*) echo "unknown option: $arg" >&2; exit 2 ;;
    *)
      if [[ "$have_dir" -eq 1 ]]; then
        echo "error: more than one XNU directory given" >&2
        exit 2
      fi
      XNU_DIR="$arg"
      have_dir=1
      ;;
  esac
done

# xnu-8796.141.3 (iOS 16 era).  Keep in sync with xnu/include/README.md.
XNU_TAG="xnu-8796.141.3"
XNU_SHA="1b191cb58250d0705d8a51287127505aa4bc0789"

if [[ ! -d "$XNU_DIR/.git" ]]; then
  if [[ "$CHECK" -eq 1 ]]; then
    echo "error: $XNU_DIR is not a git checkout; cannot --check" >&2
    exit 1
  fi
  echo "fetching $XNU_TAG ($XNU_SHA) into $XNU_DIR"
  git clone --filter=blob:none --no-checkout \
    https://github.com/apple-oss-distributions/xnu.git "$XNU_DIR"
  git -C "$XNU_DIR" fetch --depth 1 origin "$XNU_SHA"
  git -C "$XNU_DIR" checkout --detach "$XNU_SHA"
fi

actual="$(git -C "$XNU_DIR" rev-parse HEAD)"
if [[ "$actual" != "$XNU_SHA" ]]; then
  echo "error: $XNU_DIR is at $actual, expected $XNU_SHA" >&2
  echo "       check it out at the pinned tag before staging" >&2
  exit 1
fi

args=("$XNU_DIR")
[[ "$CHECK" -eq 1 ]] && args+=(--check)
python3 "$ROOT/tools/apply_xnu_patches.py" "${args[@]}"
