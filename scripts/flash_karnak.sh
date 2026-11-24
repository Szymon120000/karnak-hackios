#!/usr/bin/env bash
# Flash (or temporarily boot) a karnak image, with safety checks.
#
#   scripts/flash_karnak.sh out/uefi-karnak-lk.img \
#       --backup /path/to/stock-boot.img [--boot-test] [--yes]
#
# Default action: `fastboot flash boot <image>`.
# --boot-test:    `fastboot boot <image>` (nothing is written; use this first
#                 if the unlocked LK supports it).
#
# Refuses to run unless:
#   * the image passes tools/check_boot_image.py,
#   * exactly one fastboot device is attached and it looks like karnak,
#   * the bootloader reports itself unlocked,
#   * a stock boot.img backup exists (or --boot-test is used),
#   * the operator confirms the codename, unless --yes is passed.
#
# There is no unbrick button.  Keep the stock boot image and know how to restore
# it from recovery/TWRP before flashing anything.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE=""
BACKUP=""
BOOT_TEST=0
ASSUME_YES=0

usage() {
  sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --backup)   BACKUP="${2:-}"; shift 2 ;;
    --boot-test) BOOT_TEST=1; shift ;;
    --yes|-y)   ASSUME_YES=1; shift ;;
    -h|--help)  usage 0 ;;
    -*)         echo "unknown option: $1" >&2; usage 1 ;;
    *)          [[ -z "$IMAGE" ]] || { echo "only one image" >&2; exit 1; }
                IMAGE="$1"; shift ;;
  esac
done

die() { echo "error: $*" >&2; exit 1; }

[[ -n "$IMAGE" ]] || usage 1
[[ -f "$IMAGE" ]] || die "image not found: $IMAGE"
command -v fastboot >/dev/null 2>&1 || die "fastboot is not installed"
python3 "$ROOT/tools/check_boot_image.py" "$IMAGE" || \
  die "image failed the structural check; refusing to flash"

devices=$(fastboot devices | awk 'NF {print $1}')
count=$(printf '%s\n' "$devices" | grep -c . || true)
[[ "$count" -eq 1 ]] || die "expected exactly one fastboot device, found $count"
SERIAL="$devices"

product=$(fastboot -s "$SERIAL" getvar product 2>&1 | sed -n 's/^product: *//p' | head -1)
if [[ -n "$product" && "$product" != *karnak* ]]; then
  die "device reports product '$product', not karnak; refusing"
fi

# Locked devices usually reject flash anyway, but make the reason explicit.
unlock_info=$(fastboot -s "$SERIAL" oem device-info 2>&1; \
              fastboot -s "$SERIAL" getvar unlocked 2>&1)
if ! grep -qiE 'unlocked: *(yes|true)|Device unlocked: *true' <<<"$unlock_info"; then
  echo "warning: could not confirm the bootloader is unlocked." >&2
  echo "         Output was:" >&2
  sed 's/^/         /' <<<"$unlock_info" >&2
  [[ "$ASSUME_YES" -eq 1 ]] || die "unlock the device first, or pass --yes if you know it is unlocked"
fi

if [[ "$BOOT_TEST" -eq 0 ]]; then
  [[ -n "$BACKUP" ]] || die "--backup <stock-boot.img> is required for a real flash"
  [[ -f "$BACKUP" && -s "$BACKUP" ]] || die "backup file not found or empty: $BACKUP"
  [[ "$(head -c 8 "$BACKUP")" == "ANDROID!" ]] || \
    die "backup does not look like an Android boot image: $BACKUP"
  echo "backup: $BACKUP ($(stat -c %s "$BACKUP") bytes)"
fi

if [[ "$ASSUME_YES" -eq 0 ]]; then
  printf 'Type karnak to confirm %s on %s: ' \
    "$([[ $BOOT_TEST -eq 1 ]] && echo 'temporary boot (no write)' || echo 'FLASH of the boot partition')" \
    "$SERIAL"
  read -r answer
  [[ "$answer" == "karnak" ]] || die "aborted"
fi

if [[ "$BOOT_TEST" -eq 1 ]]; then
  echo "booting $IMAGE without writing the boot partition..."
  fastboot -s "$SERIAL" boot "$IMAGE"
else
  echo "flashing $IMAGE to boot..."
  fastboot -s "$SERIAL" flash boot "$IMAGE"
  echo
  echo "Recovery: from TWRP/root shell, restore the backup with"
  echo "  dd if=$BACKUP of=/dev/block/by-name/boot"
  echo "(copy it to the device first)."
fi
