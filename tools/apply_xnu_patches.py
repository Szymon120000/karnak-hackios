#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Stage the karnak XNU overlay and apply the board patch series.

The overlay (xnu/overlay) contains *new* files and mirrors the XNU source tree,
so `xnu/overlay/pexpert/pexpert/arm64/KARNAK.h` lands at
`<xnu>/pexpert/pexpert/arm64/KARNAK.h`.  The patches in xnu/patches only touch
upstream files.

Usage:
    tools/apply_xnu_patches.py <xnu-dir> [--check]

--check never writes: it verifies that the overlay is already staged (or would
be copied) and that each patch applies cleanly, or has already been applied.
It is what CI runs against a pristine checkout.

The patch application is idempotent: re-running after a partial stage resumes
instead of duplicating hunks.
"""

from __future__ import annotations

import argparse
import filecmp
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OVERLAY = ROOT / "xnu" / "overlay"
PATCH_DIR = ROOT / "xnu" / "patches"


def git(xnu: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", "-C", str(xnu), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=check,
    )


def overlay_files() -> list[Path]:
    if not OVERLAY.is_dir():
        raise SystemExit(f"missing overlay directory: {OVERLAY}")
    return sorted(path for path in OVERLAY.rglob("*") if path.is_file())


def is_staged(source: Path, xnu: Path) -> bool:
    relative = source.relative_to(OVERLAY)
    target = xnu / relative
    return target.is_file() and filecmp.cmp(source, target, shallow=False)


def stage_overlay(xnu: Path, check: bool) -> int:
    changed = 0
    for source in overlay_files():
        relative = source.relative_to(OVERLAY)
        target = xnu / relative
        if is_staged(source, xnu):
            continue
        changed += 1
        if check:
            print(f"would stage {relative}")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        print(f"staged {relative}")
    return changed


def patch_state(xnu: Path, patch: Path) -> str:
    """Return 'applies', 'applied' or 'conflict'."""
    forward = git(xnu, "apply", "--check", str(patch), check=False)
    if forward.returncode == 0:
        return "applies"
    reverse = git(xnu, "apply", "--reverse", "--check", str(patch), check=False)
    if reverse.returncode == 0:
        return "applied"
    return "conflict"


def apply_patches(xnu: Path, check: bool) -> int:
    patches = sorted(PATCH_DIR.glob("*.patch"))
    if not patches:
        raise SystemExit(f"no patches in {PATCH_DIR}")
    applied_now = 0
    for patch in patches:
        state = patch_state(xnu, patch)
        if state == "applied":
            print(f"already applied {patch.name}")
        elif state == "applies":
            if check:
                print(f"would apply {patch.name}")
            else:
                git(xnu, "apply", str(patch))
                print(f"applied {patch.name}")
            applied_now += 1
        else:
            detail = git(xnu, "apply", "--check", str(patch), check=False)
            raise SystemExit(
                f"{patch.name} does not apply and is not already present:\n"
                f"{detail.stderr.strip()}"
            )
    return applied_now


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("xnu", type=Path, help="XNU source tree")
    parser.add_argument("--check", action="store_true",
                        help="verify only; never modify the tree")
    args = parser.parse_args()

    xnu = args.xnu.resolve()
    if not (xnu / "makedefs" / "MakeInc.def").is_file():
        raise SystemExit(f"{xnu} does not look like an XNU source tree")
    if not (xnu / ".git").exists():
        raise SystemExit(
            f"{xnu} is not a git checkout; patches need git apply --check")

    pending_overlay = stage_overlay(xnu, args.check)
    pending_patches = apply_patches(xnu, args.check)

    summary = (
        f"{len(overlay_files())} overlay file(s), {len(sorted(PATCH_DIR.glob('*.patch')))} "
        f"patch(es); {pending_overlay} overlay / {pending_patches} patch pending"
    )
    if args.check and (pending_overlay or pending_patches):
        print(f"check: {summary} (dry run, nothing written)")
    else:
        print(f"karnak XNU stage: {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
