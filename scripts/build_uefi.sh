#!/usr/bin/env bash
# Build uefi-karnak.fd / uefi-karnak.img (BootShim + EDK2 FD in an Android boot image).
#
#   ./scripts/build_uefi.sh [DEBUG|RELEASE]
#
# Environment:
#   SKIP_FETCH=1   reuse an existing vendor/edk2-mtk checkout
#   JOBS=N         parallel make jobs (default: nproc)
#
# The build deliberately drives EDK2's `build` tool directly instead of the
# stuart/edk2-pytool wrapper: KarnakPkg has no Python dependencies, and the
# wrapper pulls Mu basecore ext-deps that this tree does not ship.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2_DIR="$ROOT/vendor/edk2-mtk"
TARGET="${1:-${TARGET:-DEBUG}}"
JOBS="${JOBS:-$(nproc)}"

# Firmware layout, must match include/karnak/hardware_map.h.
FD_BASE="0x50000000"
FD_SIZE="0x00700000"

if [[ "${SKIP_FETCH:-0}" != "1" ]]; then
  "$ROOT/scripts/fetch_deps.sh"
fi
"$ROOT/scripts/stage_edk2.sh" "$EDK2_DIR"

# Reject stale generated hardware values before spending minutes compiling.
if python3 -c 'import libfdt' >/dev/null 2>&1; then
  python3 "$ROOT/tools/extract_karnak_hw.py" \
    --kernel "${KARNAK_KERNEL:-$ROOT/karnak-kernel-3.18}" --check
else
  echo "warning: python libfdt missing, skipping hardware map drift check" >&2
fi

# edk2 needs WORKSPACE + PACKAGES_PATH; the MediaTek tree keeps its own
# copies of edk2/edk2-platforms under Common/.
export WORKSPACE="$EDK2_DIR"
export PACKAGES_PATH="$EDK2_DIR/Common/edk2:$EDK2_DIR/Common/edk2-platforms:$EDK2_DIR/Silicon/MediaTek:$EDK2_DIR/Platform/Amazon"
export EDK_TOOLS_PATH="$EDK2_DIR/Common/edk2/BaseTools"
export PYTHON_COMMAND="${PYTHON_COMMAND:-python3}"
export CLANG38_BIN="${CLANG38_BIN:-/usr/bin/}"
export CLANG38_AARCH64_PREFIX="${CLANG38_AARCH64_PREFIX:-aarch64-linux-gnu-}"
export CONF_PATH="${CONF_PATH:-$EDK2_DIR/Conf}"

# edk2 expects a workspace-level Conf/ with the toolchain templates.
mkdir -p "$CONF_PATH"
for tpl in tools_def build_rule target; do
  dst="$CONF_PATH/$tpl.txt"
  [[ -f "$dst" ]] || cp "$EDK2_DIR/Common/edk2/BaseTools/Conf/$tpl.template" "$dst"
done

# Some vendor modules pull in <arm_neon.h>, which includes the target libc's
# <stdint.h>.  Point clang at the aarch64 sysroot when the distro provides one
# (Debian/Ubuntu: libc6-dev-arm64-cross).
if [[ -d /usr/aarch64-linux-gnu/include ]]; then
  export CPATH="${CPATH:+$CPATH:}/usr/aarch64-linux-gnu/include"
fi

make -C "$EDK2_DIR/Common/edk2/BaseTools" -j"$JOBS"

# shellcheck disable=SC1091
set +u
source "$EDK2_DIR/Common/edk2/edksetup.sh" BaseTools
set -u

cd "$EDK2_DIR"
build \
  -p KarnakPkg/Karnak.dsc \
  -a AARCH64 \
  -t CLANG38 \
  -b "$TARGET" \
  -n "$JOBS" \
  -D "FD_BASE=$FD_BASE" \
  -D "FD_SIZE=$FD_SIZE"

OUT="$EDK2_DIR/Build/karnak/${TARGET}_CLANG38/FV"
FD="$OUT/KARNAK_UEFI.fd"
[[ -f "$FD" ]] || { echo "error: $FD was not produced" >&2; exit 1; }

# BootShim is prepended to the FD by the bootloader chain; keep both the raw
# shim+FD blob and an Android boot image for `fastboot flash boot`.
SHIM_DIR="$EDK2_DIR/Platform/Amazon/KarnakPkg/BootShim"
make -C "$SHIM_DIR" \
  UEFI_BASE="$FD_BASE" \
  UEFI_SIZE="$FD_SIZE" \
  DTB_STORE=0x4FE00000 \
  DTB_PTR_STORE=0x4FF00000 \
  DTB_MAX=0x00100000

mkdir -p "$ROOT/out"
cat "$SHIM_DIR/BootShim.bin" "$FD" > "$ROOT/out/uefi-karnak.fd"
python3 "$EDK2_DIR/Platform/Amazon/KarnakPkg/Tools/mkbootimg.py" \
  --header_version 4 \
  --kernel "$ROOT/out/uefi-karnak.fd" \
  --output "$ROOT/out/uefi-karnak.img"

echo
ls -lh "$ROOT/out"
