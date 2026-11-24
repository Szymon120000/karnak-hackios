#!/usr/bin/env bash
# Turn a Darwin HFS+ filesystem image into a karnak disk image.
#
#   ./slim_rootfs.sh <base.hfs> <out.img> [size]
#
# size is the *disk* size (default 8G) and accepts k/m/g suffixes.  Steps:
#   1. copy the base filesystem to a private working file,
#   2. grow it to at least 3 GiB (and to <size> minus the boot partition,
#      which defaults to 16 MiB) using hdiutil (macOS) or resize_hfs (Linux),
#   3. wrap it in a GPT disk image with rootfs/mkrootfs.py so MSDC0/XNU can
#      address it linearly.
#
# The image is not stripped: see rootfs/README.md for the daemon list to
# remove and the jetsam limits for a 1.5 GB device.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BASE="${1:?usage: slim_rootfs.sh <base.hfs> <out.img> [size]}"
OUT="${2:?usage: slim_rootfs.sh <base.hfs> <out.img> [size]}"
SIZE="${3:-8g}"

[[ -f "$BASE" ]] || { echo "missing $BASE" >&2; exit 1; }

# Grow the filesystem to the disk size minus the GPT/boot overhead.
if [[ "$SIZE" =~ ^([0-9]+)[gG]$ ]]; then
  fs_size="$(( ${BASH_REMATCH[1]} - 1 ))g"
elif [[ "$SIZE" =~ ^([0-9]+)[mM]$ ]]; then
  fs_size="$(( ${BASH_REMATCH[1]} - 20 ))m"
else
  fs_size="$SIZE"
fi

WORK="$(mktemp "${TMPDIR:-/tmp}/karnak-rootfs.XXXXXX.hfs")"
trap 'rm -f "$WORK"' EXIT
cp "$BASE" "$WORK"

if command -v hdiutil >/dev/null 2>&1; then
  echo "macOS: resizing HFS+ payload to $fs_size"
  hdiutil resize -size "$fs_size" -imagekey diskimage-class=CRawDiskImage "$WORK" >/dev/null
elif command -v resize_hfs >/dev/null 2>&1; then
  echo "Linux: resizing HFS+ payload to $fs_size with resize_hfs"
  resize_hfs "$WORK" "$fs_size"
else
  echo "error: need hdiutil (macOS) or hfsprogs/resize_hfs (Linux)" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUT")"
python3 "$HERE/mkrootfs.py" --hfs "$WORK" -o "$OUT" -s "$SIZE"
python3 "$HERE/mkrootfs.py" --inspect "$OUT"

echo
echo "Next: flash $OUT to the eMMC userdata area, then point XnuLoader at the"
echo "KARNAK-ROOT partition.  Memory tuning is described in rootfs/README.md."
