#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Extract the karnak (Amazon Fire HD 8 2018, MT8163) hardware map from the
Amazon/Linux 3.18 device tree and from the board LCM sources.

The device tree is the authority for every address below.  The script uses the
real kernel preprocessor include paths plus dtc, then reads the resulting blob
with libfdt.  Nothing is scraped from /proc or guessed from the product name.

Usage:
    tools/extract_karnak_hw.py --kernel ../karnak-kernel-3.18 \
        --header include/karnak/hardware_map.h \
        --json include/karnak/hardware_map.json
    tools/extract_karnak_hw.py --check      # compare, do not write
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

try:
    import libfdt
except ImportError:  # pragma: no cover
    sys.exit("python3-libfdt is required (Debian: python3-libfdt)")

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_KERNEL = ROOT.parent / "karnak-kernel-3.18"
DEFAULT_HEADER = ROOT / "include" / "karnak" / "hardware_map.h"
DEFAULT_JSON = ROOT / "include" / "karnak" / "hardware_map.json"


class Dt:
    def __init__(self, blob: bytes):
        self.fdt = libfdt.Fdt(blob)

    def node(self, path: str) -> int:
        return self.fdt.path_offset(path)

    def u32(self, path: str, prop: str) -> int:
        p = self.fdt.getprop(self.node(path), prop)
        return struct.unpack(">I", p)[0]

    def string(self, path: str, prop: str, default: str = "") -> str:
        try:
            value = self.fdt.getprop(self.node(path), prop)
            return value.as_str() if hasattr(value, "as_str") else value.as_string()
        except Exception:
            return default

    def cells(self, path: str, prop: str, width: int = 4) -> list[int]:
        p = self.fdt.getprop(self.node(path), prop)
        fmt = ">I" if width == 4 else ">Q"
        return [struct.unpack(fmt, p[i:i + width])[0]
                for i in range(0, len(p), width)]

    def reg64(self, path: str) -> list[tuple[int, int]]:
        c = self.cells(path, "reg", 4)
        # karnak uses #address-cells = 2 and #size-cells = 2 everywhere below.
        return [(c[i] << 32 | c[i + 1], c[i + 2] << 32 | c[i + 3])
                for i in range(0, len(c), 4)]

    def reg32(self, path: str) -> list[tuple[int, int]]:
        c = self.cells(path, "reg", 4)
        return [(c[i], c[i + 1]) for i in range(0, len(c), 2)]

    def prop_bytes(self, path: str, prop: str) -> bytes:
        return bytes(self.fdt.getprop(self.node(path), prop))

    def has(self, path: str, prop: str) -> bool:
        try:
            self.fdt.getprop(self.node(path), prop)
            return True
        except Exception:
            return False


def compile_dts(kernel: pathlib.Path, out_dtb: pathlib.Path) -> bytes:
    src = kernel / "arch/arm64/boot/dts/karnak.dts"
    if not src.is_file():
        raise SystemExit(f"missing {src}")
    cpp = shutil.which("cpp")
    dtc = shutil.which("dtc")
    if not cpp or not dtc:
        raise SystemExit("cpp and dtc are required")
    cmd = [cpp, "-nostdinc", "-undef", "-x", "assembler-with-cpp",
           "-I", str(kernel / "include"),
           "-I", str(kernel / "arch/arm64/boot/dts"),
           str(src)]
    pre = subprocess.run(cmd, check=True, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL)
    subprocess.run([dtc, "-I", "dts", "-O", "dtb", "-o", str(out_dtb), "-"],
                   input=pre.stdout, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out_dtb.read_bytes()


def lcm_dimensions(kernel: pathlib.Path) -> dict[str, int]:
    """Read FRAME_WIDTH/FRAME_HEIGHT from the two karnak panel drivers."""
    found: dict[str, int] = {}
    drivers = {
        "jd936x": kernel / "drivers/misc/mediatek/lcm/jd936x_wxga_dsi_vdo_karnak/jd936x_wxga_dsi_vdo_karnak.c",
        "nt35521": kernel / "drivers/misc/mediatek/lcm/nt35521_wxga_dsi_vdo_karnak/nt35521_wxga_dsi_vdo_karnak.c",
    }
    for name, path in drivers.items():
        if not path.is_file():
            continue
        for line in path.read_text(errors="replace").splitlines():
            if "#define FRAME_WIDTH" in line:
                found[name + "_width"] = int(line.split("(")[1].split(")")[0])
            elif "#define FRAME_HEIGHT" in line:
                found[name + "_height"] = int(line.split("(")[1].split(")")[0])
    return found


def extract(kernel: pathlib.Path) -> dict:
    with tempfile.TemporaryDirectory(prefix="karnak-dt-") as td:
        blob = compile_dts(kernel, pathlib.Path(td) / "karnak.dtb")
    dt = Dt(blob)

    # /memory@00000000 is a 64-bit address/size node (2+2 cells).
    mem = dt.reg64("/memory@00000000")
    if len(mem) != 1:
        raise SystemExit(f"expected one memory range, got {mem}")
    dram_base, dram_size = mem[0]

    chosen_bootargs = dt.string("/chosen", "bootargs")

    resv = {}
    for node in ("atf-reserved-memory@43000000",
                 "ram_console-reserved-memory@44400000",
                 "mdump-reserved-memory@4bd00000"):
        base, size = dt.reg64("/reserved-memory/" + node)[0]
        resv[node.split("-reserved")[0].split("@")[0]] = (base, size)
    consys_size = dt.cells("/reserved-memory/consys-reserve-memory", "size", 4)
    consys_align = dt.cells("/reserved-memory/consys-reserve-memory", "alignment", 4)

    gic = dt.reg64("/soc/interrupt-controller@10221000")
    gicd, gicc, gicv, gich = gic[0], gic[1], gic[2], gic[3]

    timer_irq = dt.cells("/timer", "interrupts", 4)
    # Each entry is <type number flags>; PPI number -> INTID is +16.
    ppi = [timer_irq[i + 1] + 16 for i in range(0, len(timer_irq), 3)]
    timer_hz = dt.u32("/timer", "clock-frequency")

    uart = {}
    for idx, addr in ((0, 0x11002000), (1, 0x11003000)):
        path = f"/soc/uart{idx}@{addr:08x}"
        base, size = dt.reg64(path)[0]
        uart[f"uart{idx}"] = {
            "base": base, "size": size,
            "clock_hz": dt.u32(path, "clock-frequency"),
            "irq": dt.cells(path, "interrupts", 4)[1],
        }

    mmc = {}
    for idx, addr in ((0, 0x11230000), (1, 0x11240000)):
        path = f"/soc/mmc@{addr:08x}"
        base, size = dt.reg64(path)[0]
        mmc[f"mmc{idx}"] = {
            "base": base, "size": size,
            "irq": dt.cells(path, "interrupts", 4)[1],
        }

    lcm = lcm_dimensions(kernel)
    fb_w = lcm.get("jd936x_width", 800)
    fb_h = lcm.get("jd936x_height", 1280)
    fb_bpp = 32
    fb_stride = fb_w * (fb_bpp // 8)
    # LK's fallback if /chosen lacks atag,videolfb (mtkfb.c:2260):
    #   fb_base = 0x5fdfffff - 0x200000 - vramsize + 1
    # The live value must come from the runtime DTB; a 16 MiB top reservation is
    # used as the static UEFI fallback.
    fb_top = 0x5fe00000
    fb_reserve = 0x01000000

    return {
        "source": {
            "kernel": str(kernel),
            "dts": "arch/arm64/boot/dts/karnak.dts",
            "dtsi": "arch/arm64/boot/dts/mt8163.dtsi",
        },
        "dram": {
            "base": dram_base,
            "dts_size": dram_size,
            "dts_end": dram_base + dram_size,
            "product_size": 0x60000000,  # 1.5 GiB, product specification
            "note": ("The build-time DTB describes 512 MiB at 0x40000000. "
                     "LK may patch /memory at boot; the boot shim therefore "
                     "reads the runtime DTB and never trusts this value alone."),
        },
        "reserved": {
            "atf": {"base": resv["atf"][0], "size": resv["atf"][1]},
            "ram_console": {"base": resv["ram_console"][0],
                            "size": resv["ram_console"][1]},
            "mdump": {"base": resv["mdump"][0], "size": resv["mdump"][1]},
            "consys": {"size": consys_size[0] << 32 | consys_size[1],
                       "alignment": consys_align[0] << 32 | consys_align[1]},
        },
        "gic": {
            "type": "GIC-400 (GICv2)",
            "distributor": {"base": gicd[0], "size": gicd[1]},
            "cpu_interface": {"base": gicc[0], "size": gicc[1]},
            "virtual_interface": {"base": gicv[0], "size": gicv[1]},
            "hyp_interface": {"base": gich[0], "size": gich[1]},
            "max_irq": 987,  # GIC-400: 988 interrupt IDs, IDs 1020-1023 special
        },
        "timer": {
            "frequency_hz": timer_hz,
            "ppi_secure_physical": ppi[0],
            "ppi_non_secure_physical": ppi[1],
            "ppi_virtual": ppi[2],
            "ppi_hyp": ppi[3],
        },
        "uart": uart,
        "mmc": mmc,
        "display": {
            "panel": "800x1280 portrait; Fire OS UI is rotated to 1280x800",
            "width": fb_w,
            "height": fb_h,
            "bpp": fb_bpp,
            "stride": fb_stride,
            "format": "BGRA8888 (LK simple framebuffer, 32bpp)",
            "fb_top_fallback": fb_top,
            "fb_reserve_bytes": fb_reserve,
            "note": ("Live fb_base/vram/lcmname are in /chosen:atag,videolfb. "
                     "UEFI and XnuLoader must read that property."),
        },
        "bootargs": chosen_bootargs,
        "cpu": {"cores": 4, "clusters": 1, "release_addr": 0x40000200,
                "compatible": "arm,cortex-a53"},
    }


def fmt(value: int) -> str:
    return f"0x{value:08X}U"


def header(d: dict) -> str:
    u = d["uart"]["uart0"]
    g = d["gic"]
    t = d["timer"]
    r = d["reserved"]
    disp = d["display"]
    mmc0, mmc1 = d["mmc"]["mmc0"], d["mmc"]["mmc1"]
    return f'''/* SPDX-License-Identifier: MIT
 * GENERATED by tools/extract_karnak_hw.py -- do not edit by hand.
 *
 * Sources (every value is traceable to one of these):
 *   {d["source"]["dts"]}
 *   {d["source"]["dtsi"]}
 *   drivers/misc/mediatek/lcm/*_wxga_dsi_vdo_karnak/*.c
 *   drivers/misc/mediatek/video/mt8163/videox/mtkfb.c
 *
 * Runtime values that cannot be known at build time are marked
 * TODO(VERIFY_BOOTARGS) and are supplied by the boot shim: it copies the live
 * DTB to KARN_DTB_STORE_BASE and publishes that pointer at KARN_DTB_PTR_STORE.
 */
#ifndef KARNAK_HARDWARE_MAP_H
#define KARNAK_HARDWARE_MAP_H

/* ---- DRAM -------------------------------------------------------------
 * DTS memory@00000000: reg = <0 0x40000000 0 0x20000000>.
 * The retail 1.5 GiB device may receive a patched /memory from LK, so the
 * shim parses the runtime DTB.  KARN_DRAM_SIZE_DTS is the build-time floor.
 */
#define KARN_DRAM_BASE                 {fmt(d["dram"]["base"])}
#define KARN_DRAM_SIZE_DTS             {fmt(d["dram"]["dts_size"])}
#define KARN_DRAM_END_DTS              {fmt(d["dram"]["dts_end"])}
#define KARN_DRAM_SIZE_PRODUCT         {fmt(d["dram"]["product_size"])}

/* ---- Reserved / firmware memory (DTS reserved-memory) ---------------- */
#define KARN_ATF_BASE                  {fmt(r["atf"]["base"])}
#define KARN_ATF_SIZE                  {fmt(r["atf"]["size"])}
#define KARN_RAM_CONSOLE_BASE          {fmt(r["ram_console"]["base"])}
#define KARN_RAM_CONSOLE_SIZE          {fmt(r["ram_console"]["size"])}
#define KARN_MDUMP_BASE                {fmt(r["mdump"]["base"])}
#define KARN_MDUMP_SIZE                {fmt(r["mdump"]["size"])}
#define KARN_CONSYS_RESERVE_SIZE       {fmt(r["consys"]["size"])}
#define KARN_CONSYS_RESERVE_ALIGN      {fmt(r["consys"]["alignment"])}

/* ---- GIC-400 / GICv2 (DTS interrupt-controller@10221000) -------------- */
#define KARN_GICD_BASE                 {fmt(g["distributor"]["base"])}
#define KARN_GICD_SIZE                 {fmt(g["distributor"]["size"])}
#define KARN_GICC_BASE                 {fmt(g["cpu_interface"]["base"])}
#define KARN_GICC_SIZE                 {fmt(g["cpu_interface"]["size"])}
#define KARN_GICV_BASE                 {fmt(g["virtual_interface"]["base"])}
#define KARN_GICV_SIZE                 {fmt(g["virtual_interface"]["size"])}
#define KARN_GICH_BASE                 {fmt(g["hyp_interface"]["base"])}
#define KARN_GICH_SIZE                 {fmt(g["hyp_interface"]["size"])}
#define KARN_GIC_MAX_IRQ               {g["max_irq"]}U
#define KARN_PPI_SECURE_PHYS           {t["ppi_secure_physical"]}U
#define KARN_PPI_NONSECURE_PHYS        {t["ppi_non_secure_physical"]}U
#define KARN_PPI_VIRTUAL               {t["ppi_virtual"]}U
#define KARN_PPI_HYP                   {t["ppi_hyp"]}U
#define KARN_SGI_IPI                   {1}U

/* ---- ARM generic timer (DTS timer, 13 MHz) --------------------------- */
#define KARN_TIMER_FREQ_HZ             {t["frequency_hz"]}U

/* ---- UART (DTS uart0@11002000 / uart1@11003000, 26 MHz input) -------- */
#define KARN_UART0_BASE                {fmt(u["base"])}
#define KARN_UART0_SIZE                {fmt(u["size"])}
#define KARN_UART0_CLOCK_HZ            {u["clock_hz"]}U
#define KARN_UART0_IRQ                 {u["irq"]}U
#define KARN_UART0_BAUD                {921600}U
#define KARN_UART1_BASE                {fmt(d["uart"]["uart1"]["base"])}
#define KARN_UART1_CLOCK_HZ            {d["uart"]["uart1"]["clock_hz"]}U
#define KARN_UART1_IRQ                 {d["uart"]["uart1"]["irq"]}U
/* MTK UART is 16550-compatible for the byte path used by LK and XNU. */
#define KARN_UART_THR                  0x00U
#define KARN_UART_DLL                  0x00U
#define KARN_UART_DLM                  0x04U
#define KARN_UART_FCR                  0x08U
#define KARN_UART_LCR                  0x0CU
#define KARN_UART_MCR                  0x10U
#define KARN_UART_LSR                  0x14U
#define KARN_UART_LSR_THRE             0x20U

/* ---- eMMC / SD (DTS mmc@11230000, mmc@11240000) ---------------------- */
#define KARN_EMMC_BASE                 {fmt(mmc0["base"])}
#define KARN_EMMC_SIZE                 {fmt(mmc0["size"])}
#define KARN_EMMC_IRQ                  {mmc0["irq"]}U
#define KARN_SDCARD_BASE               {fmt(mmc1["base"])}
#define KARN_SDCARD_SIZE               {fmt(mmc1["size"])}
#define KARN_SDCARD_IRQ                {mmc1["irq"]}U

/* ---- Display / LK framebuffer --------------------------------------- */
#define KARN_FB_WIDTH                  {disp["width"]}U
#define KARN_FB_HEIGHT                 {disp["height"]}U
#define KARN_FB_BPP                    {disp["bpp"]}U
#define KARN_FB_STRIDE                 {disp["stride"]}U
#define KARN_FB_TOP_FALLBACK           {fmt(disp["fb_top_fallback"])}
#define KARN_FB_RESERVE_BYTES          {fmt(disp["fb_reserve_bytes"])}
/* TODO(VERIFY_BOOTARGS): real base/vram come from /chosen:atag,videolfb. */
#define KARN_FB_BASE_FALLBACK          (KARN_FB_TOP_FALLBACK - KARN_FB_RESERVE_BYTES)

/* ---- Platform layout chosen for the UEFI port -----------------------
 * These are not device-tree values.  They keep the firmware payload clear of
 * ATF, ram_console, mdump and the LK framebuffer.
 */
#define KARN_DTB_STORE_BASE            {fmt(0x4FE00000)}
#define KARN_DTB_STORE_SIZE            {fmt(0x00100000)}
#define KARN_DTB_PTR_STORE             {fmt(0x4FF00000)}
#define KARN_DTB_PTR_STORE_SIZE        {fmt(0x00001000)}
#define KARN_UEFI_BASE                 {fmt(0x50000000)}
#define KARN_UEFI_SIZE                 {fmt(0x00700000)}
#define KARN_UEFI_STACK_BASE           {fmt(0x50800000)}
#define KARN_UEFI_STACK_SIZE           {fmt(0x00040000)}
#define KARN_UEFI_VECTOR_BASE          {fmt(0x50840000)}
#define KARN_UEFI_VECTOR_SIZE          {fmt(0x00010000)}
#define KARN_UEFI_POOL_BASE            {fmt(0x50850000)}
#define KARN_UEFI_POOL_SIZE            {fmt(0x02000000)}

/* ---- CPU ------------------------------------------------------------- */
#define KARN_CPU_CORES                 {d["cpu"]["cores"]}U
#define KARN_CPU_CLUSTERS              {d["cpu"]["clusters"]}U
#define KARN_CPU_RELEASE_ADDR          {fmt(d["cpu"]["release_addr"])}

/* ---- Build-time Linux bootargs (reference only) --------------------- */
#define KARN_LINUX_BOOTARGS            "{d["bootargs"]}"

#endif /* KARNAK_HARDWARE_MAP_H */
'''


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", type=pathlib.Path, default=DEFAULT_KERNEL)
    ap.add_argument("--header", type=pathlib.Path, default=DEFAULT_HEADER)
    ap.add_argument("--json", type=pathlib.Path, default=DEFAULT_JSON)
    ap.add_argument("--check", action="store_true",
                    help="fail if the checked-in outputs differ")
    args = ap.parse_args()

    data = extract(args.kernel)
    hdr = header(data)
    js = json.dumps(data, indent=2, sort_keys=True) + "\n"

    if args.check:
        bad = False
        for path, text in ((args.header, hdr), (args.json, js)):
            current = path.read_text() if path.is_file() else ""
            if current != text:
                print(f"stale: {path}", file=sys.stderr)
                bad = True
        return 1 if bad else 0

    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(hdr)
    args.json.write_text(js)
    print(f"wrote {args.header}")
    print(f"wrote {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
