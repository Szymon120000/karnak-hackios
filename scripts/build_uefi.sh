#!/usr/bin/env bash
# Build uefi-karnak.fd / uefi-karnak.img (BootShim + EDK2 FD in an Android boot image).
#
#   ./scripts/build_uefi.sh [DEBUG|RELEASE]
#
# Environment:
#   SKIP_FETCH=1   reuse an existing vendor/edk2-mtk checkout
#   JOBS=N         parallel make jobs (default: nproc)
#   BOOT_HEADER_VERSION=4  boot image header (4 for CI; 0 for karnak's LK).
#                          Header v0 additionally requires:
#   LK_KERNEL_ADDR=0x...   physical kernel load address used by the stock LK
#   LK_TAGS_ADDR=0x...     tags/DTB address used by the stock LK
#   LK_PAGE_SIZE=2048      page size for v0 (from the stock boot header)
#                          Addresses are mandatory for v0; never guessed.
#
# The build deliberately drives EDK2's `build` tool directly instead of the
# stuart/edk2-pytool wrapper: KarnakPkg has no Python dependencies, and the
# wrapper pulls Mu basecore ext-deps that this tree does not ship.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDK2_DIR="$ROOT/vendor/edk2-mtk"
TARGET="${1:-${TARGET:-DEBUG}}"
# GCC5 is the toolchain MediaTek's tree is validated against and produces ELF
# that this BaseTools' GenFw converts reliably.  CLANG38 remains available
# (TOOLCHAIN=CLANG38) but older clang/lld output has been seen to crash GenFw
# on at least one module.
TOOLCHAIN="${TOOLCHAIN:-GCC5}"
JOBS="${JOBS:-$(nproc)}"
BOOT_HEADER_VERSION="${BOOT_HEADER_VERSION:-4}"

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
export GCC5_AARCH64_PREFIX="${GCC5_AARCH64_PREFIX:-aarch64-linux-gnu-}"
export CONF_PATH="${CONF_PATH:-$EDK2_DIR/Conf}"

# edk2 expects a workspace-level Conf/ with the toolchain templates.
mkdir -p "$CONF_PATH"
for tpl in tools_def build_rule target; do
  dst="$CONF_PATH/$tpl.txt"
  [[ -f "$dst" ]] || cp "$EDK2_DIR/Common/edk2/BaseTools/Conf/$tpl.template" "$dst"
done

# CLANG38 AARCH64 defaults to LTO for both DEBUG and RELEASE.  LTO produces
# bitcode-heavy ELF that BaseTools' GenFw chokes on (observed: SIGSEGV while
# converting AcpiPlatformDxe), and it also hard-codes -llto-aarch64 from
# ArmPkg/Library/GccLto, which this MediaTek tree does not ship.  Boot firmware
# gains nothing from LTO, so strip it from the CLANG38 AARCH64 flags.
python3 - "$CONF_PATH/tools_def.txt" <<'PY'
import re
import sys

path = sys.argv[1]
text = open(path).read()
pat = re.compile(
    r"^(\s*(?:DEBUG|RELEASE)_CLANG38_AARCH64_(?:CC|DLINK)_FLAGS\s*=.*)$",
    re.MULTILINE,
)
drop = re.compile(
    r"\s*(?:-flto\b|-Wl,-O\d\b|-O[13]\b|-L\$\(WORKSPACE\)/ArmPkg/Library/GccLto"
    r"|-llto-aarch64|-Wl,-plugin-opt=-pass-through=-llto-aarch64)"
)


def clean(match):
    return drop.sub("", match.group(1))


new, n = pat.subn(clean, text)
if new != text:
    open(path, "w").write(new)
    print(f"tools_def: removed LTO from {n} CLANG38 AARCH64 flag line(s)")
PY

# Always rebuild BaseTools from scratch.  CI caches the whole edk2-mtk checkout,
# and a half-written cached GenFw binary segfaults nondeterministically on
# module conversion, which is much more confusing than a 30-second rebuild.
#
# BaseTools are *host* (x86-64) binaries.  Anything added to CPATH for the
# AARCH64 cross build would also be searched when compiling them, so clear the
# include-path environment here; a leaked /usr/aarch64-linux-gnu/include makes
# the host gcc parse aarch64 glibc headers and fail on NEON types.
#
# Newer host compilers add warnings that BaseTools' -Werror escalates (e.g.
# gcc 16's -Wunused-but-set-variable in StringFuncs.c).  BaseTools is vendored
# third-party code, so keep the warnings visible but do not fail on them.
HOST_CFLAGS="-Wno-error=unused-but-set-variable -Wno-error=format-overflow"
HOST_CFLAGS="$HOST_CFLAGS -Wno-error=stringop-overflow -Wno-error=array-bounds"
# The bundled Pccts parser generator uses K&R prototypes, which gcc >= 14
# (C23) rejects.  Pin the host C dialect to gnu17 for the whole BaseTools build.
HOST_CC="${HOST_CC:-gcc} -std=gnu17"
env -u CPATH -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH \
  make -C "$EDK2_DIR/Common/edk2/BaseTools" clean >/dev/null 2>&1 || true
env -u CPATH -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH \
  make -C "$EDK2_DIR/Common/edk2/BaseTools" -j"$JOBS" \
  BUILD_CC="$HOST_CC" EXTRA_OPTFLAGS="$HOST_CFLAGS"

# Clang has no sysroot configured by default, so point it at the aarch64 libc
# when the distro provides one (Debian/Ubuntu: libc6-dev-arm64-cross).  GCC5
# already knows its sysroot, so this is only needed for the clang toolchain and
# is exported after the host BaseTools build for the AARCH64 compilation.
if [[ "$TOOLCHAIN" == CLANG38* && -d /usr/aarch64-linux-gnu/include ]]; then
  export CPATH="${CPATH:+$CPATH:}/usr/aarch64-linux-gnu/include"
fi

# shellcheck disable=SC1091
set +u
source "$EDK2_DIR/Common/edk2/edksetup.sh" BaseTools
set -u

cd "$EDK2_DIR"
build \
  -p KarnakPkg/Karnak.dsc \
  -a AARCH64 \
  -t "$TOOLCHAIN" \
  -b "$TARGET" \
  -n "$JOBS" \
  -D "FD_BASE=$FD_BASE" \
  -D "FD_SIZE=$FD_SIZE"

OUT="$EDK2_DIR/Build/karnak/${TARGET}_${TOOLCHAIN}/FV"
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

# A v0 image is what the stock 2018 LK understands.  Its load addresses are
# device-specific, so require them explicitly instead of guessing.
if [[ "$BOOT_HEADER_VERSION" == "0" ]]; then
  : "${LK_KERNEL_ADDR:?set LK_KERNEL_ADDR (from the stock boot.img/LK) for header v0}"
  : "${LK_TAGS_ADDR:?set LK_TAGS_ADDR (from the stock boot.img/LK) for header v0}"
  BOOT_IMG="$ROOT/out/uefi-karnak-lk.img"
  MKBOOT_ARGS=(
    --header_version 0
    --kernel_addr "$LK_KERNEL_ADDR"
    --tags_addr "$LK_TAGS_ADDR"
    --pagesize "${LK_PAGE_SIZE:-2048}"
  )
else
  BOOT_IMG="$ROOT/out/uefi-karnak.img"
  MKBOOT_ARGS=(--header_version "$BOOT_HEADER_VERSION")
fi

python3 "$EDK2_DIR/Platform/Amazon/KarnakPkg/Tools/mkbootimg.py" \
  "${MKBOOT_ARGS[@]}" \
  --kernel "$ROOT/out/uefi-karnak.fd" \
  --output "$BOOT_IMG"

# Structural check: boot header, shim payload offset and FV checksum.
python3 "$ROOT/tools/check_boot_image.py" "$BOOT_IMG" \
  --fd-size "$FD_SIZE"

echo
ls -lh "$ROOT/out"
