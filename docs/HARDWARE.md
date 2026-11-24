# karnak hardware map

Device: Amazon Fire HD 8 (8th gen, 2018), `karnak` / KFKAWI.
SoC: MediaTek MT8163V/A (4× Cortex-A53, Mali-T720 MP2).
RAM: 1.5 GB (product) — see the conflict below.

Every address in `include/karnak/hardware_map.h` is generated from:

- `arch/arm64/boot/dts/karnak.dts`
- `arch/arm64/boot/dts/mt8163.dtsi`
- `drivers/misc/mediatek/lcm/jd936x_wxga_dsi_vdo_karnak/`
- `drivers/misc/mediatek/lcm/nt35521_wxga_dsi_vdo_karnak/`
- `drivers/misc/mediatek/video/mt8163/videox/mtkfb.c`

using `tools/extract_karnak_hw.py` (cpp + dtc + libfdt). Regenerate with:

```sh
python3 tools/extract_karnak_hw.py --kernel <karnak-kernel-3.18> --check
```

## Verified values

| Item | Value | Source |
|------|-------|--------|
| DRAM base | `0x40000000` | `/memory@00000000` |
| DRAM size (DTS) | `0x20000000` (512 MiB) | same node |
| GIC distributor | `0x10221000` | `/soc/interrupt-controller@10221000` |
| GIC CPU interface | `0x10222000` | same |
| GIC virtual interface | `0x10224000` | same |
| GIC hypervisor interface | `0x10226000` | same |
| Arch timer counter | 13 MHz | `/timer:clock-frequency` |
| Timer PPIs | 13 / 14 / 11 / 10 (INTID 29/30/27/26) | `/timer:interrupts` |
| UART0 | `0x11002000`, 26 MHz, IRQ 91 | `/soc/uart0@11002000` |
| UART1 | `0x11003000`, 26 MHz | `/soc/uart1@11003000` |
| eMMC (MSDC0) | `0x11230000`, IRQ 79 | `/soc/mmc@11230000` |
| microSD (MSDC1) | `0x11240000`, IRQ 80 | `/soc/mmc@11240000` |
| ATF reserved | `0x43000000` + `0x30000` | `/reserved-memory/atf-*` |
| ram_console | `0x44400000` + `0x200000` | `/reserved-memory/ram_console-*` |
| mdump | `0x4bd00000` + `0x300000` | `/reserved-memory/mdump-*` |
| consys reserve | 2 MiB, 2 MiB aligned | `/reserved-memory/consys-*` |
| Panel | 800×1280 portrait | LCM driver `FRAME_WIDTH/HEIGHT` |
| LK bootargs | `console=ttyS0,921600n1 ...` | `/chosen:bootargs` |

## Open issue: DRAM size

All `karnak` DTS variants declare **512 MiB at `0x40000000`** (ends at
`0x60000000`), while the product is documented as **1.5 GB** and MT8163
supports up to 2 GB.

Two possibilities:

1. LK patches `/memory` in the live DTB before jumping to the kernel.
2. The shipped DTB is simply conservative and the kernel is told the real
   size elsewhere.

`KarnakBootShim` therefore:

- copies and parses LK's **live** DTB,
- reports the size on UART,
- and `PlatformMemoryMapLib` extends the map to that size.

If the live DTB still says 512 MiB, only 512 MiB will be mapped. Verify on a
real device with `tools/probe_karnak.sh` (fastboot `oem device-info` / reading
`/proc/device-tree/memory/reg` from the stock boot).

`// TODO(VERIFY_DTS)` marks any consumer that depends on this.

## Framebuffer

LK passes the real framebuffer in `/chosen:atag,videolfb`
(`struct tag_videolfb { u64 fb_base; u32 islcmfound; u32 fps; u32 vram;
char lcmname[]; }`, see `mtkfb.c`). The fallback in `mtkfb.c` is:

```
fb_base = 0x5fdfffff - 0x200000 - vramsize + 1
```

The firmware reserves a 16 MiB window at `0x5ee00000..0x5fe00000` when the
property is absent, and republishes the parsed values through
`KarnakFdtLib`.
