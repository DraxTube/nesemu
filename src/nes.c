/*
 * NES System - bus arbitration, timing, save states
 */
#include "nes.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- CPU Bus ---- */
uint8_t nes_cpu_read(NES *nes, uint16_t addr) {
    if (addr < 0x2000) {
        return nes->ram[addr & 0x07FF];
    } else if (addr < 0x4000) {
        return ppu_read_reg(&nes->ppu, addr & 7);
    } else if (addr == 0x4015) {
        return apu_read(&nes->apu, addr);
    } else if (addr == 0x4016) {
        uint8_t v = (nes->ctrl_shift[0] >> 7) & 1;
        nes->ctrl_shift[0] <<= 1;
        return v;
    } else if (addr == 0x4017) {
        uint8_t v = (nes->ctrl_shift[1] >> 7) & 1;
        nes->ctrl_shift[1] <<= 1;
        return v;
    } else if (addr < 0x4020) {
        return 0;
    } else {
        /* Cartridge space */
        return nes->cart.mapper->prg_read(nes->cart.mapper, addr);
    }
}

void nes_cpu_write(NES *nes, uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        nes->ram[addr & 0x07FF] = val;
    } else if (addr < 0x4000) {
        ppu_write_reg(&nes->ppu, addr & 7, val);
    } else if (addr == 0x4014) {
        /* OAM DMA */
        nes->dma_page   = val;
        nes->dma_addr   = 0;
        nes->dma_active = 1;
        nes->dma_cycles = 513 + (nes->cpu_cycles & 1);
    } else if (addr == 0x4016) {
        uint8_t old = nes->ctrl_strobe;
        nes->ctrl_strobe = val & 1;
        if (old && !nes->ctrl_strobe) {
            /* Latch controllers */
            nes->ctrl_shift[0] = nes->ctrl_state[0];
            nes->ctrl_shift[1] = nes->ctrl_state[1];
        }
    } else if (addr < 0x4018) {
        apu_write(&nes->apu, addr, val);
    } else if (addr < 0x4020) {
        /* Test registers, ignore */
    } else {
        nes->cart.mapper->prg_write(nes->cart.mapper, addr, val);
    }
}

/* ---- PPU CHR bus ---- */
uint8_t nes_ppu_chr_read(NES *nes, uint16_t addr) {
    return nes->cart.mapper->chr_read(nes->cart.mapper, addr);
}
void nes_ppu_chr_write(NES *nes, uint16_t addr, uint8_t val) {
    nes->cart.mapper->chr_write(nes->cart.mapper, addr, val);
}
uint16_t nes_ppu_nt_mirror(NES *nes, uint16_t addr) {
    return mapper_nt_mirror(nes->cart.mirror, addr);
}

/* ---- Init / Reset / Free ---- */
int nes_init(NES *nes, const char *rom_path) {
    memset(nes, 0, sizeof(NES));

    if (cart_load(&nes->cart, rom_path) != 0) return -1;

    nes->cart.mapper = mapper_create(nes->cart.mapper_id, &nes->cart, nes);
    if (!nes->cart.mapper) return -1;

    cpu_init(&nes->cpu, nes);
    ppu_init(&nes->ppu, nes);
    apu_init(&nes->apu, nes);

    nes_reset(nes);
    nes->running = 1;
    return 0;
}

void nes_reset(NES *nes) {
    cpu_reset(&nes->cpu);
    ppu_reset(&nes->ppu);
    apu_reset(&nes->apu);
    if (nes->cart.mapper && nes->cart.mapper->reset)
        nes->cart.mapper->reset(nes->cart.mapper);
    nes->dma_active = 0;
    nes->cpu_cycles = 0;
    nes->ppu_cycles = 0;
    nes->frame_ready = 0;
}

void nes_free(NES *nes) {
    cart_free(&nes->cart);
}

/* ---- Frame runner ---- */
void nes_run_frame(NES *nes) {
    nes->frame_ready = 0;
    int frame = nes->ppu.frame;

    while (!nes->frame_ready && nes->running) {
        int cpu_cyc;

        if (nes->dma_active) {
            /* DMA transfer */
            if (nes->dma_cycles > 0) {
                nes->dma_cycles--;
                /* Transfer one byte every other cycle */
                if (nes->dma_cycles > 0 && (nes->dma_cycles & 1) == 0) {
                    uint16_t src = (nes->dma_page << 8) | nes->dma_addr;
                    uint8_t  val = nes_cpu_read(nes, src);
                    nes->ppu.oam[nes->dma_addr++] = val;
                }
            } else {
                nes->dma_active = 0;
            }
            cpu_cyc = 1;
        } else {
            cpu_cyc = cpu_step(&nes->cpu);
        }

        nes->cpu_cycles += cpu_cyc;

        /* APU: 1 tick per CPU cycle */
        for (int i = 0; i < cpu_cyc; i++) {
            apu_step(&nes->apu);
        }

        /* PPU: 3 ticks per CPU cycle */
        int ppu_ticks = cpu_cyc * 3;
        for (int i = 0; i < ppu_ticks; i++) {
            int old_frame = nes->ppu.frame;
            ppu_step(&nes->ppu);
            /* Scanline IRQ for MMC3 at start of each scanline */
            if (nes->ppu.dot == 260 && nes->ppu.scanline < 240) {
                nes->cart.mapper->step(nes->cart.mapper);
            }
            if (nes->ppu.frame != old_frame) {
                nes->frame_ready = 1;
            }
        }

        if (nes->ppu.frame != frame) break;
    }
}

void nes_set_controller(NES *nes, int player, uint8_t buttons) {
    if (player < 0 || player > 1) return;
    nes->ctrl_state[player] = buttons;
    if (nes->ctrl_strobe) {
        nes->ctrl_shift[player] = buttons;
    }
}

/* ---- Save State ---- */
#define SAVE_MAGIC  0x4E455353  /* "NESS" */
#define SAVE_VER    1

typedef struct {
    uint32_t magic;
    uint32_t version;
    CPU      cpu;
    PPU      ppu;
    APU      apu;
    uint8_t  ram[2048];
    uint8_t  ctrl_state[2];
    uint8_t  oam[256];
    uint8_t  pal[32];
    uint8_t  vram[2048];
    uint8_t  prg_ram[8192];
    uint8_t  chr_ram[8192];
} SaveState;

int nes_save_state(NES *nes, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    SaveState ss;
    memset(&ss, 0, sizeof(ss));
    ss.magic   = SAVE_MAGIC;
    ss.version = SAVE_VER;
    ss.cpu     = nes->cpu;
    ss.ppu     = nes->ppu;
    ss.apu     = nes->apu;
    memcpy(ss.ram, nes->ram, 2048);
    ss.ctrl_state[0] = nes->ctrl_state[0];
    ss.ctrl_state[1] = nes->ctrl_state[1];
    memcpy(ss.oam,  nes->ppu.oam,  256);
    memcpy(ss.pal,  nes->ppu.pal,   32);
    memcpy(ss.vram, nes->ppu.vram, 2048);
    memcpy(ss.prg_ram, nes->cart.prg_ram, 8192);
    memcpy(ss.chr_ram, nes->cart.chr_ram, 8192);

    fwrite(&ss, sizeof(ss), 1, f);
    fclose(f);
    return 0;
}

int nes_load_state(NES *nes, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    SaveState ss;
    if (fread(&ss, sizeof(ss), 1, f) != 1) { fclose(f); return -1; }
    fclose(f);

    if (ss.magic != SAVE_MAGIC || ss.version != SAVE_VER) return -1;

    /* Restore (preserve pointers) */
    NES *n = nes->cpu.nes; /* same pointer */
    nes->cpu = ss.cpu; nes->cpu.nes = n;
    nes->ppu = ss.ppu; nes->ppu.nes = n;
    nes->apu = ss.apu; nes->apu.nes = n;
    memcpy(nes->ram, ss.ram, 2048);
    nes->ctrl_state[0] = ss.ctrl_state[0];
    nes->ctrl_state[1] = ss.ctrl_state[1];
    memcpy(nes->ppu.oam,  ss.oam,  256);
    memcpy(nes->ppu.pal,  ss.pal,   32);
    memcpy(nes->ppu.vram, ss.vram, 2048);
    memcpy(nes->cart.prg_ram, ss.prg_ram, 8192);
    memcpy(nes->cart.chr_ram, ss.chr_ram, 8192);
    return 0;
}
