#!/usr/bin/env bash
# Convenience wrapper: see scripts/build_uefi.sh.
#
#   ./build_uefi.sh            # DEBUG firmware (serial console enabled)
#   ./build_uefi.sh RELEASE    # smaller image, no debug prints
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$ROOT/scripts/build_uefi.sh" "${1:-DEBUG}"
