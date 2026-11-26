/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Synthetic karnak "kernel" for the loader end-to-end test.
 *
 * XnuLoader parses this image as an ARM64 Mach-O, copies its __TEXT segment
 * into allocator-provided memory, builds boot_args, leaves boot services and
 * branches here with the MMU and caches off and x0 = boot_args.
 *
 * It uses no stack and paints the panel directly, so it can prove the whole
 * file -> parse -> copy -> handoff path without a real XNU:
 *
 *   rows   0..199  white   the loaded kernel actually ran
 *   rows 200..299  cyan    this is SyntheticKernel, not the probe payload
 *   rows 300..end  green   boot_args revision/version are valid
 *                  red     boot_args are wrong
 *
 * The image is relocated by the loader, so every address below is formed
 * PC-relative (literal pool loads) and no absolute link address is assumed.
 */

#include <karnak/hardware_map.h>

	.text

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

	.global	SyntheticKernelEntry
	.type	SyntheticKernelEntry, %function
SyntheticKernelEntry:
	/* x0 = boot_args; keep it while the fill helpers clobber x1-x18. */
	mov	x19, x0

	ldr	x10, =KARN_FB_BASE_FALLBACK	/* framebuffer base */
	mov	x11, #KARN_FB_STRIDE		/* row bytes */
	mov	x12, #KARN_FB_WIDTH		/* pixels per row */

	/* White: "the synthetic kernel reached its entry point". */
	ldr	w9, =0xFFFFFFFF
	KARNAK_FILL_ROWS 0, 200

	/* Cyan: identify the loaded kernel image.  The panel scans bytes
	 * R,G,B,A, so a little-endian word is 0xAABBGGRR: cyan is 0xFFFFFF00. */
	ldr	w9, =0xFFFFFF00
	KARNAK_FILL_ROWS 200, 100

	/* Green for valid boot_args, red otherwise. */
	ldr	w1, [x19]			/* Revision | Version << 16 */
	mov	w2, #0x00020002
	cmp	w1, w2
	b.ne	.Lbad_args
	ldr	w9, =0xFF00FF00			/* green: valid boot_args */
	b	.Lbody

.Lbad_args:
	ldr	w9, =0xFFFF0000			/* red: boot_args wrong */

.Lbody:
	KARNAK_FILL_ROWS 300, (KARN_FB_HEIGHT - 300)

.Lhang:
	wfi
	b	.Lhang

	/* Keep the literals inside the image the loader copies. */
	.ltorg
