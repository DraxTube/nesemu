# NESPSP — NES Emulator for PSP

A complete, accurate NES emulator for Sony PSP.

## Features

### CPU
- Full MOS 6502 CPU emulation
- All 56 official opcodes
- Undocumented opcodes: LAX, SAX, SLO, RLA, SRE, RRA, DCP, ISC and NOPs
- Correct cycle counting with page-cross penalties
- Accurate NMI, IRQ, BRK interrupt handling

### PPU (Picture Processing Unit)
- Accurate scanline-based rendering (256×240)
- Background rendering with fine/coarse scrolling
- 64 hardware sprites, 8 per scanline with proper overflow flag
- Sprite 0 hit detection
- 8×8 and 8×16 sprite modes
- All 4 nametable mirror modes (horizontal, vertical, single, 4-screen)
- NES standard 64-color palette (NTSC)

### APU (Audio Processing Unit)
- Pulse 1 & 2 (4 duty cycles, sweep, envelope)
- Triangle channel (linear counter)
- Noise channel (short/long mode)
- DMC (Delta Modulation Channel) with IRQ support
- All 5 channels mixed with proper NES non-linear mixing table
- 44100Hz stereo output

### Mappers Supported
| # | Name | Games |
|---|------|-------|
| 0 | NROM | Super Mario Bros, Donkey Kong, Pac-Man |
| 1 | MMC1 | Mega Man 2, Metroid, Legend of Zelda |
| 2 | UxROM | Contra, Castlevania, Mega Man |
| 3 | CNROM | Arkanoid, Paperboy |
| 4 | MMC3 | Super Mario Bros 3, Kirby's Adventure, Mega Man 3-6 |
| 7 | AxROM | Battletoads, Marble Madness |
| 9 | MMC2 | Punch-Out!! |
| 11 | Color Dreams | Crystal Mines |
| 66 | GxROM | Super Mario Bros + Duck Hunt |
| 71 | Camerica | Micro Machines, Bee 52 |

### PSP-Specific Features
- 480×272 display with centered 256×240 NES output
- PSP GU hardware-accelerated texture blitting
- 44100Hz double-buffered audio
- Analog stick support
- Save states (stored in `ms0:/NES/saves/`)
- ROM browser for `ms0:/NES/`
- In-game pause menu
- OSD notifications
- CPU overclocked to 333MHz for maximum performance

## Controls

| PSP Button | NES Function |
|-----------|-------------|
| Cross (×) | A |
| Circle (○) | B |
| Select | Select |
| Start | Start |
| D-pad | D-pad |
| L + R | Pause menu |
| L + Select | Save state |
| R + Select | Load state |
| L + Start | Reset |
| Home | Back to ROM browser |

## Installation

### From GitHub Actions (recommended)
1. Push/commit to trigger the build workflow
2. Download `NESPSP-EBOOT` artifact from the Actions tab
3. Copy `EBOOT.PBP` to `ms0:/PSP/GAME/NESPSP/EBOOT.PBP` on your PSP memory stick
4. Copy your `.nes` ROM files to `ms0:/NES/` on your memory stick

### Build manually
Requirements: pspdev toolchain

```bash
# Install pspdev
git clone https://github.com/pspdev/pspdev.git
cd pspdev && ./build-all.sh

# Build emulator
export PSPDEV=/usr/local/pspdev
export PATH=$PATH:$PSPDEV/bin:$PSPDEV/psp/bin
make -j4
```

### ROM Placement
```
ms0:/
├── PSP/
│   └── GAME/
│       └── NESPSP/
│           └── EBOOT.PBP
└── NES/
    ├── Super Mario Bros.nes
    ├── Contra.nes
    └── saves/          (auto-created)
```

## Project Structure

```
nesemu-psp/
├── .github/workflows/build.yml   # CI/CD pipeline
├── Makefile                      # PSP build system
├── README.md
└── src/
    ├── main.c          # PSP entry point, emulation loop
    ├── cpu.c/h         # MOS 6502 CPU
    ├── ppu.c/h         # Picture Processing Unit
    ├── apu.c/h         # Audio Processing Unit
    ├── nes.c/h         # System bus, timing, save states
    ├── cart.c/h        # iNES cartridge loader
    ├── mapper.c/h      # Mapper implementations
    ├── psp_video.c/h   # GU-based display
    ├── psp_audio.c/h   # sceAudio output
    ├── psp_input.c/h   # Controller input
    ├── psp_menu.c/h    # ROM browser
    └── font_data.h     # Embedded 8×8 bitmap font
```

## Compatibility Notes

- Tested mapper coverage handles ~85% of the NES library
- iNES 1.0 and NES 2.0 ROM formats supported
- CHR-RAM games fully supported
- Battery saves loaded/stored in `ms0:/NES/saves/` (auto on save state)

## License

MIT License. NES is a trademark of Nintendo. This project is not affiliated with Nintendo.
