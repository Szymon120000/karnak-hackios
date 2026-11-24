#!/usr/bin/env bash
# Fetch the pinned EDK2 MediaTek tree and its submodules.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR="${VENDOR:-$ROOT/vendor}"

EDK2_MTK_REPO="${EDK2_MTK_REPO:-https://github.com/linux-mediatek/edk2-mtk.git}"
EDK2_MTK_REF="${EDK2_MTK_REF:-next}"

mkdir -p "$VENDOR"

if [[ ! -d "$VENDOR/edk2-mtk/.git" ]]; then
  echo "cloning $EDK2_MTK_REPO ($EDK2_MTK_REF)"
  git clone --recursive --shallow-submodules --depth 1 \
    --branch "$EDK2_MTK_REF" "$EDK2_MTK_REPO" "$VENDOR/edk2-mtk"
else
  echo "updating $VENDOR/edk2-mtk"
  git -C "$VENDOR/edk2-mtk" fetch --depth 1 origin "$EDK2_MTK_REF"
  git -C "$VENDOR/edk2-mtk" checkout -q FETCH_HEAD
  git -C "$VENDOR/edk2-mtk" submodule update --init --recursive --depth 1
fi

echo "edk2-mtk ready at $VENDOR/edk2-mtk"
