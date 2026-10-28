#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET		:= Trinity3DS
BUILD		:= build
SRC_ROOT	:= ../../src

APP_TITLE	:= Trinity3DS
APP_DESCRIPTION	:= dsda-doom port for Nintendo 3DS (Doom, Heretic, Hexen)
APP_AUTHOR	:= mcl5500 & dsda-doom contributors
ICON		:= res/icon.png

ARCH	:= -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

INCLUDES := -I$(SRC_ROOT) \
	    -I$(SRC_ROOT)/3DS \
	    -I$(BUILD)/3DS \
	    -I$(PORTLIBS)/include/SDL \
	    -I$(PORTLIBS)/include \
	    -I$(CTRULIB)/include

CFLAGS	:= $(ARCH) $(INCLUDES) -std=gnu17 -Wno-incompatible-pointer-types -Wno-int-conversion \
	   -Wno-return-mismatch -Wno-discarded-qualifiers -Wno-unused-result -O3 -mword-relocations \
	   -ffunction-sections -fdata-sections -ffast-math -fno-short-enums \
	   -D__3DS__ -DHAVE_CONFIG_H -DHAVE_LIBZ

CXXFLAGS := $(ARCH) $(INCLUDES) -std=gnu++17 -O3 -mword-relocations \
	    -ffunction-sections -fdata-sections -ffast-math -fno-short-enums \
	    -fno-rtti -fno-exceptions \
	    -D__3DS__ -DHAVE_CONFIG_H -DHAVE_LIBZ

ASFLAGS	:= -march=armv6k
LDFLAGS	:= -specs=3dsx.specs $(ARCH) -Wl,-no-enum-size-warning -Wl,--gc-sections -Wl,-Map,$(TARGET).map

LIBDIRS	:= $(PORTLIBS) $(CTRULIB)
LIBPATHS := -L$(PORTLIBS)/lib -L$(CTRULIB)/lib

LIBS	:= -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lminizip -ljson-c -lpng -lSDL_mixer -lSDL -lz -lmikmod -lvorbisidec -logg -lmad -lstdc++ -lcitro3d -lctru -lm

# Collect sources
C_SRCS := $(shell find $(SRC_ROOT) -type f -name "*.c" \
	! -path "$(SRC_ROOT)/SDL/*" \
	! -path "$(SRC_ROOT)/Win32/*" \
	! -path "$(SRC_ROOT)/textscreen/*" \
	! -path "$(SRC_ROOT)/gl_clipper.c" \
	! -path "$(SRC_ROOT)/gl_drawinfo.c" \
	! -path "$(SRC_ROOT)/gl_fbo.c" \
	! -path "$(SRC_ROOT)/gl_light.c" \
	! -path "$(SRC_ROOT)/gl_main.c" \
	! -path "$(SRC_ROOT)/gl_map.c" \
	! -path "$(SRC_ROOT)/gl_opengl.c" \
	! -path "$(SRC_ROOT)/gl_preprocess.c" \
	! -path "$(SRC_ROOT)/gl_progress.c" \
	! -path "$(SRC_ROOT)/gl_shader.c" \
	! -path "$(SRC_ROOT)/gl_sky.c" \
	! -path "$(SRC_ROOT)/gl_texture.c" \
	! -path "$(SRC_ROOT)/gl_vertex.c" \
	! -path "$(SRC_ROOT)/gl_wipe.c" \
	! -path "$(SRC_ROOT)/dsda/gl/*")

CPP_SRCS := $(shell find $(SRC_ROOT) -type f -name "*.cpp" \
	! -path "$(SRC_ROOT)/textscreen/*")

PICA_SRCS := $(shell find $(SRC_ROOT) -type f -name "*.v.pica")

# Generate object paths preserving directory hierarchy under $(BUILD)/
C_OBJS := $(patsubst $(SRC_ROOT)/%.c, $(BUILD)/%.o, $(C_SRCS))
CPP_OBJS := $(patsubst $(SRC_ROOT)/%.cpp, $(BUILD)/%.cpp.o, $(CPP_SRCS))
PICA_OBJS := $(patsubst $(SRC_ROOT)/%.v.pica, $(BUILD)/%.shbin.o, $(PICA_SRCS))
PICA_HEADERS := $(patsubst $(SRC_ROOT)/%.v.pica, $(BUILD)/%_shbin.h, $(PICA_SRCS))

ALL_OBJS := $(PICA_OBJS) $(C_OBJS) $(CPP_OBJS)

.PHONY: all clean

all: $(TARGET).3dsx

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	@echo "3DSXTOOL $@"
	@3dsxtool $(TARGET).elf $@ --smdh=$(TARGET).smdh

$(TARGET).smdh: res/icon.png
	@smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $< $@

$(TARGET).elf: $(PICA_HEADERS) $(ALL_OBJS)
	@echo "LD $@"
	@$(CXX) $(LDFLAGS) $(ALL_OBJS) $(LIBPATHS) $(LIBS) -o $@

$(BUILD)/%.o: $(SRC_ROOT)/%.c $(PICA_HEADERS)
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.cpp.o: $(SRC_ROOT)/%.cpp $(PICA_HEADERS)
	@mkdir -p $(dir $@)
	@echo "CXX $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%_shbin.h $(BUILD)/%.shbin.o: $(SRC_ROOT)/%.v.pica
	@mkdir -p $(dir $@)
	@echo "PICA $<"
	@picasso -o $(BUILD)/$*.shbin $<
	@bin2s $(BUILD)/$*.shbin | $(AS) $(ASFLAGS) -o $(BUILD)/$*.shbin.o
	@echo "extern const u8" `(echo $(notdir $*)_shbin | tr . _)`"_end[];" > $(BUILD)/$*_shbin.h
	@echo "extern const u8" `(echo $(notdir $*)_shbin | tr . _)`"[];" >> $(BUILD)/$*_shbin.h
	@echo "extern const u32" `(echo $(notdir $*)_shbin | tr . _)`_size";" >> $(BUILD)/$*_shbin.h

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).map
