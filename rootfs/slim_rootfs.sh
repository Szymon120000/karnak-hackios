#!/usr/bin/env bash
# Resize and trim a Darwin HFS+ rootfs for karnak.
#
#   ./slim_rootfs.sh <base.hfs> <out.img> [size]
#
# size accepts any hdiutil/mkfs size suffix; default 8g.
# This is a placeholder pipeline: it validates inputs and produces a resized
# copy, but does not yet strip daemons (see README.md).
set -euo pipefail

BASE="${1:?usage: slim_rootfs.sh <base.hfs> <out.img> [size]}"
OUT="${2:?usage: slim_rootfs.sh <base.hfs> <out.img> [size]}"
SIZE="${3:-8g}"

[[ -f "$BASE" ]] || { echo "missing $BASE" >&2; exit 1; }

if command -v hdiutil >/dev/null 2>&1; then
  echo "macOS: resizing $BASE -> $OUT ($SIZE)"
  hdiutil resize -size "$SIZE" -imagekey diskimage-class=CRawDiskImage "$BASE"
  cp "$BASE" "$OUT"
elif command -v resize_hfs >/dev/null 2>&1; then
  echo "Linux: copying then resizing with resize_hfs"
  cp "$BASE" "$OUT"
  resize_hfs "$OUT" "$SIZE"
else
  echo "error: need hdiutil (macOS) or hfsprogs/resize_hfs (Linux)" >&2
  exit 1
fi

echo "wrote $OUT"
echo
echo "Next: flash to userdata, then teach XnuLoader the device path."
echo "Memory tuning for 1.5 GB is described in rootfs/README.md."
