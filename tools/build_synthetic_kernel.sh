#!/usr/bin/env bash
# Build the synthetic karnak kernel Mach-O used by the loader end-to-end test.
#
#   tools/build_synthetic_kernel.sh <output-dir>
#
# Produces <output-dir>/SyntheticKernel.bin (flat code) and
# <output-dir>/kernelcache (a minimal ARM64 Mach-O with LC_UNIXTHREAD).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:?usage: build_synthetic_kernel.sh <output-dir>}"
PREFIX="${CROSS_COMPILE:-aarch64-linux-gnu-}"
SRC="$ROOT/platform/edk2/XnuLoaderPkg/Test/SyntheticKernel/SyntheticKernel.S"

mkdir -p "$OUT"

"${PREFIX}gcc" -c "$SRC" -o "$OUT/SyntheticKernel.o" \
  -I "$ROOT/include" -Wa,--noexecstack
"${PREFIX}objcopy" -O binary "$OUT/SyntheticKernel.o" "$OUT/SyntheticKernel.bin"

python3 "$ROOT/tools/mk_macho.py" \
  --payload "$OUT/SyntheticKernel.bin" \
  --output "$OUT/kernelcache"
python3 "$ROOT/tools/mk_macho.py" --check "$OUT/kernelcache"
