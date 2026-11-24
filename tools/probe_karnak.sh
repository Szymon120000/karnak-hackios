#!/usr/bin/env bash
# Read-only karnak information collector.
#
# Works both against a booted Android/Linux system (adb) and against fastboot,
# and never writes to the device.  Its purpose is to settle the DRAM size and
# framebuffer questions in docs/HARDWARE.md.
#
#   tools/probe_karnak.sh                 # adb, else fastboot, else guidance
#   KARNAK_SERIAL=... tools/probe_karnak.sh
#
# The /memory/reg cells are big-endian and use the root node's #address-cells
# and #size-cells, which are not always 2+2.  The raw od output is decoded by
# the embedded Python instead of being eyeballed.
set -uo pipefail

say() { printf '\n== %s ==\n' "$*"; }

ADB=(adb)
[[ -n "${KARNAK_SERIAL:-}" ]] && ADB=(adb -s "$KARNAK_SERIAL")

# Dump a device-tree property as one hex string (busybox/toybox od compatible).
dt_hex() {
  "${ADB[@]}" shell "od -An -tx1 -v '$1' 2>/dev/null" | tr -d ' \t\r\n'
}

# Decode an integer property from a hex string.
hex_to_int() {
  python3 -c 'import sys; d=bytes.fromhex(sys.argv[1] or ""); print(int.from_bytes(d, "big") if d else 0)' "$1"
}

decode_memory() {
  local ac="$1" sc="$2" blob="$3"
  python3 - "$ac" "$sc" "$blob" <<'PY'
import sys

ac, sc, blob = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
data = bytes.fromhex(blob)
cell = 4
if ac <= 0 or sc <= 0:
    sys.exit(f"invalid #address-cells/#size-cells ({ac}/{sc})")
step = (ac + sc) * cell
if len(data) % step:
    sys.exit(f"reg length {len(data)} is not a multiple of {(ac + sc) * cell} bytes")
for off in range(0, len(data), step):
    base = int.from_bytes(data[off:off + ac * cell], "big")
    size = int.from_bytes(data[off + ac * cell:off + step], "big")
    print(f"  base 0x{base:09x}  size 0x{size:09x}  "
          f"({size // (1024 * 1024)} MiB, end 0x{base + size:09x})")
PY
}

if command -v adb >/dev/null 2>&1 && "${ADB[@]}" get-state >/dev/null 2>&1; then
  say "Android property summary"
  "${ADB[@]}" shell getprop ro.product.device
  "${ADB[@]}" shell getprop ro.product.model
  "${ADB[@]}" shell getprop ro.boot.hardware
  "${ADB[@]}" shell getprop ro.boot.dtb_idx
  "${ADB[@]}" shell cat /proc/meminfo | head -3

  say "device-tree memory nodes"
  ac=$(hex_to_int "$(dt_hex /proc/device-tree/#address-cells)")
  sc=$(hex_to_int "$(dt_hex /proc/device-tree/#size-cells)")
  echo "  root #address-cells=$ac #size-cells=$sc"
  found=0
  while IFS= read -r node; do
    [[ -n "$node" ]] || continue
    found=1
    echo "  $node"
    decode_memory "$ac" "$sc" "$(dt_hex "$node")" || \
      echo "  (could not decode; raw: $(dt_hex "$node"))"
  done < <("${ADB[@]}" shell 'ls -d /proc/device-tree/memory*/reg 2>/dev/null')
  [[ $found -eq 1 ]] || echo "  (no /proc/device-tree/memory* nodes)"

  say "chosen bootargs / videolfb"
  "${ADB[@]}" shell 'tr -d "\0" < /proc/device-tree/chosen/bootargs 2>/dev/null; echo'
  "${ADB[@]}" shell 'tr -d "\0" < /proc/device-tree/chosen/atag,videolfb 2>/dev/null; echo'

  say "msdc / mmc"
  "${ADB[@]}" shell 'ls /sys/class/mmc_host 2>/dev/null; cat /proc/partitions'
  exit 0
fi

if command -v fastboot >/dev/null 2>&1 && fastboot devices | grep -q .; then
  say "fastboot variables"
  fastboot getvar product 2>&1
  fastboot getvar variant 2>&1
  fastboot getvar current-slot 2>&1 || true
  fastboot oem device-info 2>&1 || true
  say "NOTE: fastboot cannot read /proc/device-tree; boot a root shell to do that."
  exit 0
fi

cat <<EOF
No device found.

Connect the tablet with adb enabled (stock Fire OS is fine) and rerun, or boot
a recovery with adb.  The useful facts are:

  - /proc/meminfo MemTotal (should be ~1.5 GiB minus carve-outs)
  - /proc/device-tree/memory*/reg (big-endian, root #address-cells/#size-cells)
  - /proc/device-tree/chosen/atag,videolfb
EOF
