/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * karnak ExitBootServices -> XNU entry.
 *
 * karnak's UEFI runs at EL2 (MediaTek PrePi sets HCR_EL2.TGE), while XNU
 * expects EL1.  This routine:
 *
 *   1. cleans the kernel, boot_args and loader ranges to the point of
 *      coherency and invalidates their I-cache lines,
 *   2. at EL2 sets up an EL1 state with the MMU and caches off and drops
 *      through ELR_EL2/SPSR_EL2,
 *   3. at EL1 disables stage-1 translation and caches in SCTLR_EL1,
 *   4. branches to the entry point with x0 = boot_args.
 *
 * No stack, no firmware calls.  All addresses are physical/identity-mapped.
 */
#include <karnak/hardware_map.h>

	.text

/* Fill \rows rows starting at \start with the 32-bit colour in w9.
 * x10 = base, x11 = row bytes, x12 = width; clobbers x14-x18. */
	.macro	KARNAK_FILL_ROWS start, rows
	mov	x14, #(\start)
	mul	x15, x14, x11
	add	x14, x10, x15
	mov	x16, #(\rows)
1:
	mov	x17, x14
	mov	x18, x12
2:
	str	w9, [x17], #4
	subs	x18, x18, #1
	b.ne	2b
	add	x14, x14, x11
	subs	x16, x16, #1
	b.ne	1b
	.endm

/*
 * Clean [x0, x1) to PoC and invalidate its I-cache lines to PoU.
 * Clobbers x0-x8.  Empty ranges are skipped.
 */
	.type	KarnakHandoffCleanRange, %function
KarnakHandoffCleanRange:
	cmp	x1, x0
	b.ls	3f
	mov	x8, x0
	mrs	x2, ctr_el0

	/* D-cache line: 4 << DminLine. */
	ubfx	x3, x2, #16, #4
	mov	x4, #4
	lsl	x3, x4, x3
	sub	x5, x3, #1
	bic	x0, x0, x5
1:	dc	cvac, x0
	add	x0, x0, x3
	cmp	x0, x1
	b.lo	1b
	dsb	sy

	/* I-cache line: 4 << IminLine. */
	and	x3, x2, #0xf
	lsl	x3, x4, x3
	sub	x5, x3, #1
	bic	x0, x8, x5
2:	ic	ivau, x0
	add	x0, x0, x3
	cmp	x0, x1
	b.lo	2b
	dsb	sy
	isb

3:	ret

/*
 * VOID KarnakXnuHandoff (UINT64 Entry, UINT64 BootArgs,
 *                        const KARNAK_HANDOFF_RANGES *Ranges)
 */
	.global	KarnakXnuHandoff
	.type	KarnakXnuHandoff, %function
KarnakXnuHandoff:
	msr	daifset, #0xf
	dsb	sy

	mov	x19, x0			/* entry */
	mov	x20, x1			/* boot_args */
	mov	x21, x2			/* ranges */

	ldp	x0, x1, [x21, #0]
	bl	KarnakHandoffCleanRange
	ldp	x0, x1, [x21, #16]
	bl	KarnakHandoffCleanRange
	ldp	x0, x1, [x21, #32]
	bl	KarnakHandoffCleanRange
	ldp	x0, x1, [x21, #48]
	bl	KarnakHandoffCleanRange
	dsb	sy

	mrs	x22, CurrentEL
	lsr	x22, x22, #2
	cmp	x22, #1
	b.eq	.Lel1
	cmp	x22, #2
	b.eq	.Lel2

	/* EL3 is not supported by this handoff; park rather than jump blind. */
	b	.Lpark

.Lel2:
	/*
	 * Clear TGE/E2H/VM and friends, keep RW=1 so EL1 is AArch64.
	 * PrePi armed TGE=1 for the firmware; XNU must not inherit it.
	 */
	mov	x0, #1
	lsl	x0, x0, #31
	msr	hcr_el2, x0
	isb

	/* FP/SIMD available at EL1 and EL0. */
	movz	x0, #0x30, lsl #16		/* 0x00300000 */
	msr	cpacr_el1, x0
	isb

	/* Let EL1 use the physical counter/timer (XNU's arch timer). */
	mov	x0, #3
	msr	cnthctl_el2, x0

	/*
	 * EL1 control state: RES1 bits set, MMU (M), D-cache (C) and I-cache (I)
	 * off.  XNU installs its own tables and turns them back on.
	 */
	ldr	x0, =0x30D00800
	msr	sctlr_el1, x0
	isb

	/* Return to EL1h with all interrupts masked, x0 = boot_args. */
	ldr	x0, =0x3C5
	msr	spsr_el2, x0
	msr	elr_el2, x19
	mov	x0, x20
	dsb	sy
	isb
	eret

.Lel1:
	ldr	x0, =0x30D00800
	dsb	sy
	msr	sctlr_el1, x0
	isb
	tlbi	vmalle1
	dsb	sy
	isb

.Lenter:
	mov	x0, x20
	br	x19

.Lpark:
	wfi
	b	.Lpark

/*
 * Bring-up payload.  Entered with the MMU and caches off and x0 = boot_args.
 * Uses only registers and the direct physical framebuffer, so it is safe
 * after ExitBootServices.  See KarnakHandoffLib.h for the colour code.
 */
	.global	KarnakHandoffTestPayload
	.type	KarnakHandoffTestPayload, %function
KarnakHandoffTestPayload:
	ldr	x10, =KARN_FB_BASE_FALLBACK	/* framebuffer base */
	mov	x11, #KARN_FB_STRIDE		/* row bytes */
	mov	x12, #KARN_FB_WIDTH		/* pixels per row */
	mov	x13, #KARN_FB_HEIGHT		/* rows */

	/* White over the first 200 rows: "the payload actually ran". */
	ldr	w9, =0xFFFFFFFF
	KARNAK_FILL_ROWS 0, 200

	/* Choose the result colour from boot_args and CurrentEL. */
	ldr	w1, [x0]			/* Revision | Version << 16 */
	mov	w2, #0x00020002
	cmp	w1, w2
	b.ne	.Lpayload_bad_args

	mrs	x3, CurrentEL
	lsr	x3, x3, #2
	cmp	x3, #1
	b.eq	.Lpayload_el1
	cmp	x3, #2
	b.eq	.Lpayload_el2
	ldr	w9, =0xFFFF00FF			/* magenta: EL3 */
	b	.Lpayload_fill

.Lpayload_el1:
	ldr	w9, =0xFF00FF00			/* green: EL1, success */
	b	.Lpayload_fill

.Lpayload_el2:
	ldr	w9, =0xFF00FFFF			/* yellow: still EL2 */
	b	.Lpayload_fill

.Lpayload_bad_args:
	ldr	w9, =0xFF0000FF			/* red: boot_args wrong */

.Lpayload_fill:
	KARNAK_FILL_ROWS 200, (KARN_FB_HEIGHT - 200)

.Lpayload_hang:
	wfi
	b	.Lpayload_hang

	/* Emit the literal pool before the end label so the payload's own
	 * constants fall inside the reported range. */
	.ltorg

	.global	KarnakHandoffTestPayloadEnd
KarnakHandoffTestPayloadEnd:
