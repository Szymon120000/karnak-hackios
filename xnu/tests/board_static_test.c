/*
 * Host-side sanity test for the karnak XNU board header.
 *
 * The full XNU build needs macOS + an Apple KDK, so CI cannot compile the
 * kernel here.  This test at least proves the board constants are
 * self-consistent and match the generated hardware map.
 *
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdio.h>

#include "pexpert/pexpert/arm64/KARNAK.h"

/* Mirror the generated values (include/karnak/hardware_map.h). */
#define HW_GICD_BASE   0x10221000U
#define HW_GICC_BASE   0x10222000U
#define HW_TIMER_HZ    13000000U
#define HW_VTIMER_INT  27
#define HW_UART0_BASE  0x11002000U
#define HW_FB_WIDTH    800U
#define HW_FB_HEIGHT   1280U

int
main(void)
{
	assert(KARNAK_GICD_PA == HW_GICD_BASE);
	assert(KARNAK_GICC_PA == HW_GICC_BASE);
	assert(KARNAK_COUNTER_HZ == HW_TIMER_HZ);
	assert(KARNAK_TIMER_INTID == HW_VTIMER_INT);
	assert(KARNAK_UART0_PA == HW_UART0_BASE);
	assert(KARNAK_FB_WIDTH == HW_FB_WIDTH);
	assert(KARNAK_FB_HEIGHT == HW_FB_HEIGHT);
	assert(KARNAK_FB_STRIDE == HW_FB_WIDTH * 4);
	assert(KARNAK_UART_BAUD == 921600);
	assert(KARNAK_SMP_MAX_CPUS == 4);
	assert(HAS_GIC_V3 == 0);
	assert(KARNAK_GICV_PA == 0x10224000U);
	assert(KARNAK_GICH_PA == 0x10226000U);

	printf("karnak board header: ok\n");
	return 0;
}
