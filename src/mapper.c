/*
 * NES Mapper implementations
 * Supports: 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM),
 *           4 (MMC3), 7 (AxROM), 9 (MMC2), 11 (Color Dreams),
 *           66 (GxROM), 71 (Camerica)
 */
#include "mapper.h"
#include "nes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- NT mirror helper ---- */
uint16_t mapper_nt_mirror(int mirror, uint16_t addr) {
    addr = (addr - 0x2000) & 0x0FFF;
    switch (mirror) {
    case MIRROR_HORIZ:   return (addr & 0x03FF) | ((addr & 0x0800) ? 0x400 : 0);
    case MIRROR_VERT:    return addr & 0x07FF;
    case MIRROR_SINGLE0: return addr & 0x03FF;
    case MIRROR_SINGLE1: return 0x400 | (addr & 0x03FF);
    case MIRROR_4SCREEN: return addr & 0x0FFF;
    }
    return addr & 0x07FF;
}

/* ===== MAPPER 0: NROM ===== */
static uint8_t m0_prg_read(Mapper *m, uint16_t a) {
    if (a >= 0x8000) {
        int bank = (a - 0x8000) & (m->cart->prg_size * 16384 - 1);
        return m->cart->prg_rom[bank];
    }
    if (a >= 0x6000) return m->cart->prg_ram[a - 0x6000];
    return 0;
}
static void m0_prg_write(Mapper *m, uint16_t a, uint8_t v) {
    if (a >= 0x6000 && a < 0x8000) m->cart->prg_ram[a - 0x6000] = v;
}
static uint8_t m0_chr_read(Mapper *m, uint16_t a) {
    if (m->cart->has_chr_ram) return m->cart->chr_ram[a & 0x1FFF];
    return m->cart->chr_rom[a & (m->cart->chr_size * 8192 - 1)];
}
static void m0_chr_write(Mapper *m, uint16_t a, uint8_t v) {
    if (m->cart->has_chr_ram) m->cart->chr_ram[a & 0x1FFF] = v;
}

/* ===== MAPPER 1: MMC1 ===== */
typedef struct {
    uint8_t  shift;
    uint8_t  shift_count;
    uint8_t  ctrl;
    uint8_t  chr0, chr1;
    uint8_t  prg;
    uint8_t  prg_bank[2];  /* current banks */
    uint8_t  chr_bank[2];
} MMC1;

static void mmc1_update(Mapper *m) {
    MMC1 *s = (MMC1 *)m->data;
    int prg_mode  = (s->ctrl >> 2) & 3;
    int chr_mode  = (s->ctrl >> 4) & 1;
    int prg_banks = m->cart->prg_size;

    if (chr_mode == 0) {
        /* 8KB CHR mode */
        s->chr_bank[0] = (s->chr0 & ~1) & (m->cart->chr_size * 2 - 1);
        s->chr_bank[1] = s->chr_bank[0] + 1;
    } else {
        /* 4KB CHR mode */
        s->chr_bank[0] = s->chr0 & (m->cart->chr_size * 2 - 1);
        s->chr_bank[1] = s->chr1 & (m->cart->chr_size * 2 - 1);
    }
    switch (prg_mode) {
    case 0: case 1:
        s->prg_bank[0] = (s->prg & ~1) & (prg_banks - 1);
        s->prg_bank[1] = s->prg_bank[0] + 1;
        break;
    case 2:
        s->prg_bank[0] = 0;
        s->prg_bank[1] = s->prg & (prg_banks - 1);
        break;
    case 3:
        s->prg_bank[0] = s->prg & (prg_banks - 1);
        s->prg_bank[1] = prg_banks - 1;
        break;
    }
    /* Mirror mode */
    switch (s->ctrl & 3) {
    case 0: m->cart->mirror = MIRROR_SINGLE0; break;
    case 1: m->cart->mirror = MIRROR_SINGLE1; break;
    case 2: m->cart->mirror = MIRROR_VERT;    break;
    case 3: m->cart->mirror = MIRROR_HORIZ;   break;
    }
}

static uint8_t m1_prg_read(Mapper *m, uint16_t a) {
    MMC1 *s = (MMC1 *)m->data;
    if (a >= 0x8000) {
        int half  = (a >= 0xC000) ? 1 : 0;
        int bank  = s->prg_bank[half];
        int off   = a & 0x3FFF;
        return m->cart->prg_rom[bank * 16384 + off];
    }
    if (a >= 0x6000) return m->cart->prg_ram[a - 0x6000];
    return 0;
}
static void m1_prg_write(Mapper *m, uint16_t a, uint8_t v) {
    MMC1 *s = (MMC1 *)m->data;
    if (a < 0x6000) return;
    if (a < 0x8000) { m->cart->prg_ram[a - 0x6000] = v; return; }
    if (v & 0x80) { s->shift = 0; s->shift_count = 0; s->ctrl |= 0x0C; mmc1_update(m); return; }
    s->shift |= (v & 1) << s->shift_count++;
    if (s->shift_count == 5) {
        uint8_t reg = (a >> 13) & 3;
        switch (reg) {
        case 0: s->ctrl = s->shift; break;
        case 1: s->chr0 = s->shift; break;
        case 2: s->chr1 = s->shift; break;
        case 3: s->prg  = s->shift & 0x0F; break;
        }
        mmc1_update(m);
        s->shift = 0; s->shift_count = 0;
    }
}
static uint8_t m1_chr_read(Mapper *m, uint16_t a) {
    MMC1 *s = (MMC1 *)m->data;
    if (m->cart->has_chr_ram) return m->cart->chr_ram[a & 0x1FFF];
    int bank = s->chr_bank[(a >= 0x1000) ? 1 : 0];
    int off  = a & 0x0FFF;
    int max  = m->cart->chr_size * 8192;
    return m->cart->chr_rom[(bank * 4096 + off) % max];
}
static void m1_chr_write(Mapper *m, uint16_t a, uint8_t v) {
    if (m->cart->has_chr_ram) m->cart->chr_ram[a & 0x1FFF] = v;
}
static void m1_reset(Mapper *m) {
    MMC1 *s = (MMC1 *)m->data;
    s->shift = 0; s->shift_count = 0; s->ctrl = 0x0C;
    mmc1_update(m);
}

/* ===== MAPPER 2: UxROM ===== */
typedef struct { uint8_t bank; } UxROM;
static uint8_t m2_prg_read(Mapper *m, uint16_t a) {
    UxROM *s = (UxROM *)m->data;
    if (a >= 0xC000) return m->cart->prg_rom[(m->cart->prg_size - 1) * 16384 + (a & 0x3FFF)];
    if (a >= 0x8000) return m->cart->prg_rom[s->bank * 16384 + (a & 0x3FFF)];
    return 0;
}
static void m2_prg_write(Mapper *m, uint16_t a, uint8_t v) {
    if (a >= 0x8000) ((UxROM *)m->data)->bank = v & (m->cart->prg_size - 1);
}

/* ===== MAPPER 3: CNROM ===== */
typedef struct { uint8_t bank; } CNROM;
static uint8_t m3_chr_read(Mapper *m, uint16_t a) {
    CNROM *s = (CNROM *)m->data;
    int max = m->cart->chr_size * 8192;
    return m->cart->chr_rom[(s->bank * 8192 + a) % max];
}
static void m3_prg_write(Mapper *m, uint16_t a, uint8_t v) {
    if (a >= 0x8000) ((CNROM *)m->data)->bank = v & (m->cart->chr_size - 1);
}

/* ===== MAPPER 4: MMC3 ===== */
typedef struct {
    uint8_t  bank_select;
    uint8_t  banks[8];
    uint8_t  mirror;
    uint8_t  prg_ram_protect;
    uint8_t  irq_latch;
    uint8_t  irq_counter;
    uint8_t  irq_enable;
    uint8_t  irq_reload;
} MMC3;

static void mmc3_get_prg(Mapper *m, uint16_t a, int *bank_out, int *off_out) {
    MMC3 *s = (MMC3 *)m->data;
    int prg_mode = (s->bank_select >> 6) & 1;
    int banks = m->cart->prg_size * 2; /* 8KB banks */
    int bank;
    int off = a & 0x1FFF;
    if (prg_mode == 0) {
        if      (a < 0xA000) bank = s->banks[6] & (banks - 1);
        else if (a < 0xC000) bank = s->banks[7] & (banks - 1);
        else if (a < 0xE000) bank = banks - 2;
        else                 bank = banks - 1;
    } else {
        if      (a < 0xA000) bank = banks - 2;
        else if (a < 0xC000) bank = s->banks[7] & (banks - 1);
        else if (a < 0xE000) bank = s->banks[6] & (banks - 1);
        else                 bank = banks - 1;
    }
    *bank_out = bank; *off_out = off;
}

static uint8_t m4_prg_read(Mapper *m, uint16_t a) {
    if (a >= 0x8000) {
        int bank, off;
        mmc3_get_prg(m, a, &bank, &off);
        return m->cart->prg_rom[bank * 8192 + off];
    }
    if (a >= 0x6000) return m->cart->prg_ram[a - 0x6000];
    return 0;
}
static void m4_prg_write(Mapper *m, uint16_t a, uint8_t v) {
    MMC3 *s = (MMC3 *)m->data;
    if (a < 0x6000) return;
    if (a < 0x8000) { m->cart->prg_ram[a - 0x6000] = v; return; }
    switch (a & 0xE001) {
    case 0x8000: s->bank_select = v; break;
    case 0x8001: s->banks[s->bank_select & 7] = v; break;
    case 0xA000:
        m->cart->mirror = (v & 1) ? MIRROR_HORIZ : MIRROR_VERT; break;
    case 0xA001: s->prg_ram_protect = v; break;
    case 0xC000: s->irq_latch = v; break;
    case 0xC001: s->irq_counter = 0; s->irq_reload = 1; break;
    case 0xE000: s->irq_enable = 0; break;
    case 0xE001: s->irq_enable = 1; break;
    }
}
static uint8_t m4_chr_read(Mapper *m, uint16_t a) {
    MMC3 *s = (MMC3 *)m->data;
    if (m->cart->has_chr_ram) return m->cart->chr_ram[a & 0x1FFF];
    int chr_mode = (s->bank_select >> 7) & 1;
    int banks = m->cart->chr_size * 8; /* 1KB banks */
    int bank;
    if (chr_mode == 0) {
        if      (a < 0x0800) bank = (s->banks[0] & ~1) + ((a >> 10) & 1);
        else if (a < 0x1000) bank = (s->banks[1] & ~1) + ((a >> 10) & 1);
        else if (a < 0x1400) bank = s->banks[2];
        else if (a < 0x1800) bank = s->banks[3];
        else if (a < 0x1C00) bank = s->banks[4];
        else                 bank = s->banks[5];
    } else {
        if      (a < 0x0400) bank = s->banks[2];
        else if (a < 0x0800) bank = s->banks[3];
        else if (a < 0x0C00) bank = s->banks[4];
        else if (a < 0x1000) bank = s->banks[5];
        else if (a < 0x1800) bank = (s->banks[0] & ~1) + ((a >> 10) & 1);
        else                 bank = (s->banks[1] & ~1) + ((a >> 10) & 1);
    }
    bank %= banks;
    return m->cart->chr_rom[bank * 1024 + (a & 0x3FF)];
}
static void m4_chr_write(Mapper *m, uint16_t a, uint8_t v) {
    if (m->cart->has_chr_ram) m->cart->chr_ram[a & 0x1FFF] = v;
}
static void m4_step(Mapper *m) {
    /* Called each scanline by PPU */
    MMC3 *s = (MMC3 *)m->data;
    if (s->irq_counter == 0 || s->irq_reload) {
        s->irq_counter = s->irq_latch;
        s->irq_reload = 0;
    } else {
        s->irq_counter--;
    }
    if (s->irq_counter == 0 && s->irq_enable) {
        cpu_irq(&m->nes->cpu);
    }
}
static void m4_reset(Mapper *m) {
    MMC3 *s = (MMC3 *)m->data;
    s->banks[6] = 0; s->banks[7] = 1;
}

/* ===== MAPPER 7: AxROM ===== */
typedef struct { uint8_t bank; } AxROM;
static uint8_t m7_prg_read(Mapper *m, uint16_t a) {
    if (a >= 0x8000) {
        int bank = ((AxROM *)m->data)->bank;
        return m->cart->prg_rom[bank * 32768 + (a & 0x7FFF)];
    }
    return 0;
}
static void m7_prg_write(Mapper *m, uint16_t a, uint8_t v) {
    if (a >= 0x8000) {
        AxROM *s = (AxROM *)m->data;
        s->bank = v & 7 & (m->cart->prg_size / 2 - 1);
        m->cart->mirror = (v & 0x10) ? MIRROR_SINGLE1 : MIRROR_SINGLE0;
    }
}

/* ===== MAPPER 9: MMC2 (Punch-Out!!) ===== */
typedef struct {
    uint8_t prg_bank;
    uint8_t chr_bank[2][2]; /* [latch][0/1] */
    uint8_t latch[2];
} MMC2;
static uint8_t m9_prg_read(Mapper *m, uint16_t a) {
    MMC2 *s = (MMC2 *)m->data;
    if (a >= 0x8000) {
        int off = a & 0x1FFF;
        int banks = m->cart->prg_size * 2;
        int bank;
        if      (a < 0xA000) bank = s->prg_bank & (banks - 1);
        else if (a < 0xC000) bank = banks - 3;
        else if (a < 0xE000) bank = banks - 2;
        else                 bank = banks - 1;
        return m->cart->prg_rom[bank * 8192 + off];
    }
    return 0;
}
static void m9_prg_write(Mapper *m, uint16_t a, uint8_t v) {
    MMC2 *s = (MMC2 *)m->data;
    switch (a & 0xF000) {
    case 0xA000: s->prg_bank = v & 0x0F; break;
    case 0xB000: s->chr_bank[0][0] = v & 0x1F; break;
    case 0xC000: s->chr_bank[0][1] = v & 0x1F; break;
    case 0xD000: s->chr_bank[1][0] = v & 0x1F; break;
    case 0xE000: s->chr_bank[1][1] = v & 0x1F; break;
    case 0xF000: m->cart->mirror = (v & 1) ? MIRROR_HORIZ : MIRROR_VERT; break;
    }
}
static uint8_t m9_chr_read(Mapper *m, uint16_t a) {
    MMC2 *s = (MMC2 *)m->data;
    int half = (a >= 0x1000) ? 1 : 0;
    int bank = s->chr_bank[half][s->latch[half]];
    uint8_t v = m->cart->chr_rom[(bank * 4096 + (a & 0x0FFF)) % (m->cart->chr_size * 8192)];
    /* Update latch */
    if      (a == 0x0FD8) s->latch[0] = 0;
    else if (a == 0x0FE8) s->latch[0] = 1;
    else if (a >= 0x1FD8 && a <= 0x1FDF) s->latch[1] = 0;
    else if (a >= 0x1FE8 && a <= 0x1FEF) s->latch[1] = 1;
    return v;
}

/* ===== MAPPER 11: Color Dreams ===== */
typedef struct { uint8_t prg, chr; } ColorDreams;
static uint8_t m11_prg_read(Mapper *m, uint16_t a) {
    if (a >= 0x8000) {
        int bank = ((ColorDreams *)m->data)->prg & (m->cart->prg_size / 2 - 1);
        return m->cart->prg_rom[bank * 32768 + (a & 0x7FFF)];
    }
    return 0;
}
static void m11_prg_write(Mapper *m, uint16_t a, uint8_t v) {
    if (a >= 0x8000) {
        ColorDreams *s = (ColorDreams *)m->data;
        s->prg = v & 0x03;
        s->chr = (v >> 4) & 0x0F;
    }
}
static uint8_t m11_chr_read(Mapper *m, uint16_t a) {
    int bank = ((ColorDreams *)m->data)->chr & (m->cart->chr_size - 1);
    return m->cart->chr_rom[bank * 8192 + (a & 0x1FFF)];
}

/* ===== MAPPER 66: GxROM ===== */
typedef struct { uint8_t prg, chr; } GxROM;
static uint8_t m66_prg_read(Mapper *m, uint16_t a) {
    if (a >= 0x8000) {
        int bank = ((GxROM *)m->data)->prg & (m->cart->prg_size / 2 - 1);
        return m->cart->prg_rom[bank * 32768 + (a & 0x7FFF)];
    }
    return 0;
}
static void m66_prg_write(Mapper *m, uint16_t a, uint8_t v) {
    if (a >= 0x8000) {
        GxROM *s = (GxROM *)m->data;
        s->prg = (v >> 4) & 3;
        s->chr = v & 3;
    }
}
static uint8_t m66_chr_read(Mapper *m, uint16_t a) {
    int bank = ((GxROM *)m->data)->chr & (m->cart->chr_size - 1);
    return m->cart->chr_rom[bank * 8192 + (a & 0x1FFF)];
}

/* ===== MAPPER 71: Camerica/Codemasters ===== */
typedef struct { uint8_t bank; } Camerica;
static uint8_t m71_prg_read(Mapper *m, uint16_t a) {
    if (a >= 0xC000) return m->cart->prg_rom[(m->cart->prg_size - 1) * 16384 + (a & 0x3FFF)];
    if (a >= 0x8000) return m->cart->prg_rom[((Camerica *)m->data)->bank * 16384 + (a & 0x3FFF)];
    return 0;
}
static void m71_prg_write(Mapper *m, uint16_t a, uint8_t v) {
    if (a >= 0x8000) ((Camerica *)m->data)->bank = v & (m->cart->prg_size - 1);
}

/* ===== Generic helpers ===== */
static uint8_t generic_prg_read(Mapper *m, uint16_t a) {
    if (a >= 0x8000) {
        return m->cart->prg_rom[(a - 0x8000) & (m->cart->prg_size * 16384 - 1)];
    }
    return 0;
}
static void noop_write(Mapper *m, uint16_t a, uint8_t v) { (void)m;(void)a;(void)v; }
static void noop_step(Mapper *m) { (void)m; }
static void noop_reset(Mapper *m) { (void)m; }
static void noop_free(Mapper *m) { free(m->data); free(m); }

/* ===== Factory ===== */
Mapper *mapper_create(int id, Cart *cart, NES *nes) {
    Mapper *m = (Mapper *)calloc(1, sizeof(Mapper));
    m->id   = id;
    m->cart = cart;
    m->nes  = nes;
    /* common defaults */
    m->chr_write = m0_chr_write;
    m->step  = noop_step;
    m->reset = noop_reset;
    m->free  = noop_free;

    switch (id) {
    case 0:
        m->prg_read  = m0_prg_read;
        m->prg_write = m0_prg_write;
        m->chr_read  = m0_chr_read;
        m->chr_write = m0_chr_write;
        break;
    case 1: {
        MMC1 *s = (MMC1 *)calloc(1, sizeof(MMC1));
        m->data = s; s->ctrl = 0x0C;
        m->prg_read  = m1_prg_read;
        m->prg_write = m1_prg_write;
        m->chr_read  = m1_chr_read;
        m->chr_write = m1_chr_write;
        m->reset     = m1_reset;
        m1_reset(m);
        break; }
    case 2: {
        UxROM *s = (UxROM *)calloc(1, sizeof(UxROM));
        m->data = s;
        m->prg_read  = m2_prg_read;
        m->prg_write = m2_prg_write;
        m->chr_read  = m0_chr_read;
        break; }
    case 3: {
        CNROM *s = (CNROM *)calloc(1, sizeof(CNROM));
        m->data = s;
        m->prg_read  = m0_prg_read;
        m->prg_write = m3_prg_write;
        m->chr_read  = m3_chr_read;
        break; }
    case 4: {
        MMC3 *s = (MMC3 *)calloc(1, sizeof(MMC3));
        m->data = s;
        m->prg_read  = m4_prg_read;
        m->prg_write = m4_prg_write;
        m->chr_read  = m4_chr_read;
        m->chr_write = m4_chr_write;
        m->step      = m4_step;
        m->reset     = m4_reset;
        m4_reset(m);
        break; }
    case 7: {
        AxROM *s = (AxROM *)calloc(1, sizeof(AxROM));
        m->data = s;
        m->prg_read  = m7_prg_read;
        m->prg_write = m7_prg_write;
        m->chr_read  = m0_chr_read;
        break; }
    case 9: {
        MMC2 *s = (MMC2 *)calloc(1, sizeof(MMC2));
        m->data = s;
        m->prg_read  = m9_prg_read;
        m->prg_write = m9_prg_write;
        m->chr_read  = m9_chr_read;
        break; }
    case 11: {
        ColorDreams *s = (ColorDreams *)calloc(1, sizeof(ColorDreams));
        m->data = s;
        m->prg_read  = m11_prg_read;
        m->prg_write = m11_prg_write;
        m->chr_read  = m11_chr_read;
        break; }
    case 66: {
        GxROM *s = (GxROM *)calloc(1, sizeof(GxROM));
        m->data = s;
        m->prg_read  = m66_prg_read;
        m->prg_write = m66_prg_write;
        m->chr_read  = m66_chr_read;
        break; }
    case 71: {
        Camerica *s = (Camerica *)calloc(1, sizeof(Camerica));
        m->data = s;
        m->prg_read  = m71_prg_read;
        m->prg_write = m71_prg_write;
        m->chr_read  = m0_chr_read;
        break; }
    default:
        fprintf(stderr, "Unsupported mapper %d, falling back to mapper 0\n", id);
        m->prg_read  = m0_prg_read;
        m->prg_write = m0_prg_write;
        m->chr_read  = m0_chr_read;
        m->chr_write = m0_chr_write;
        break;
    }
    return m;
}

void mapper_destroy(Mapper *m) {
    if (m) m->free(m);
}
