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
 * Integration status: NOT YET WIRED INTO A KERNEL BUILD.  See xnu/README.md.
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
#define __ARM_V8_CRYPTO_EXTENSIONS__ 1

/* Cortex-A53 is ARMv8.0: no PAN, no LSE, no 16K-only restriction. */
#undef  __ARM_PAN_AVAILABLE__
#define __ARM_16K_PG__            0

/* ------------------------------------------------------------------ GIC */
/* MT8163 uses a GIC-400 (GICv2).  DTS: interrupt-controller@10221000. */
#define KARNAK_GICD_PA            0x10221000ULL
#define KARNAK_GICC_PA            0x10222000ULL
#define KARNAK_GICV_PA            0x10224000ULL
#define KARNAK_GICH_PA            0x10226000ULL
#define HAS_GIC_V3                0

#define GIC_SPURIOUS_IRQ          1023

/* GICv2 distributor registers (ARM IHI 0048B). */
#define GICD_CTLR                 0x000
#define GICD_CTLR_ENABLEGRP1      0x2
#define GICD_IGROUPR              0x080
#define GICD_ISENABLER            0x100
#define GICD_IPRIORITYR           0x400
#define GICD_ITARGETSR            0x800
#define GICC_CTLR                 0x000
#define GICC_CTLR_ENABLEGRP1      0x2
#define GICC_PMR                  0x004
#define GICC_BPR                  0x008
#define GICC_IAR                  0x00C
#define GICC_EOIR                 0x010

/* ----------------------------------------------------------------- timer */
/* DTS timer:clock-frequency = 13000000.  PPIs 13/14/11/10 -> 29/30/27/26. */
#define KARNAK_COUNTER_HZ         13000000ULL
#define KARNAK_TIMER_INTID        27      /* virtual timer INTID (PPI 11) */
#define SWEET_VTIMER_INTID        KARNAK_TIMER_INTID
#define SWEET_IPI_SGI             1

/* ------------------------------------------------------------- framebuffer */
/* 800x1280 portrait, 32bpp.  The live base comes from /chosen:atag,videolfb. */
#define KARNAK_FB_WIDTH           800U
#define KARNAK_FB_HEIGHT          1280U
#define KARNAK_FB_STRIDE          (KARNAK_FB_WIDTH * 4U)
#define KARNAK_FB_BPP             32U
#define KARNAK_FB_BASE_FALLBACK   0x5EE00000ULL

/* ------------------------------------------------------------------- UART */
/* DTS uart0@11002000, 26 MHz, 921600 8N1 (LK bootargs console=ttyS0). */
#define KARNAK_UART0_PA           0x11002000ULL
#define KARNAK_UART_CLOCK_HZ      26000000U
#define KARNAK_UART_BAUD          921600U

/* --------------------------------------------------------------- storage */
/* eMMC is MSDC0 at 0x11230000; there is no UFS on this SoC. */
#define KARNAK_EMMC_PA            0x11230000ULL
#define KARNAK_SDCARD_PA          0x11240000ULL

#endif /* _PEXPERT_ARM64_KARNAK_H */
