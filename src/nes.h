#ifndef NES_H
#define NES_H

#include <stdint.h>
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "cart.h"
#include "mapper.h"

/* Controller bits */
#define BTN_A       0x01
#define BTN_B       0x02
#define BTN_SELECT  0x04
#define BTN_START   0x08
#define BTN_UP      0x10
#define BTN_DOWN    0x20
#define BTN_LEFT    0x40
#define BTN_RIGHT   0x80

typedef struct NES {
    CPU     cpu;
    PPU     ppu;
    APU     apu;
    Cart    cart;

    uint8_t ram[2048];          /* 2KB internal RAM */

    /* Controllers */
    uint8_t ctrl_state[2];      /* latched state */
    uint8_t ctrl_shift[2];      /* shift register */
    uint8_t ctrl_strobe;

    /* DMA */
    uint8_t  dma_page;
    uint8_t  dma_addr;
    int      dma_cycles;
    int      dma_active;

    /* Cycle timing */
    int      cpu_cycles;
    int      ppu_cycles;        /* PPU runs at 3x CPU clock */

    int      running;
    int      frame_ready;
} NES;

int  nes_init(NES *nes, const char *rom_path);
void nes_reset(NES *nes);
void nes_free(NES *nes);
void nes_run_frame(NES *nes);   /* Run one full frame */
void nes_set_controller(NES *nes, int player, uint8_t buttons);

/* Bus access (called by CPU/PPU) */
uint8_t  nes_cpu_read(NES *nes, uint16_t addr);
void     nes_cpu_write(NES *nes, uint16_t addr, uint8_t val);
uint8_t  nes_ppu_chr_read(NES *nes, uint16_t addr);
void     nes_ppu_chr_write(NES *nes, uint16_t addr, uint8_t val);
uint16_t nes_ppu_nt_mirror(NES *nes, uint16_t addr);

/* Save states */
int  nes_save_state(NES *nes, const char *path);
int  nes_load_state(NES *nes, const char *path);

#endif /* NES_H */
