#!/usr/bin/env bash
# Build out/uefi-karnak-lk.img (Android header v0) for karnak's stock LK.
#
#   scripts/build_lk_image.sh --stock boot-stock.img [DEBUG|RELEASE]
#   scripts/build_lk_image.sh --kernel-addr 0x... --tags-addr 0x... [DEBUG|RELEASE]
#
# The addresses are read from the stock boot image (preferred) or passed
# explicitly.  They are never guessed: a wrong kernel_addr makes LK jump into
# random memory.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STOCK=""
KERNEL_ADDR=""
TAGS_ADDR=""
PAGE_SIZE=""

usage() { sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --stock)        STOCK="${2:-}"; shift 2 ;;
    --kernel-addr)  KERNEL_ADDR="${2:-}"; shift 2 ;;
    --tags-addr)    TAGS_ADDR="${2:-}"; shift 2 ;;
    --page-size)    PAGE_SIZE="${2:-}"; shift 2 ;;
    -h|--help)      usage 0 ;;
    DEBUG|RELEASE)  TARGET="$1"; shift ;;
    *) echo "unknown argument: $1" >&2; usage 1 ;;
  esac
done
TARGET="${TARGET:-DEBUG}"

if [[ -n "$STOCK" ]]; then
  [[ -f "$STOCK" ]] || { echo "error: no such file: $STOCK" >&2; exit 1; }
  echo "reading LK load addresses from $STOCK"
  python3 "$ROOT/tools/boot_header.py" "$STOCK"
  eval "$(python3 "$ROOT/tools/boot_header.py" "$STOCK" --shell)"
  [[ -z "$KERNEL_ADDR" ]] && KERNEL_ADDR="$LK_KERNEL_ADDR"
  [[ -z "$TAGS_ADDR" ]] && TAGS_ADDR="$LK_TAGS_ADDR"
  [[ -z "$PAGE_SIZE" ]] && PAGE_SIZE="$LK_PAGE_SIZE"
fi

[[ -n "$KERNEL_ADDR" && -n "$TAGS_ADDR" ]] || {
  echo "error: pass --stock <boot.img> or both --kernel-addr and --tags-addr" >&2
  exit 1
}

export BOOT_HEADER_VERSION=0
export LK_KERNEL_ADDR="$KERNEL_ADDR"
export LK_TAGS_ADDR="$TAGS_ADDR"
[[ -n "$PAGE_SIZE" ]] && export LK_PAGE_SIZE="$PAGE_SIZE"

exec "$ROOT/scripts/build_uefi.sh" "$TARGET"
