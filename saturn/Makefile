# saturn/Makefile - Saturn build: IP.BIN, runtime, programs and disc images.
#
#   make            build build/opendune.cue
#   make opendune   only the engine disc
#   make clean
#
# Needs the toolchain from toolchain/build-toolchain.sh and the SDK in ../SSSDK.

TOOLCHAIN ?= $(CURDIR)/toolchain/install
PREFIX    := $(TOOLCHAIN)/bin/sh-elf-
CC        := $(PREFIX)gcc
CXX       := $(PREFIX)g++
OBJCOPY   := $(PREFIX)objcopy
AR        := $(PREFIX)ar

SEGALIB   := ../SSSDK/SaturnBasicLibrary6/SEGALIB/LIB
SRC       := ../src
BUILD     := build

# Dune II v1.07 data files; only the PAK archives go on the disc
DATA      ?= ../bin/data

# General MIDI SoundFont the music instruments are taken from (INSTR.BNK);
# without it the disc has no music
SOUNDFONT ?= /usr/share/soundfonts/FluidR3_GM.sf2

CFLAGS    := -m2 -O2 -std=gnu11 -Wall -Wextra -Icommon
# C++ only for the AdLib driver: no exceptions, RTTI or C++ library
CXXFLAGS  := -m2 -O2 -std=gnu++11 -Wall -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics
LDFLAGS   := -m2 -nostartfiles -Wl,--no-warn-rwx-segments -T common/saturn.ld
SBL_LIBS  := $(BUILD)/libsbl.a $(BUILD)/libcdc.a
LIBS      := $(SBL_LIBS) -lc -lgcc

SEGA_IP_OBJS := sys_sec sys_arej sys_aret sys_areu sys_aree sys_init

.PHONY: all opendune clean
all: opendune
opendune: $(BUILD)/opendune.iso

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

# --- SBL: file system (GFS) and what it needs ------------------------------

# GFS, DMA and cache control are built from the SDK's C sources. The files
# are copied first: lower-case names (the sources include them that way),
# no CR line ends and no DOS end-of-file marker.
SBL_SRC     := ../SSSDK/SaturnBasicLibrary6/SEGALIB
SBL_MODULES := GFS DMA CSH
SBL_GFS     := gfs gfs_buf gfs_cdb gfs_cdc gfs_cdf gfs_dir gfs_trn
SBL_DMA     := dma_cpu0 dma_cpu1 dma_cpu2 dma_cpu3 dma_cpu4 dma_cpu5 dma_cpum dma_scu0 dma_scu1
SBL_OBJS    := $(SBL_GFS:%=$(BUILD)/sbl/gfs/%.o) $(SBL_DMA:%=$(BUILD)/sbl/dma/%.o) \
               $(BUILD)/sbl/csh/csh_main.o $(BUILD)/sbl/int_bios.o
SBL_CFLAGS  := -m2 -O2 -std=gnu89 -w -D_SH -DMODEL_S -I$(BUILD)/sbl/inc

$(BUILD)/sbl/.copied:
	@mkdir -p $(BUILD)/sbl/inc
	for f in $(SBL_SRC)/INCLUDE/*.H; do \
		tr -d '\r\032' < $$f > $(BUILD)/sbl/inc/$$(basename $$f | tr A-Z a-z); done
	for m in $(SBL_MODULES); do \
		d=$(BUILD)/sbl/$$(echo $$m | tr A-Z a-z); mkdir -p $$d; \
		for f in $(SBL_SRC)/$$m/*.C $(SBL_SRC)/$$m/*.H; do \
			if [ -f $$f ]; then tr -d '\r\032' < $$f > $$d/$$(basename $$f | tr A-Z a-z); fi; done; done
	touch $@

$(BUILD)/sbl/%.o: $(BUILD)/sbl/.copied
	$(CC) $(SBL_CFLAGS) -I$(@D) -c $(@:.o=.c) -o $@

$(BUILD)/sbl/int_bios.o: sbl/int_bios.c $(BUILD)/sbl/.copied
	$(CC) $(SBL_CFLAGS) -c $< -o $@

$(BUILD)/libsbl.a: $(SBL_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

# The CD block driver (CDC) only exists as Hitachi SH COFF objects.
$(BUILD)/libcdc.a: $(SBL_SRC)/LIB/SEGA_CDC.A
	rm -rf $(BUILD)/cdc && mkdir -p $(BUILD)/cdc
	cd $(BUILD)/cdc && $(AR) x $(abspath $<)
	for f in $(BUILD)/cdc/*.o; do $(OBJCOPY) -I coff-sh -O elf32-sh $$f $$f; done
	rm -f $@
	$(AR) rcs $@ $(BUILD)/cdc/*.o

# --- runtime shared by every program ---------------------------------------
# (objects also depend on this Makefile, so changed flags rebuild them)

COMMON_HDRS := $(wildcard common/*.h)
RUNTIME_OBJS := $(patsubst common/%,$(BUILD)/common/%.o,$(basename $(wildcard common/*.c common/*.S)))

$(BUILD)/common/%.o: common/%.S Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/common/%.o: common/%.c $(COMMON_HDRS) Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# the file layer talks to GFS
$(BUILD)/common/files.o: common/files.c $(COMMON_HDRS) $(BUILD)/sbl/.copied Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -D_SH -DMODEL_S -I$(BUILD)/sbl/inc -c $< -o $@

# --- the AdLib driver (C++) ------------------------------------------------

$(BUILD)/cxx/adl_driver.o: $(SRC)/audio/adl_driver.cpp $(SRC)/audio/adl_driver.h Makefile
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- opendune: the engine ---------------------------------------------------

ENGINE_SRCS := \
	animation.c audio/driver.c audio/dsp_saturn.c audio/midi_saturn.c audio/mt32mpu.c \
	audio/sound.c codec/format40.c codec/format80.c config.c crashlog/crashlog.c \
	crashlog/crashlog_none.c cutscene.c explosion.c file.c gfx.c gui/editbox.c \
	gui/font.c gui/gui.c gui/mentat.c gui/security.c gui/viewport.c gui/widget.c \
	gui/widget_click.c gui/widget_draw.c house.c ini.c inifile.c input/input.c \
	input/mouse.c input/pad_saturn.c load.c map.c object.c opendune.c os/endian.c os/error.c \
	os/readdir_saturn.c pool/house.c pool/structure.c pool/team.c pool/unit.c \
	save.c saveload/house.c saveload/info.c saveload/map.c saveload/object.c \
	saveload/saveload.c saveload/scenario.c saveload/scriptengine.c \
	saveload/structure.c saveload/team.c saveload/unit.c scenario.c \
	script/general.c script/script.c script/structure.c script/team.c \
	script/unit.c sprites.c string.c structure.c table/actioninfo.c \
	table/animation.c table/explosion.c table/houseanimation.c table/houseinfo.c \
	table/landscapeinfo.c table/movementtype.c table/selectiontype.c table/sound.c \
	table/structureinfo.c table/teamaction.c table/tilediff.c table/unitinfo.c \
	table/widget.c table/widgetinfo.c table/windowdesc.c team.c tile.c timer.c \
	tools.c unit.c video/video_fps.c video/video_saturn.c wsa.c

ENGINE_OBJS := $(ENGINE_SRCS:%.c=$(BUILD)/engine/%.o) $(BUILD)/engine/rev.o
ENGINE_CFLAGS := $(CFLAGS) -DSATURN -DDUNE_DATA_DIR='"CD"' -I../include

$(BUILD)/engine/%.o: $(SRC)/%.c Makefile
	@mkdir -p $(@D)
	$(CC) $(ENGINE_CFLAGS) -MMD -MP -c $< -o $@

-include $(ENGINE_OBJS:.o=.d)

# rev.c is normally generated by configure
$(BUILD)/engine/rev.c: $(SRC)/rev.c.in
	@mkdir -p $(@D)
	sed -e "s/!!VERSION!!/$$(git describe --tags --always 2>/dev/null || echo unknown)-saturn/" \
	    -e "s/!!MODIFIED!!/$$(git diff --quiet HEAD 2>/dev/null && echo 0 || echo 1)/" $< > $@

$(BUILD)/engine/rev.o: $(BUILD)/engine/rev.c
	$(CC) $(ENGINE_CFLAGS) -I$(SRC) -c $< -o $@

$(BUILD)/opendune.elf: $(RUNTIME_OBJS) $(ENGINE_OBJS) $(SBL_LIBS) common/saturn.ld
	$(CC) $(LDFLAGS) $(RUNTIME_OBJS) $(ENGINE_OBJS) $(LIBS) -o $@ -Wl,-Map,$(BUILD)/opendune.map

# --- disc images -------------------------------------------------------------

$(BUILD)/%/disc/0.BIN: $(BUILD)/%.elf
	@mkdir -p $(@D)
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.iso: $(BUILD)/ip.bin $(BUILD)/%/disc/0.BIN tools/mkdisc.sh
	tools/mkdisc.sh $(BUILD)/ip.bin $(BUILD)/$*/disc $(BUILD)/$*

# the engine disc also carries the game's PAK archives, in upper case
DATA_PAKS := $(wildcard $(DATA)/*.PAK $(DATA)/*.pak)

# (the marker file sits next to the disc tree, not in it)
$(BUILD)/%/data.stamp: $(DATA_PAKS)
	@[ -n "$(DATA_PAKS)" ] || { echo "No PAK files in $(DATA) (set DATA=...)"; exit 1; }
	@mkdir -p $(@D)/disc
	for f in $(DATA_PAKS); do cp $$f $(@D)/disc/$$(basename $$f | tr a-z A-Z); done
	touch $@

$(BUILD)/opendune.iso: $(BUILD)/opendune/data.stamp $(BUILD)/opendune/bank.stamp

# instrument bank for the MIDI synthesizer (src/audio/midi_saturn.c)
$(BUILD)/opendune/bank.stamp: tools/make_bank.py tools/xmi.py $(wildcard $(DATA)/SOUND.PAK $(DATA)/sound.pak) $(wildcard $(SOUNDFONT))
	@mkdir -p $(@D)/disc
	@if [ -f "$(SOUNDFONT)" ]; then \
		python3 -B tools/make_bank.py $(firstword $(wildcard $(DATA)/SOUND.PAK $(DATA)/sound.pak)) $(SOUNDFONT) $(@D)/disc/INSTR.BNK; \
	else \
		echo "warning: no SoundFont at $(SOUNDFONT) (set SOUNDFONT=...): the disc will have no music"; \
		rm -f $(@D)/disc/INSTR.BNK; \
	fi
	touch $@

# keep the files the pattern rules produce along the way
.SECONDARY:

clean:
	rm -rf $(BUILD)
