/*
 * karnak (MediaTek MT8163) board hooks.
 *
 * The only job of this file today is to bring up a console and print the
 * facts that the first hardware boot has to confirm: the DTB XnuLoader handed
 * over, the memory size XNU was told about, and the framebuffer geometry.
 * Everything it prints goes through the polled UART driver in karnak_early.c.
 *
 * Hardware interrupt setup, the GICv2 driver, the timer and MSDC0 are separate
 * pieces of work (see xnu/README.md); this file must stay small enough to be
 * reviewed on its own.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pexpert/pexpert.h>
#include <pexpert/arm64/boot.h>
#include <pexpert/arm64/board_config.h>

#if defined(ARM64_BOARD_CONFIG_KARNAK)

#include <machine/machine_routines.h>

#include <pexpert/arm64/karnak_early.h>

/* FDT header magic, big-endian on the wire. */
#define KARNAK_FDT_MAGIC 0xD00DFEEDU

static unsigned int
karnak_be32(const void *address)
{
	const unsigned char *bytes = (const unsigned char *)address;

	return ((unsigned int)bytes[0] << 24) |
	       ((unsigned int)bytes[1] << 16) |
	       ((unsigned int)bytes[2] << 8) |
	       (unsigned int)bytes[3];
}

/*
 * Called from PE_init_platform(TRUE) once the VM is up, i.e. after
 * ml_io_map() is legal.  Before this point the early console can only be used
 * from code that still runs on the identity mapping.
 */
/*
 * Bring up the in-kernel framebuffer text console.  boot_args carries the
 * physical address LK programmed into the display controller; setting bit 0
 * tells initialize_screen() to map it instead of treating it as a virtual
 * address.  From here on kprintf output is visible on the panel, which is the
 * only console available until the UART pads are wired.
 */
static void
karnak_fb_console_init(boot_args *args)
{
	PE_Video video;

	if ((args->Video.v_baseAddr == 0) || (args->Video.v_rowBytes == 0) ||
	    (args->Video.v_height == 0))
	{
		karnak_early_puts("KARNAK: no boot_args framebuffer\n");
		return;
	}

	/*
	 * boot_args carries a Boot_Video, which has a different layout from
	 * PE_Video; copy the fields instead of assigning the structs.
	 */
	video = PE_state.video;
	video.v_baseAddr = (args->Video.v_baseAddr & ~3UL) | 1UL;   /* physical */
	video.v_rowBytes = args->Video.v_rowBytes;
	video.v_width    = args->Video.v_width;
	video.v_height   = args->Video.v_height;
	video.v_depth    = args->Video.v_depth;
	video.v_offset   = 0;
	video.v_length   = 0;
	/* 2x scaling makes the 8x16 font readable in a photo of the panel. */
	video.v_scale    = kPEScaleFactor2x;
	PE_state.video   = video;

	(void)PE_initialize_console(&PE_state.video, kPEGraphicsMode);
	(void)PE_initialize_console(&PE_state.video, kPEAcquireScreen);

	kprintf("KARNAK: fb console %lux%lu, %lu bpp, stride 0x%lx, pa 0x%lx\n",
	    (unsigned long)video.v_width, (unsigned long)video.v_height,
	    (unsigned long)video.v_depth, (unsigned long)video.v_rowBytes,
	    (unsigned long)(video.v_baseAddr & ~3UL));
}

void
karnak_board_early_init(boot_args *args)
{
	vm_offset_t uart;

	karnak_early_puts("KARNAK: XNU arm64 start (MT8163, Cortex-A53 x4)\n");

	uart = ml_io_map((vm_offset_t)KARNAK_UART0_PA, 0x1000);
	if (uart != 0) {
		karnak_early_set_mmio_base((unsigned long)uart);
	} else {
		karnak_early_puts("KARNAK: UART0 map failed; output may be lost\n");
	}

	karnak_fb_console_init(args);

	karnak_early_mark("dtb", (unsigned long long)(unsigned long)args->deviceTreeP);
	karnak_early_mark("dtb_len", args->deviceTreeLength);
	karnak_early_mark("mem_size", args->memSize);
	karnak_early_mark("mem_size_actual", args->memSizeActual);
	karnak_early_mark("kernel_data_top", args->topOfKernelData);

	if (args->deviceTreeP != NULL) {
		unsigned int magic = karnak_be32(args->deviceTreeP);

		if (magic != KARNAK_FDT_MAGIC) {
			karnak_early_mark("dtb_bad_magic", magic);
		}
	}

	karnak_early_mark("fb", (unsigned long long)args->Video.v_baseAddr);
	karnak_early_mark("fb_width", args->Video.v_width);
	karnak_early_mark("fb_height", args->Video.v_height);
	karnak_early_mark("fb_rowbytes", args->Video.v_rowBytes);

	karnak_early_puts("KARNAK: board early init done\n");
}

#else /* !ARM64_BOARD_CONFIG_KARNAK */

/*
 * Keep the translation unit non-empty for other boards so the same file list
 * can be used everywhere; the compiler warns about empty TUs.
 */
typedef int karnak_board_mt8163_unused;

#endif /* ARM64_BOARD_CONFIG_KARNAK */
