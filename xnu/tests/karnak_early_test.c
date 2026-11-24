/*
 * Host test for the karnak early UART console.
 *
 * Simulates the 16550 register file instead of touching MMIO, so it runs
 * anywhere.  Build:
 *
 *   cc -std=c11 -Wall -Wextra -Werror -DKARNAK_EARLY_HOST_TEST \
 *      -I xnu/overlay xnu/tests/karnak_early_test.c \
 *      xnu/overlay/pexpert/arm64/karnak_early.c -o karnak_early_test
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include <pexpert/arm64/karnak_early.h>

#define REG_THR  0x00
#define REG_RBR  0x00
#define REG_LSR  0x14
#define THRE     0x20

static char output[4096];
static size_t output_len;
static int transmitter_ready = 1;
static unsigned int lsr_reads;
static unsigned int thr_writes;

static unsigned int rx_byte;
static int receiver_ready;

unsigned int
karnak_early_test_read(unsigned int offset)
{
	if (offset == REG_LSR) {
		lsr_reads++;
		return (transmitter_ready ? THRE : 0) | (receiver_ready ? 1 : 0);
	}
	if (offset == REG_RBR) {
		receiver_ready = 0;
		return rx_byte;
	}
	return 0;
}

void
karnak_early_test_write(unsigned int offset, unsigned int value)
{
	if (offset == REG_THR) {
		thr_writes++;
		if (output_len + 1 < sizeof(output)) {
			output[output_len++] = (char)value;
			output[output_len] = '\0';
		}
	}
}

static void
reset(void)
{
	output_len = 0;
	output[0] = '\0';
	transmitter_ready = 1;
	receiver_ready = 0;
	lsr_reads = 0;
	thr_writes = 0;
}

static int
expect(const char *what, const char *want)
{
	if (strcmp(output, want) == 0) {
		return 0;
	}
	fprintf(stderr, "FAIL: %s\n  want %s\n  got  %s\n", what, want, output);
	return 1;
}

int
main(void)
{
	int failures = 0;

	/* Plain text, including newline expansion. */
	reset();
	karnak_early_puts("hello\n");
	failures += expect("puts", "hello\r\n");
	if (lsr_reads != thr_writes) {
		fprintf(stderr, "FAIL: %u LSR reads for %u characters\n",
		    lsr_reads, thr_writes);
		failures++;
	}

	/* Hex: zero, a single digit, and a full-width value. */
	reset();
	karnak_early_hex(0);
	karnak_early_hex(0xa);
	karnak_early_hex(0xdeadbeefULL);
	failures += expect("hex", "0adeadbeef");

	/* Decimal, including the value that overflows 32 bits. */
	reset();
	karnak_early_udec(0);
	karnak_early_putc(':');
	karnak_early_udec(4294967296ULL);
	failures += expect("udec", "0:4294967296");

	/* Checkpoint helper format. */
	reset();
	karnak_early_mark("dtb", 0x4fe00000ULL);
	failures += expect("mark", "KARNAK: dtb=0x4fe00000\r\n");

	reset();
	karnak_early_range("dram", 0x40000000ULL, 0x20000000ULL);
	failures += expect("range", "KARNAK: dram 0x40000000 + 0x20000000\r\n");

	/* A stuck transmitter must drop the character, not spin forever. */
	reset();
	transmitter_ready = 0;
	karnak_early_puts("x");
	if (thr_writes != 0 || output_len != 0) {
		fprintf(stderr, "FAIL: wrote to a stuck transmitter\n");
		failures++;
	}
	if (lsr_reads == 0) {
		fprintf(stderr, "FAIL: never polled LSR\n");
		failures++;
	}

	/* Receive path, used by the pe_serial wrapper. */
	reset();
	rx_byte = 0x5A;
	receiver_ready = 1;
	if (!karnak_early_rx_ready()) {
		fprintf(stderr, "FAIL: rx not reported ready\n");
		failures++;
	}
	if (karnak_early_getc() != 0x5A) {
		fprintf(stderr, "FAIL: wrong byte received\n");
		failures++;
	}
	if (karnak_early_getc() != -1) {
		fprintf(stderr, "FAIL: read a byte when none was waiting\n");
		failures++;
	}

	/* Switching the base must not change behaviour (host ignores it). */
	reset();
	karnak_early_set_mmio_base(0xdeadb000UL);
	karnak_early_putc('A');
	karnak_early_set_mmio_base(0);
	failures += expect("mmio base", "A");

	if (failures != 0) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("karnak early console: ok\n");
	return 0;
}
