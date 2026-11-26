#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Check that the XNU board header agrees with the generated hardware map.

`include/karnak/hardware_map.h` is generated from the stock device tree by
`tools/extract_karnak_hw.py`; `xnu/overlay/.../KARNAK.h` duplicates those
values for the kernel, where the generator's names and types are not
available.  A silent divergence between the two has already bitten the
framebuffer fallback, so compare every shared constant here.  This needs no
libfdt and runs in CI next to the host-side board test.
"""
from __future__ import annotations

import ast
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAP = ROOT / "include/karnak/hardware_map.h"
BOARD = ROOT / "xnu/overlay/pexpert/pexpert/arm64/KARNAK.h"

# hardware_map.h name -> KARNAK.h name
SHARED = {
    "KARN_GICD_BASE": "KARNAK_GICD_PA",
    "KARN_GICC_BASE": "KARNAK_GICC_PA",
    "KARN_GICV_BASE": "KARNAK_GICV_PA",
    "KARN_GICH_BASE": "KARNAK_GICH_PA",
    "KARN_PPI_VIRTUAL": "KARNAK_TIMER_INTID",
    "KARN_PPI_NONSECURE_PHYS": "KARNAK_PHYS_TIMER_INTID",
    "KARN_SGI_IPI": "KARNAK_IPI_SGI",
    "KARN_TIMER_FREQ_HZ": "KARNAK_COUNTER_HZ",
    "KARN_UART0_BASE": "KARNAK_UART0_PA",
    "KARN_UART0_CLOCK_HZ": "KARNAK_UART_CLOCK_HZ",
    "KARN_UART0_BAUD": "KARNAK_UART_BAUD",
    "KARN_UART_THR": "KARNAK_UART_THR",
    "KARN_UART_FCR": "KARNAK_UART_FCR",
    "KARN_UART_LCR": "KARNAK_UART_LCR",
    "KARN_UART_MCR": "KARNAK_UART_MCR",
    "KARN_UART_LSR": "KARNAK_UART_LSR",
    "KARN_FB_WIDTH": "KARNAK_FB_WIDTH",
    "KARN_FB_HEIGHT": "KARNAK_FB_HEIGHT",
    "KARN_FB_STRIDE": "KARNAK_FB_STRIDE",
    "KARN_FB_BPP": "KARNAK_FB_BPP",
    "KARN_FB_BASE_FALLBACK": "KARNAK_FB_BASE_FALLBACK",
    "KARN_EMMC_BASE": "KARNAK_EMMC_PA",
    "KARN_SDCARD_BASE": "KARNAK_SDCARD_PA",
}

DEFINE = re.compile(r"^\s*#define\s+(\w+)\s+(.+?)\s*(?:/\*.*)?$")


def macros(path: pathlib.Path) -> dict[str, str]:
    found: dict[str, str] = {}
    for line in path.read_text().splitlines():
        match = DEFINE.match(line)
        if match:
            found[match.group(1)] = match.group(2).strip()
    return found


def number(expr: str, values: dict[str, str]) -> int:
    """Evaluate a simple integer expression made of macros and + - * ( )."""
    expr = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b", r"\1", expr)
    tree = ast.parse(expr, mode="eval")

    def walk(node: ast.AST) -> int:
        if isinstance(node, ast.Expression):
            return walk(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.Name):
            if node.id not in values:
                raise KeyError(node.id)
            return number(values[node.id], values)
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            return -walk(node.operand)
        if isinstance(node, ast.BinOp):
            left, right = walk(node.left), walk(node.right)
            if isinstance(node.op, ast.Add):
                return left + right
            if isinstance(node.op, ast.Sub):
                return left - right
            if isinstance(node.op, ast.Mult):
                return left * right
            if isinstance(node.op, ast.Div):
                return left // right
        raise ValueError(f"unsupported expression: {ast.dump(node)}")

    return walk(tree)


def main() -> int:
    hardware = macros(MAP)
    board = macros(BOARD)

    failures: list[str] = []
    checked = 0
    for source, target in SHARED.items():
        if source not in hardware:
            failures.append(f"{source} missing from {MAP.relative_to(ROOT)}")
            continue
        if target not in board:
            failures.append(f"{target} missing from {BOARD.relative_to(ROOT)}")
            continue
        expected = number(hardware[source], hardware)
        actual = number(board[target], board)
        checked += 1
        if expected != actual:
            failures.append(
                f"{target} = {actual:#x}, {source} = {expected:#x}")

    # Stride and the fallback base are expressions in the kernel header.
    if "KARNAK_FB_STRIDE" not in board:
        failures.append("KARNAK_FB_STRIDE missing from KARNAK.h")
    elif number(hardware["KARN_FB_WIDTH"], hardware) * 4 != number(
            hardware["KARN_FB_STRIDE"], hardware):
        failures.append("hardware_map.h framebuffer stride is not width*4")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print(f"karnak hardware map: {checked} constants match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
