# saturn/Makefile - Saturn build: IP.BIN, start-up code and disc images.
#
#   make            build build/ip.bin and the start-up code
#   make clean
#
# Needs the toolchain from toolchain/build-toolchain.sh and the SDK in ../SSSDK.

TOOLCHAIN ?= $(CURDIR)/toolchain/install
PREFIX    := $(TOOLCHAIN)/bin/sh-elf-
CC        := $(PREFIX)gcc
OBJCOPY   := $(PREFIX)objcopy

SEGALIB   := ../SSSDK/SaturnBasicLibrary6/SEGALIB/LIB
BUILD     := build

CFLAGS    := -m2 -O2 -Wall -Wextra -ffreestanding -fno-builtin -Icommon
LDFLAGS   := -m2 -nostdlib -nostartfiles -Wl,--no-warn-rwx-segments

SEGA_IP_OBJS := sys_sec sys_arej sys_aret sys_areu sys_aree sys_init

.PHONY: all clean
all: $(BUILD)/ip.bin $(BUILD)/common/crt0.o

# --- IP.BIN ---------------------------------------------------------------

# Sega's IP objects are Hitachi SH COFF; convert them to ELF for our linker.
define SEGA_IP_RULE
$(BUILD)/ip/$(1).o: $(SEGALIB)/$(shell echo $(1) | tr a-z A-Z).O | $(BUILD)/ip
	$$(OBJCOPY) -I coff-sh -O elf32-sh $$< $$@
endef
$(foreach o,$(SEGA_IP_OBJS),$(eval $(call SEGA_IP_RULE,$(o))))

$(BUILD)/ip/%.o: ip/%.S | $(BUILD)/ip
	$(CC) $(CFLAGS) -c $< -o $@

IP_OBJS := $(BUILD)/ip/sys_id.o $(SEGA_IP_OBJS:%=$(BUILD)/ip/%.o) $(BUILD)/ip/app_init.o

$(BUILD)/ip.elf: $(IP_OBJS) ip/ip.ld
	$(CC) $(LDFLAGS) -T ip/ip.ld $(IP_OBJS) -o $@

$(BUILD)/ip.bin: $(BUILD)/ip.elf
	$(OBJCOPY) -O binary $< $@

# --- start-up code ----------------------------------------------------------

$(BUILD)/common/crt0.o: common/crt0.S | $(BUILD)/common
	$(CC) $(CFLAGS) -c $< -o $@

# --- disc images -------------------------------------------------------------

$(BUILD)/%/disc/0.BIN: $(BUILD)/%.elf
	@mkdir -p $(@D)
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.iso: $(BUILD)/ip.bin $(BUILD)/%/disc/0.BIN tools/mkdisc.sh
	tools/mkdisc.sh $(BUILD)/ip.bin $(BUILD)/$*/disc $(BUILD)/$*

$(BUILD)/ip $(BUILD)/common:
	mkdir -p $@

clean:
	rm -rf $(BUILD)
