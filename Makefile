# Cannonball (OutRun engine) — Retro-Go SD GWHB homebrew for the Game & Watch.
#
#   make                         build + pack Cannonball.bin  (-> SD /homebrews/)
#   make GCC_PATH=/path/to/bin   toolchain not on PATH (Arm GNU Toolchain 15.2.rel1)
#   make docker                  same toolchain image as retro-go-sd CI
#
# external/cannonball is djyt/cannonball with a minimal patch: tools/prepare_cannonball.py.
# Runtime data: the user's OutRun rev. B ROMs in /cannonball/roms/ (converted on first launch).

PROJECT_KIND ?= homebrew
ifneq ($(PROJECT_KIND),homebrew)
$(error Cannonball is a homebrew: PROJECT_KIND=homebrew)
endif

CORE_NAME  := cannonball
CORE_ENTRY := app_main

CB := external/cannonball

CORE_C_SOURCES := src/gw/main_cannonball.c

CORE_CXX_SOURCES := \
src/gw/gw_engine.cpp \
src/gw/gw_menu.cpp \
src/gw/config_gw.cpp \
src/gw/roms_gw.cpp \
src/gw/romconv.cpp \
src/gw/utils_gw.cpp \
src/gw/cxx_runtime_gw.cpp \
$(CB)/video.cpp \
$(CB)/trackloader.cpp \
$(CB)/hwvideo/hwroad.cpp \
$(CB)/hwvideo/hwsprites.cpp \
$(CB)/hwvideo/hwtiles.cpp \
$(CB)/hwaudio/segapcm.cpp \
$(CB)/hwaudio/soundchip.cpp \
$(CB)/hwaudio/ym2151.cpp \
$(CB)/engine/audio/osound.cpp \
$(CB)/engine/audio/osoundint.cpp \
$(CB)/engine/oanimseq.cpp \
$(CB)/engine/oattractai.cpp \
$(CB)/engine/obonus.cpp \
$(CB)/engine/ocrash.cpp \
$(CB)/engine/oferrari.cpp \
$(CB)/engine/ohiscore.cpp \
$(CB)/engine/ohud.cpp \
$(CB)/engine/oinitengine.cpp \
$(CB)/engine/oinputs.cpp \
$(CB)/engine/olevelobjs.cpp \
$(CB)/engine/ologo.cpp \
$(CB)/engine/omap.cpp \
$(CB)/frontend/ttrial.cpp \
$(CB)/engine/omusic.cpp \
$(CB)/engine/ooutputs.cpp \
$(CB)/engine/opalette.cpp \
$(CB)/engine/oroad.cpp \
$(CB)/engine/osmoke.cpp \
$(CB)/engine/osprite.cpp \
$(CB)/engine/osprites.cpp \
$(CB)/engine/ostats.cpp \
$(CB)/engine/otiles.cpp \
$(CB)/engine/otraffic.cpp \
$(CB)/engine/outils.cpp \
$(CB)/engine/outrun.cpp

CORE_C_INCLUDES := -Isrc/gw

CORE_C_DEFS := -DPROJECT_KIND_HOMEBREW=1

# The renderers and sound chips are the hot path (measured on the G&W: ~19 ms of the 33 ms
# frame at 280 MHz with -O2). -Os would save code size at a real speed cost.
OPT ?= -O2

GNW_CORE_SDK ?= sdk
BUILD_DIR ?= build

PACKED_BIN := Cannonball.bin
HB_NAME    := Cannonball
COVER_SRC  := src/assets/cover.png
COVER_JPG  := $(BUILD_DIR)/cover.jpg

include $(GNW_CORE_SDK)/Makefile

# Engine (C++) files: the engine's own tree first, then the shims (SDL types, null iostream).
CB_CXX_INC := -I$(CB) -Isrc/gw/shim
CB_WARN_OFF := -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare \
	-Wno-unused-function -Wno-reorder -Wno-misleading-indentation -Wno-parentheses \
	-Wno-class-memaccess -Wno-maybe-uninitialized -Wno-narrowing -Wno-unused-value \
	-Wno-dangling-else -Wno-strict-aliasing -Wno-switch -Wno-array-bounds
CXXFLAGS += $(CB_CXX_INC) $(CB_WARN_OFF) -std=gnu++14


CORE_LDLIBS := -lm

CORE_VERSION ?= 0.1.0

.PHONY: pack cover
cover: $(COVER_JPG)
$(COVER_JPG): $(COVER_SRC)
	$(V)$(ECHO) [ COVER ] $(COVER_JPG)
	$(V)mkdir -p $(BUILD_DIR)
	$(V)python3 -c "from pathlib import Path; from PIL import Image; \
img=Image.open('$(COVER_SRC)').convert('RGB'); \
img.thumbnail((186,100)); \
img.save('$(COVER_JPG)', 'JPEG', quality=85, optimize=True); \
sz=Path('$(COVER_JPG)').stat().st_size; \
assert sz <= 10*1024, f'cover too big: {sz}'"

PACK_HOMEBREW := $(GNW_CORE_SDK)/tools/pack_homebrew.py
pack: $(TARGET_BIN) $(if $(wildcard $(COVER_SRC)),$(COVER_JPG))
	$(V)$(ECHO) [ PACK GWHB ] "$(PACKED_BIN)" version=$(CORE_VERSION)
	$(V)python3 $(PACK_HOMEBREW) \
		--nm $(if $(GCC_PATH),$(GCC_PATH)/)arm-none-eabi-nm \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--name "$(HB_NAME)" --version "$(CORE_VERSION)" \
		$(if $(wildcard $(COVER_SRC)),--cover $(COVER_JPG)) \
		--out "$(BUILD_DIR)/$(PACKED_BIN)"

all: pack

clean::
	$(V)rm -f "$(BUILD_DIR)/$(PACKED_BIN)" $(COVER_JPG)

.PHONY: docker
DOCKER_IMAGE ?= sylverb/retro-go-sd-builder:v1.5
docker:
	docker run --rm --user $$(id -u):$$(id -g) -v "$(CURDIR):/opt/workdir" -w /opt/workdir \
		$(DOCKER_IMAGE) make --no-print-directory -j$$(nproc)
