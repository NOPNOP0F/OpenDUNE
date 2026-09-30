# saturn/Makefile - Saturn build: IP.BIN, runtime, programs and disc images.
#
#   make            build build/ip.bin and the runtime
#   make clean
#
# Needs the toolchain from toolchain/build-toolchain.sh and the SDK in ../SSSDK.

TOOLCHAIN ?= $(CURDIR)/toolchain/install
PREFIX    := $(TOOLCHAIN)/bin/sh-elf-
CC        := $(PREFIX)gcc
OBJCOPY   := $(PREFIX)objcopy

SEGALIB   := ../SSSDK/SaturnBasicLibrary6/SEGALIB/LIB
BUILD     := build

CFLAGS    := -m2 -O2 -std=gnu11 -Wall -Wextra -Icommon
LDFLAGS   := -m2 -nostartfiles -Wl,--no-warn-rwx-segments -T common/saturn.ld
LIBS      := -lc -lgcc

SEGA_IP_OBJS := sys_sec sys_arej sys_aret sys_areu sys_aree sys_init

.PHONY: all build-all clean
all: build-all

# --- IP.BIN ---------------------------------------------------------------

# Sega's IP objects are Hitachi SH COFF; convert them to ELF for our linker.
define SEGA_IP_RULE
$(BUILD)/ip/$(1).o: $(SEGALIB)/$(shell echo $(1) | tr a-z A-Z).O
	@mkdir -p $$(@D)
	$$(OBJCOPY) -I coff-sh -O elf32-sh $$< $$@
endef
$(foreach o,$(SEGA_IP_OBJS),$(eval $(call SEGA_IP_RULE,$(o))))

$(BUILD)/ip/%.o: ip/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

IP_OBJS := $(BUILD)/ip/sys_id.o $(SEGA_IP_OBJS:%=$(BUILD)/ip/%.o) $(BUILD)/ip/app_init.o

$(BUILD)/ip.elf: $(IP_OBJS) ip/ip.ld
	$(CC) -m2 -nostdlib -Wl,--no-warn-rwx-segments -T ip/ip.ld $(IP_OBJS) -o $@

$(BUILD)/ip.bin: $(BUILD)/ip.elf
	$(OBJCOPY) -O binary $< $@

# --- runtime shared by every program ---------------------------------------

COMMON_HDRS := $(wildcard common/*.h)
RUNTIME_OBJS := $(patsubst common/%,$(BUILD)/common/%.o,$(basename $(wildcard common/*.c common/*.S)))

$(BUILD)/common/%.o: common/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/common/%.o: common/%.c $(COMMON_HDRS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# --- disc images -------------------------------------------------------------

$(BUILD)/%/disc/0.BIN: $(BUILD)/%.elf
	@mkdir -p $(@D)
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.iso: $(BUILD)/ip.bin $(BUILD)/%/disc/0.BIN tools/mkdisc.sh
	tools/mkdisc.sh $(BUILD)/ip.bin $(BUILD)/$*/disc $(BUILD)/$*

build-all: $(BUILD)/ip.bin $(RUNTIME_OBJS)

clean:
	rm -rf $(BUILD)
