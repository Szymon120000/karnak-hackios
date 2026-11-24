/*
 * KarnakBootShim - MediaTek LK -> EDK2 handoff for the Fire HD 8 (2018).
 *
 * LK jumps to the boot.img kernel with:
 *   x0 = live device-tree blob (possibly patched with the real DRAM size)
 *   MMU off, D-cache usually off
 *
 * We:
 *   1. copy the live DTB to DTB_STORE (bounded by DTB_MAX),
 *   2. publish the copy's address at DTB_PTR_STORE for KarnakDtbLoaderLib,
 *   3. relocate the appended EDK2 FD to UEFI_BASE,
 *   4. branch to the FD entry point.
 *
 * All constants come from the Makefile, which gets them from
 * include/karnak/hardware_map.h.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

_Head:
	/* ARM64 Linux Image header (LK's booti recognises it). */
	b	_Start			/* code0 */
	.long	0			/* code1: reserved */
	.quad	0			/* text_offset */
	.quad	UEFI_SIZE + 0x1000	/* image_size: FD + header slack */
	.quad	0			/* flags: LE, 4K pages */
	.quad	0			/* res2 */
	.quad	0			/* res3 */
	.quad	0			/* res4 */
	.ascii	"ARM\x64"		/* image magic, bytes 41 52 4d 64 */
	.long	0			/* res5 */
	.ascii	"KARNAK"		/* vendor tag (outside the header) */

	.balign	4
_Start:
	/* Save the live DTB pointer. */
	mov	x9, x0

	/* Clear the published pointer so a rejected DTB reads as "absent". */
	ldr	x16, =DTB_PTR_STORE
	str	xzr, [x16]

	/* Validate the FDT magic (0xD00DFEED big-endian). */
	cbz	x9, 2f
	ldr	w10, [x9]		/* bytes d0 0d fe ed */
	ldr	w11, =0xEDFE0DD0	/* same word read little-endian */
	cmp	w10, w11
	b.ne	2f

	/* totalsize is big-endian at offset 4; reject absurd sizes. */
	ldr	w12, [x9, #4]
	rev	w12, w12
	cbz	w12, 2f
	ldr	w13, =DTB_MAX
	cmp	w12, w13
	b.hi	2f

	/* Copy the blob to a stable, reserved page range. */
	ldr	x13, =DTB_STORE
	mov	x14, x12
1:	ldrb	w15, [x9], #1
	strb	w15, [x13], #1
	subs	x14, x14, #1
	b.ne	1b

	/* Publish the stable copy's address. */
	ldr	x16, =DTB_PTR_STORE
	ldr	x17, =DTB_STORE
	str	x17, [x16]
	dsb	sy

2:
	/* Copy the appended EDK2 FD to its link address. */
	adr	x5, _Payload
	ldr	x4, =UEFI_BASE
	ldr	x6, =UEFI_SIZE

	/*
	 * A forward copy is only safe when the source does not overlap the
	 * destination.  LK normally picks a load address below UEFI_BASE, but
	 * do the copy backwards if the source sits inside the target range so
	 * a relocation to overlapping memory cannot corrupt the payload.
	 */
	cmp	x5, x4
	b.lo	3f			/* src < dst: forward */
	add	x7, x4, x6
	cmp	x5, x7
	b.hs	3f			/* src >= dst + size: forward */

	add	x5, x5, x6
	add	x4, x4, x6
4:	ldp	x2, x3, [x5, #-16]!
	stp	x2, x3, [x4, #-16]!
	subs	x6, x6, #16
	b.ne	4b
	b	5f

3:	ldp	x2, x3, [x5], #16
	stp	x2, x3, [x4], #16
	subs	x6, x6, #16
	b.ne	3b
5:

	/* The payload was written with the MMU off; make it visible. */
	dsb	sy
	isb

	ldr	x4, =UEFI_BASE
	mov	x0, #0
	br	x4

_Dead:
	/* Should never be reached. */
	b	_Dead

	/* Keep the literal pool before the payload so BootShim.bin ends exactly
	 * at _Payload: scripts/build_uefi.sh appends the FD there. */
	.ltorg

.text
.align 4

_Payload:
	/* The EDK2 FD is appended here by PlatformBuild.py. */
