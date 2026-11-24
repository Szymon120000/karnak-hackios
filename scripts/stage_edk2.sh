#!/usr/bin/env bash
# Install the karnak platform package into an edk2-mtk checkout.
#
#   scripts/stage_edk2.sh [edk2-mtk-dir]
#
# Defaults to vendor/edk2-mtk.  The source of truth for the package lives in
# platform/edk2/KarnakPkg so this repository can be reviewed and diffed
# without carrying the whole EDK2 tree.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2_DIR="${1:-$ROOT/vendor/edk2-mtk}"
SRC="$ROOT/platform/edk2/KarnakPkg"
DST="$EDK2_DIR/Platform/Amazon/KarnakPkg"

if [[ ! -d "$EDK2_DIR" ]]; then
  echo "error: $EDK2_DIR does not exist; run scripts/fetch_deps.sh" >&2
  exit 1
fi

mkdir -p "$(dirname "$DST")"
rm -rf "$DST"
cp -a "$SRC" "$DST"

# Keep the generated hardware map alongside the package for traceability.
mkdir -p "$DST/Docs"
cp "$ROOT/include/karnak/hardware_map.h" "$DST/Docs/"
cp "$ROOT/include/karnak/hardware_map.json" "$DST/Docs/"

echo "staged $DST"
