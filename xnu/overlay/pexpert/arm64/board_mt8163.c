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

/*
 * Defined in osfmk/console/serial_console.c.  It is writable before kernel
 * lockdown, which is exactly when the board hook runs.
 */
extern unsigned int cons_ops_index;

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
 * Minimal framebuffer text console.
 *
 * XNU's video_console expects the platform's PE_Video/initialize_screen()
 * handshake and, on this boot path, only ever produced a stray green blob.
 * For bring-up we do not need VT100: writing the ISO 8x16 glyphs straight
 * into the linear framebuffer is enough to watch the kernel boot, and it works
 * before IOKit exists.  karnak_fb_putc() is registered as an extra console op
 * (index 2) so every kprintf lands here once the board hook selects it.
 */
extern unsigned char iso_font[256 * 16];

#define KARNAK_FB_COLS   100U   /* 800 / 8  */
#define KARNAK_FB_ROWS    80U   /* 1280 / 16 */
#define KARNAK_FB_FG     0x00FFFFFFU
#define KARNAK_FB_BG     0x00000000U

static volatile unsigned int *karnak_fb;
static unsigned long karnak_fb_stride_px;
static unsigned int karnak_fb_cols;
static unsigned int karnak_fb_rows;
static unsigned int karnak_fb_cx;
static unsigned int karnak_fb_cy;

static void
karnak_fb_fill(unsigned int value)
{
	unsigned long pixels;

	if (karnak_fb == NULL) {
		return;
	}
	pixels = karnak_fb_stride_px * karnak_fb_rows * 16UL;
	for (unsigned long i = 0; i < pixels; i++) {
		karnak_fb[i] = value;
	}
}

static void
karnak_fb_paint(unsigned int cell_x, unsigned int cell_y, unsigned char ch)
{
	const unsigned char *glyph;
	volatile unsigned int *row;

	if (karnak_fb == NULL || cell_x >= karnak_fb_cols ||
	    cell_y >= karnak_fb_rows) {
		return;
	}
	glyph = &iso_font[(unsigned int)ch * 16U];
	row = karnak_fb + (unsigned long)cell_y * 16UL * karnak_fb_stride_px +
	    (unsigned long)cell_x * 8UL;
	for (unsigned int y = 0; y < 16U; y++) {
		unsigned int bits = glyph[y];

		for (unsigned int x = 0; x < 8U; x++) {
			row[x] = (bits & (0x80U >> x)) ? KARNAK_FB_FG : KARNAK_FB_BG;
		}
		row += karnak_fb_stride_px;
	}
}

void
karnak_fb_console_init(unsigned long long base_addr,
    unsigned long long row_bytes, unsigned long long width,
    unsigned long long height, unsigned long long depth)
{
	unsigned long long size;

	if ((base_addr == 0) || (row_bytes == 0) || (width < 8) ||
	    (height < 16) || (depth != 32)) {
		karnak_early_puts("KARNAK: no usable framebuffer\n");
		return;
	}

	size = row_bytes * height;
	karnak_fb = (volatile unsigned int *)ml_io_map(
	    (vm_offset_t)(base_addr & ~3ULL), (vm_size_t)size);
	if (karnak_fb == NULL) {
		karnak_early_puts("KARNAK: framebuffer map failed\n");
		return;
	}

	karnak_fb_stride_px = (unsigned long)(row_bytes / 4ULL);
	karnak_fb_cols = (width >= KARNAK_FB_COLS * 8ULL) ?
	    KARNAK_FB_COLS : (unsigned int)(width / 8ULL);
	karnak_fb_rows = (height >= KARNAK_FB_ROWS * 16ULL) ?
	    KARNAK_FB_ROWS : (unsigned int)(height / 16ULL);
	karnak_fb_cx = 0;
	karnak_fb_cy = 0;
	karnak_fb_fill(KARNAK_FB_BG);
}

int
karnak_fb_is_ready(void)
{
	return karnak_fb != NULL;
}

void
karnak_fb_putc(char c, int poll)
{
	(void)poll;

	if (karnak_fb == NULL) {
		return;
	}

	switch (c) {
	case '\r':
		karnak_fb_cx = 0;
		return;
	case '\n':
		karnak_fb_cx = 0;
		karnak_fb_cy++;
		break;
	case '\b':
		if (karnak_fb_cx > 0) {
			karnak_fb_cx--;
		}
		karnak_fb_paint(karnak_fb_cx, karnak_fb_cy, ' ');
		return;
	case '\t':
		do {
			karnak_fb_paint(karnak_fb_cx, karnak_fb_cy, ' ');
			karnak_fb_cx++;
			if (karnak_fb_cx >= karnak_fb_cols) {
				karnak_fb_cx = 0;
				karnak_fb_cy++;
			}
		} while ((karnak_fb_cx & 7U) != 0);
		break;
	default:
		karnak_fb_paint(karnak_fb_cx, karnak_fb_cy, (unsigned char)c);
		karnak_fb_cx++;
		if (karnak_fb_cx >= karnak_fb_cols) {
			karnak_fb_cx = 0;
			karnak_fb_cy++;
		}
		break;
	}

	if (karnak_fb_cy >= karnak_fb_rows) {
		/* Start over rather than scrolling a device mapping. */
		karnak_fb_cy = 0;
		karnak_fb_cx = 0;
		karnak_fb_fill(KARNAK_FB_BG);
	}
}

int
karnak_fb_getc(int wait)
{
	(void)wait;
	return 0;
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

	karnak_fb_console_init((unsigned long long)(args->Video.v_baseAddr & ~3ULL),
	    args->Video.v_rowBytes, args->Video.v_width, args->Video.v_height,
	    args->Video.v_depth & kBootVideoDepthMask);
	if (karnak_fb_is_ready()) {
		/*
		 * cons_ops[] gains a third entry for karnak (see
		 * osfmk/console/serial_console.c); select it so kprintf and
		 * panic output are drawn on the panel.
		 */
		cons_ops_index = 2;
		kprintf("KARNAK: fb console %lux%lu depth %lu stride 0x%lx\n",
		    (unsigned long)args->Video.v_width,
		    (unsigned long)args->Video.v_height,
		    (unsigned long)(args->Video.v_depth & kBootVideoDepthMask),
		    (unsigned long)args->Video.v_rowBytes);
	} else {
		karnak_early_puts("KARNAK: framebuffer console unavailable\n");
	}

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
