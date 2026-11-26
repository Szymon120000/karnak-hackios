/*
 * karnak early console: polled output on UART0 (MediaTek MT8163).
 *
 * This is the first thing that can print once XNU's arm64 startup is far
 * enough along to branch into C.  It intentionally does not program the
 * controller: LK has already muxed the pins and set 921600 8N1 from the
 * 26 MHz clock (see pexpert/arm64/board_mt8163.c and the EDK2
 * KarnakSerialPortLib, which takes the same approach).  Re-deriving the
 * MTK high-speed divisor here would only risk breaking LK's setup.
 *
 * Before the MMU is enabled, the driver uses KARNAK_UART0_PA directly.  Once
 * board_mt8163.c has mapped the controller, karnak_early_set_mmio_base()
 * switches every access to the virtual mapping.
 *
 * The header is standalone and the implementation is host-testable:
 * xnu/tests/karnak_early_test.c compiles it with KARNAK_EARLY_HOST_TEST,
 * which routes every register access to a simulated 16550.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _PEXPERT_ARM64_KARNAK_EARLY_H
#define _PEXPERT_ARM64_KARNAK_EARLY_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Point the driver at a new mapping (the physical address by default).
 * Pass 0 to go back to KARNAK_UART0_PA.
 */
void karnak_early_set_mmio_base(unsigned long base);

/* Non-zero when the transmitter can accept a character / a byte is waiting. */
unsigned int karnak_early_tx_ready(void);
unsigned int karnak_early_rx_ready(void);

/* Read one byte; only call when karnak_early_rx_ready() is non-zero. */
int karnak_early_getc(void);

/*
 * Write one character.  '\n' is expanded to "\r\n".  If the transmitter never
 * reports THRE within KARNAK_EARLY_TX_SPIN polls, the character is dropped:
 * bring-up must not hang in a console write.
 */
void karnak_early_putc(unsigned int c);

void karnak_early_puts(const char *s);

/* Hexadecimal without a "0x" prefix: "0" for zero, otherwise no leading zeros. */
void karnak_early_hex(unsigned long long value);

/* Unsigned decimal. */
void karnak_early_udec(unsigned long long value);

/* "KARNAK: <tag>=0x<hex>\r\n" -- one grep-able line per checkpoint. */
void karnak_early_mark(const char *tag, unsigned long long value);

/* "KARNAK: <tag> 0x<base> + 0x<size>\r\n" for memory ranges. */
void karnak_early_range(const char *tag, unsigned long long base,
    unsigned long long size);

/*
 * Minimal framebuffer text console.
 *
 * XNU's own video console needs a GOP/PE_Video handshake that does not work
 * reliably on this boot path, so karnak draws ISO 8x16 glyphs straight into
 * the linear framebuffer instead.  Once karnak_fb_console_init() succeeds the
 * board hook selects it as console ops index 2, which means ordinary kprintf
 * output (including panics) lands on the panel.
 *
 * All of these are no-ops until karnak_fb_console_init() has been called.
 * karnak_fb_getc() always returns 0; the panel is output-only.
 */
void karnak_fb_console_init(unsigned long long base_addr,
    unsigned long long row_bytes, unsigned long long width,
    unsigned long long height, unsigned long long depth);
int karnak_fb_is_ready(void);
void karnak_fb_putc(char c, int poll);
int karnak_fb_getc(int wait);

#ifdef __cplusplus
}
#endif

#endif /* _PEXPERT_ARM64_KARNAK_EARLY_H */
