/*
 * karnak: files.arm64 builds this for nos_arm_asm, but the source only ships
 * in Apple's closed per-SoC library. It implements the AMCC read-only region
 * (KTRR/CTRR kernel text lockdown), which needs Apple memory controller
 * hardware; nothing references it on KARNAK (no KERNEL_INTEGRITY_KTRR/CTRR).
 */
#include <stdint.h>

static const uint8_t karnak_no_amcc_rorgn __attribute__((unused)) = 0;
