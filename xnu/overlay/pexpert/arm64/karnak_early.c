/*
 * karnak early console implementation.  See karnak_early.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pexpert/arm64/KARNAK.h>
#include <pexpert/arm64/karnak_early.h>

#ifndef KARNAK_EARLY_TX_SPIN
#define KARNAK_EARLY_TX_SPIN   1000000U
#endif

/*
 * The base is a plain unsigned long so the file compiles in both the kernel
 * (vm_offset_t is 64-bit) and the host test without pulling in XNU headers.
 */
static unsigned long karnak_uart_base = (unsigned long)KARNAK_UART0_PA;

#if defined(KARNAK_EARLY_HOST_TEST)
/*
 * Host test hook: the test owns the register file.  Signed/unsigned widths
 * match the 32-bit MMIO the kernel uses, so the test exercises the same paths.
 */
extern unsigned int karnak_early_test_read(unsigned int offset);
extern void karnak_early_test_write(unsigned int offset, unsigned int value);
#define KARNAK_UART_READ(off)      karnak_early_test_read (off)
#define KARNAK_UART_WRITE(off, v)  karnak_early_test_write ((off), (v))
#else
#define KARNAK_UART_READ(off) \
	(*(volatile unsigned int *)(karnak_uart_base + (unsigned long)(off)))
#define KARNAK_UART_WRITE(off, v) \
	(*(volatile unsigned int *)(karnak_uart_base + (unsigned long)(off)) = (v))
#endif

void
karnak_early_set_mmio_base(unsigned long base)
{
	karnak_uart_base = (base != 0) ? base : (unsigned long)KARNAK_UART0_PA;
}

unsigned int
karnak_early_tx_ready(void)
{
	return (KARNAK_UART_READ(KARNAK_UART_LSR) & KARNAK_UART_LSR_THRE) != 0;
}

unsigned int
karnak_early_rx_ready(void)
{
	return (KARNAK_UART_READ(KARNAK_UART_LSR) & KARNAK_UART_LSR_DR) != 0;
}

int
karnak_early_getc(void)
{
	if (!karnak_early_rx_ready()) {
		return -1;
	}
	return (int)(KARNAK_UART_READ(KARNAK_UART_RBR) & 0xFFU);
}

static void
karnak_early_raw(unsigned int c)
{
	unsigned int spin = KARNAK_EARLY_TX_SPIN;

	while (!karnak_early_tx_ready()) {
		if (--spin == 0) {
			return;
		}
	}
	KARNAK_UART_WRITE(KARNAK_UART_THR, c & 0xFFU);
}

void
karnak_early_putc(unsigned int c)
{
	if (c == '\n') {
		karnak_early_raw('\r');
	}
	karnak_early_raw(c);
}

void
karnak_early_puts(const char *s)
{
	if (s == (const char *)0) {
		return;
	}
	while (*s != '\0') {
		karnak_early_putc((unsigned char)*s++);
	}
}

void
karnak_early_hex(unsigned long long value)
{
	static const char digits[] = "0123456789abcdef";
	char buffer[16];
	int i = 0;

	do {
		buffer[i++] = digits[value & 0xFU];
		value >>= 4;
	} while (value != 0);

	while (i > 0) {
		karnak_early_putc((unsigned char)buffer[--i]);
	}
}

void
karnak_early_udec(unsigned long long value)
{
	char buffer[20];
	int i = 0;

	do {
		buffer[i++] = (char)('0' + (value % 10));
		value /= 10;
	} while (value != 0);

	while (i > 0) {
		karnak_early_putc((unsigned char)buffer[--i]);
	}
}

void
karnak_early_mark(const char *tag, unsigned long long value)
{
	karnak_early_puts("KARNAK: ");
	karnak_early_puts(tag);
	karnak_early_puts("=0x");
	karnak_early_hex(value);
	karnak_early_putc('\n');
}

void
karnak_early_range(const char *tag, unsigned long long base,
    unsigned long long size)
{
	karnak_early_puts("KARNAK: ");
	karnak_early_puts(tag);
	karnak_early_puts(" 0x");
	karnak_early_hex(base);
	karnak_early_puts(" + 0x");
	karnak_early_hex(size);
	karnak_early_putc('\n');
}
