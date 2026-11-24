# KarnakBootShim build.
# SPDX-License-Identifier: BSD-2-Clause-Patent

TARGET        ?= aarch64-linux-gnu
CROSS_COMPILE ?= $(TARGET)-
CC            := $(CROSS_COMPILE)gcc
OBJCOPY       := $(CROSS_COMPILE)objcopy

# Must match include/karnak/hardware_map.h.
UEFI_BASE     ?= 0x50000000
UEFI_SIZE     ?= 0x00700000
DTB_STORE     ?= 0x4FE00000
DTB_PTR_STORE ?= 0x4FF00000
DTB_MAX       ?= 0x00100000

DEFINES := -DUEFI_BASE=$(UEFI_BASE) -DUEFI_SIZE=$(UEFI_SIZE) \
           -DDTB_STORE=$(DTB_STORE) -DDTB_PTR_STORE=$(DTB_PTR_STORE) \
           -DDTB_MAX=$(DTB_MAX)

all: BootShim.bin

BootShim.elf: BootShim.S
	$(CC) -c $< -o $@ $(DEFINES) -Wa,--noexecstack

BootShim.bin: BootShim.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f BootShim.elf BootShim.bin

.PHONY: all clean
