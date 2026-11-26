/*
 * karnak: Apple's per-SoC CPU tunables (implementation-defined HID/ACC
 * register settings applied in start.s) are not in the open source drop,
 * and none of them exist on Cortex-A53. APPLY_TUNABLES does nothing.
 */
.macro APPLY_TUNABLES midr, tmp1, tmp2
.endm
