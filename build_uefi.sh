#!/usr/bin/env bash
# Build uefi-karnak.img (BootShim + EDK2 FD wrapped in an Android boot image).
#
#   ./build_uefi.sh            # fetch deps if needed, then build RELEASE
#   TARGET=DEBUG ./build_uefi.sh
#
# Set SKIP_FETCH=1 to reuse an existing vendor/edk2-mtk checkout.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDK2_DIR="$ROOT/vendor/edk2-mtk"
TARGET="${TARGET:-RELEASE}"

if [[ "${SKIP_FETCH:-0}" != "1" ]]; then
  "$ROOT/scripts/fetch_deps.sh"
fi

"$ROOT/scripts/stage_edk2.sh" "$EDK2_DIR"

# Regenerate the hardware map so stale generated files fail loudly.
python3 "$ROOT/tools/extract_karnak_hw.py" \
  --kernel "${KARNAK_KERNEL:-$ROOT/karnak-kernel-3.18}" \
  --check || {
    echo "hardware map is stale; regenerate it with tools/extract_karnak_hw.py" >&2
    exit 1
  }

cd "$EDK2_DIR"
make -C Common/edk2/BaseTools -j"$(nproc)"
python3 Platform/Amazon/KarnakPkg/PlatformBuild.py "TARGET=$TARGET"

mkdir -p "$ROOT/out"
cp -f "Build/karnak/${TARGET}_CLANG38/FV/KARNAK_UEFI.fd.shimmed" \
      "$ROOT/out/uefi-karnak.fd" 2>/dev/null || true
if [[ -f "out/boot-karnak.img" ]]; then
  cp -f "out/boot-karnak.img" "$ROOT/out/uefi-karnak.img"
fi

echo
echo "artifacts:"
ls -lh "$ROOT/out" || true
