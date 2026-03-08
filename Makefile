# NESPSP - NES Emulator for PSP
# Requires: pspdev toolchain (https://github.com/pspdev/pspdev)

TARGET      = NESPSP
OBJS        = src/main.o     \
              src/cpu.o      \
              src/ppu.o      \
              src/apu.o      \
              src/nes.o      \
              src/cart.o     \
              src/mapper.o   \
              src/psp_video.o \
              src/psp_audio.o \
              src/psp_input.o \
              src/psp_menu.o

# PSP metadata
BUILD_PRX    = 1
PSP_FW_VERSION = 371
EXTRA_TARGETS = EBOOT.PBP

PSP_EBOOT_TITLE = NESPSP
PSP_EBOOT_ICON  =
PSP_EBOOT_PIC1  =

# Optimization for speed
CFLAGS   = -O3 -G0 -Wall -Wextra \
           -ffast-math           \
           -fomit-frame-pointer  \
           -DPSP                 \
           -I$(PSPSDK)/include   \
           -Isrc

CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti

LDFLAGS  = -L$(PSPSDK)/lib

LIBS     = -lpspgu      \
           -lpspgum     \
           -lpspdisplay \
           -lpspaudio   \
           -lpspctrl    \
           -lpspiofilemgr \
           -lpsppower   \
           -lpspkernel_stub \
           -lm

PSPSDK   = $(shell psp-config --pspsdk-path)

include $(PSPSDK)/lib/build.mak
