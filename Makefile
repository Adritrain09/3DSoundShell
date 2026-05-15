#---------------------------------------------------------------------------------
# 3DSoundShell — Makefile pour devkitARM
# Requiert devkitPro avec 3ds-dev, citro2d, citro3d
#---------------------------------------------------------------------------------

APP_TITLE    := 3DSoundShell
APP_DESCRIPTION := Lecteur de musique 3DS
APP_AUTHOR   := 3DSoundShell
APP_PRODUCT_CODE := CTR-P-TDSS
APP_UNIQUE_ID := 0xFF3D5

TARGET       := 3DSoundShell
BUILD        := build
SOURCES      := source
INCLUDES     := include
ROMFS        := romfs
GFXBUILD     := $(BUILD)/gfx

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := -g -Wall -O2 -mword-relocations \
            -fomit-frame-pointer -ffunction-sections \
            $(ARCH) $(INCLUDE) -D__3DS__ -I/opt/devkitpro/portlibs/3ds/include/opus

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS  := -g $(ARCH)

LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

#---------------------------------------------------------------------------------
# Libraries
#  Order matters for static linking
#---------------------------------------------------------------------------------
LIBS     := -lopusfile -lopus -logg \
            -lmpg123 \
            -lvorbisidec \
            -lFLAC \
            -lcitro2d -lcitro3d \
            -lctru \
            -lm

LIBDIRS  := /opt/devkitpro/libctru /opt/devkitpro/portlibs/3ds

#---------------------------------------------------------------------------------
# Include devkitARM rules
#---------------------------------------------------------------------------------
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# Targets
#---------------------------------------------------------------------------------
.PHONY: all clean 3dsx cia

all: $(TARGET).3dsx $(TARGET).cia

3dsx: $(TARGET).3dsx

#---------------------------------------------------------------------------------
# Build rules
#---------------------------------------------------------------------------------
$(TARGET).3dsx: $(TARGET).elf
	3dsxtool $< $@ --romfs=$(ROMFS) --smdh=$(TARGET).smdh

$(TARGET).smdh: $(APP_TITLE).png
	bannertool/windows-x86_64/bannertool.exe makesmdh -s "$(APP_TITLE)" \
	    -l "$(APP_DESCRIPTION)" \
	    -p "$(APP_AUTHOR)" \
	    -i romfs/gfx/icon.png \
	    -o $@

$(TARGET).elf:
	$(MAKE) --no-print-directory -C $(BUILD) \
	    -f $(CURDIR)/Makefile.inner \
	    TARGET=$(CURDIR)/$(TARGET) \
	    SOURCES=$(CURDIR)/$(SOURCES) \
	    INCLUDES=$(CURDIR)/$(INCLUDES) \
	    ROMFS=$(CURDIR)/$(ROMFS) \
	    CFLAGS="$(CFLAGS)" \
	    LDFLAGS="$(LDFLAGS)" \
	    LIBS="$(LIBS)" \
	    LIBDIRS="$(LIBDIRS)"

#---------------------------------------------------------------------------------
# CIA target (requires makerom + bannertool)
#---------------------------------------------------------------------------------
cia: $(TARGET).cia

$(TARGET).cia: $(TARGET).3dsx $(TARGET).smdh
	@echo "Génération de la bannière..."
	bannertool/windows-x86_64/bannertool.exe makebanner \
	    -i romfs/gfx/banner.png \
	    -a romfs/audio/silence.wav \
	    -o banner.bin

	@echo "Génération de l'icône SMDH..."
	bannertool/windows-x86_64/bannertool.exe makesmdh \
	    -s "$(APP_TITLE)" \
	    -l "$(APP_DESCRIPTION)" \
	    -p "$(APP_AUTHOR)" \
	    -i romfs/gfx/icon.png \
	    -o icon.icn

	@echo "Création du .cia avec makerom..."
	makerom -f cia \
	    -target t \
	    -exefslogo \
	    -elf $(TARGET).elf \
	    -rsf cia.rsf \
	    -banner banner.bin \
	    -icon icon.icn \
	    \
	    -o $(TARGET).cia

	@echo ""
	@echo "✅  $(TARGET).cia généré avec succès!"
	@echo "   → Copiez-le sur votre carte SD et installez avec FBI"

clean:
	@echo "Nettoyage..."
	@rm -rf $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh $(TARGET).cia
	@rm -f banner.bin icon.icn

#---------------------------------------------------------------------------------
# Inner build rules (used inside build/ directory)
#---------------------------------------------------------------------------------
CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
OFILES   := $(CFILES:.c=.o) $(CPPFILES:.cpp=.o)

INCLUDE  := $(foreach dir,$(INCLUDES),-I$(dir)) \
            $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
            -I$(BUILD)

LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) $(LIBPATHS) $(LIBS) -o $@

%.o: $(SOURCES)/%.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@
