# Nintendo Switch build (devkitPro / libnx).
# Install devkitPro, then: dkp-pacman -S switch-dev switch-zlib
# Build with: make

TARGET      := telegramnx
BUILD       := build_switch
SOURCES     := $(wildcard src/core/*.cpp) $(wildcard src/ui/*.cpp) \
               third_party/qrcodegen.cpp \
               src/platform/switch_platform.cpp src/platform/switch_main.cpp
OFILES      := $(patsubst %.cpp,$(BUILD)/%.o,$(SOURCES))
APP_TITLE   := TelegramNX
APP_AUTHOR  := homebrew
APP_VERSION := 0.1.0

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set. Install devkitPro and export DEVKITPRO=/opt/devkitpro")
endif

DEVKITA64 := $(DEVKITPRO)/devkitA64
LIBNX     := $(DEVKITPRO)/libnx
PORTLIBS  := $(DEVKITPRO)/portlibs/switch

PREFIX  := $(DEVKITA64)/bin/aarch64-none-elf-
CXX     := $(PREFIX)g++
CC      := $(PREFIX)gcc
AS      := $(PREFIX)as
LD      := $(CXX)
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro
NACPTOOL:= $(DEVKITPRO)/tools/bin/nacptool
NXTOOL  := $(DEVKITPRO)/tools/bin/nxtool

ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
DEFINES := -D__SWITCH__ -DTGNX_SWITCH
INCLUDES:= -Isrc -Ithird_party -I$(LIBNX)/include -I$(PORTLIBS)/include

CFLAGS  := -g -O2 -Wall -ffunction-sections -fdata-sections -fexceptions \
           $(ARCH) $(DEFINES) $(INCLUDES)
CXXFLAGS:= $(CFLAGS) -std=c++17
LDFLAGS := -specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,--gc-sections
LIBS    := -L$(LIBNX)/lib -L$(PORTLIBS)/lib -lz -lm -lnx

all: $(BUILD)/$(TARGET).nro

$(BUILD)/$(TARGET).nacp:
	@mkdir -p $(BUILD)
	$(NACPTOOL) --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(BUILD)/$(TARGET).nro: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).nacp
	$(ELF2NRO) $< $@ --nacp=$(BUILD)/$(TARGET).nacp

$(BUILD)/$(TARGET).elf: $(OFILES)
	@mkdir -p $(BUILD)
	$(LD) $(LDFLAGS) $(OFILES) $(LIBS) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

install: $(BUILD)/$(TARGET).nro
	@echo "Copy $(BUILD)/$(TARGET).nro to sdmc:/switch/ and the fonts to sdmc:/switch/telegramnx/fonts/"

.PHONY: all clean install
