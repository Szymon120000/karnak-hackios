#!/usr/bin/env bash
# Report whether the local/CI toolchain can build the UEFI firmware.
set -uo pipefail

missing=0
check() {
  local name="$1"
  if command -v "$name" >/dev/null 2>&1; then
    printf '  ok      %-22s %s\n' "$name" "$(command -v "$name")"
  else
    printf '  MISSING %-22s\n' "$name"
    missing=$((missing + 1))
  fi
}

echo "host tools:"
for tool in git make python3 clang gcc aarch64-linux-gnu-gcc aarch64-linux-gnu-objcopy \
            dtc cpp nasm iasl mkbootimg; do
  check "$tool"
done

echo
echo "python modules:"
python3 - <<'PY' || true
for mod in ("libfdt",):
    try:
        __import__(mod)
        print(f"  ok      {mod}")
    except Exception:
        print(f"  MISSING {mod} (Debian: python3-libfdt)")
PY
for mod in ("edk2toolext", "edk2toollib"); do
  if python3 -c "import $mod" >/dev/null 2>&1; then
    echo "  ok      $mod"
  else
    echo "  MISSING $mod (pip install edk2-pytool-extensions edk2-pytool-library)"
  fi
done

echo
if [[ $missing -eq 0 ]]; then
  echo "all required host tools are present"
else
  echo "$missing required host tool(s) missing"
fi
exit 0
