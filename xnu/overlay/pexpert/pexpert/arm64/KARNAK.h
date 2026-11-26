/*
 * karnak (Amazon Fire HD 8 2018, MediaTek MT8163) board configuration.
 *
 * This header is the single place where karnak differs from the generic
 * "sweet" bring-up board that the hackios XNU patch series targets.  It is
 * standalone (no XNU includes) so it can be compiled by the host-side test in
 * xnu/tests/.
 *
 * Values are from include/karnak/hardware_map.h, which is generated from
 * Amazon's 3.18 device tree.  Do not edit those constants here without
 * updating the generator.
 *
 * Integration status: selected by ARM64_BOARD_CONFIG_KARNAK from
 * pexpert/pexpert/arm64/board_config.h (xnu/patches/0001).  The polled early
 * console is wired up by xnu/patches/0002; see xnu/README.md for what still
 * has to be implemented before a kernel boots.
 */

#ifndef _PEXPERT_ARM64_KARNAK_H
#define _PEXPERT_ARM64_KARNAK_H

#define KARNAK                    1

#define NO_MONITOR                1
#define NO_ECORE                  1

/* One cluster of four Cortex-A53. */
#define KARNAK_SMP_MAX_CPUS       4

#define ARM_ARCH_TIMER
#define __ARM_ARCH__              8
#define __ARM_VMSA__              8
#define __ARM_VFP__               4
#define __ARM_COHERENT_CACHE__    1
#define __ARM_DEBUG__             7
#define __ARM_ENABLE_SWAP__       1

/* Cortex-A53 is ARMv8.0-A: optional CRC/crypto, no LSE, no PAN. */

/*
 * Cortex-A53 is ARMv8.0: no PAN, no LSE.  karnak uses 4 KiB pages, so
 * __ARM_16K_PG__ must stay undefined: a few XNU headers test it with
 * #ifdef, not #if.
 */
#undef  __ARM_PAN_AVAILABLE__

/* ------------------------------------------------------------------ GIC */
/* MT8163 uses a GIC-400 (GICv2).  DTS: interrupt-controller@10221000. */
#define KARNAK_GICD_PA            0x10221000ULL
#define KARNAK_GICC_PA            0x10222000ULL
#define KARNAK_GICV_PA            0x10224000ULL
#define KARNAK_GICH_PA            0x10226000ULL
#define HAS_GIC_V3                0

#define KARNAK_GIC_SPURIOUS_IRQ   1023
#define KARNAK_IPI_SGI            1

/* GICv2 distributor registers (ARM IHI 0048B). */
#define KARNAK_GICD_CTLR           0x000
#define KARNAK_GICD_TYPER          0x004
#define KARNAK_GICD_IGROUPR        0x080
#define KARNAK_GICD_ISENABLER      0x100
#define KARNAK_GICD_ICENABLER      0x180
#define KARNAK_GICD_ICPENDR        0x280
#define KARNAK_GICD_IPRIORITYR     0x400
#define KARNAK_GICD_ITARGETSR      0x800
#define KARNAK_GICD_SGIR           0xF00

/*
 * karnak's UEFI runs non-secure.  In the non-secure view of GICv2 the
 * group-1 enable is bit 0 (the secure view calls it EnableGrp0).
 */
#define KARNAK_GICD_CTLR_ENABLEGRP1 0x1
#define KARNAK_GICD_TYPER_LINES    0x1F  /* ITLinesNumber */

#define KARNAK_GICD_SGIR_SGI_SHIFT    0
#define KARNAK_GICD_SGIR_TARGET_SHIFT 16
#define KARNAK_GICD_SGIR_FILTER_SHIFT 24
#define KARNAK_GICD_SGIR_FILTER_LIST  0x0

#define KARNAK_GICC_CTLR           0x000
#define KARNAK_GICC_CTLR_ENABLEGRP1 0x1
#define KARNAK_GICC_CTLR_EOIMODE    (1U << 9)
#define KARNAK_GICC_CTLR_FIQEN      (1U << 3)
#define KARNAK_GICC_PMR            0x004
#define KARNAK_GICC_BPR            0x008
#define KARNAK_GICC_IAR            0x00C
#define KARNAK_GICC_EOIR           0x010
#define KARNAK_GICC_IAR_INTID_MASK 0x3FFU

/* ----------------------------------------------------------------- timer */
/* DTS timer:clock-frequency = 13000000.  PPIs 13/14/11/10 -> 29/30/27/26. */
#define KARNAK_COUNTER_HZ         13000000ULL
#define KARNAK_TIMER_INTID        27      /* virtual timer INTID (PPI 11) */
#define KARNAK_PHYS_TIMER_INTID   30      /* EL1 physical timer (PPI 14) */

/* ------------------------------------------------------------- framebuffer */
/* 800x1280 portrait, 32bpp.  The live base comes from /chosen:atag,videolfb. */
#define KARNAK_FB_WIDTH           800U
#define KARNAK_FB_HEIGHT          1280U
#define KARNAK_FB_STRIDE          (KARNAK_FB_WIDTH * 4U)
#define KARNAK_FB_BPP             32U
/* Retail karnak: fb_top 0x99600000 minus the 14 MiB reservation. */
#define KARNAK_FB_BASE_FALLBACK   0x98800000ULL

/* ------------------------------------------------------------------- UART */
/* DTS uart0@11002000, 26 MHz, 921600 8N1 (LK bootargs console=ttyS0). */
#define KARNAK_UART               1
#define KARNAK_UART0_PA           0x11002000ULL
#define KARNAK_UART_CLOCK_HZ      26000000U
#define KARNAK_UART_BAUD          921600U

/* 16550-compatible register offsets, relative to KARNAK_UART0_PA. */
#define KARNAK_UART_THR           0x00
#define KARNAK_UART_RBR           0x00
#define KARNAK_UART_IER           0x04
#define KARNAK_UART_FCR           0x08
#define KARNAK_UART_LCR           0x0C
#define KARNAK_UART_MCR           0x10
#define KARNAK_UART_LSR           0x14
#define KARNAK_UART_LSR_DR        0x01
#define KARNAK_UART_LSR_THRE      0x20

/* --------------------------------------------------------------- storage */
/* eMMC is MSDC0 at 0x11230000; there is no UFS on this SoC. */
#define KARNAK_EMMC_PA            0x11230000ULL
#define KARNAK_SDCARD_PA          0x11240000ULL

#endif /* _PEXPERT_ARM64_KARNAK_H */
