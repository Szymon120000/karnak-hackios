#!/usr/bin/env bash
# Read-only karnak information collector.
#
# Works both against a booted Android/Linux system (adb) and against fastboot,
# and never writes to the device.  Its purpose is to settle the DRAM size and
# framebuffer questions in docs/HARDWARE.md.
set -uo pipefail

say() { printf '\n== %s ==\n' "$*"; }

have_adb=0
have_fb=0
command -v adb >/dev/null 2>&1 && have_adb=1
command -v fastboot >/dev/null 2>&1 && have_fb=1

if [[ $have_adb -eq 1 ]] && adb get-state >/dev/null 2>&1; then
  say "Android property summary"
  adb shell getprop ro.product.device
  adb shell getprop ro.product.model
  adb shell getprop ro.boot.hardware
  adb shell getprop ro.boot.dtb_idx
  adb shell cat /proc/meminfo | head -3

  say "device-tree memory node"
  adb shell 'for f in /proc/device-tree/memory*/reg; do echo "$f"; od -An -tx1 "$f"; done' 2>/dev/null

  say "chosen bootargs / videolfb"
  adb shell 'od -An -tx1 /proc/device-tree/chosen/bootargs 2>/dev/null'
  adb shell 'od -An -tx1 /proc/device-tree/chosen/atag,videolfb 2>/dev/null'

  say "msdc / mmc"
  adb shell 'ls /sys/class/mmc_host 2>/dev/null; cat /proc/partitions'
  exit 0
fi

if [[ $have_fb -eq 1 ]] && fastboot devices | grep -q .; then
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
  - /proc/device-tree/memory*/reg (big-endian base/size, 2+2 cells)
  - /proc/device-tree/chosen/atag,videolfb
EOF
