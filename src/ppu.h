#ifndef PPU_H
#define PPU_H

#include <stdint.h>

#define PPU_WIDTH  256
#define PPU_HEIGHT 240

/* OAM sprite entry */
typedef struct {
    uint8_t y;
    uint8_t tile;
    uint8_t attr;
    uint8_t x;
} OAMEntry;

typedef struct NES NES;

typedef struct PPU {
    /* Registers exposed to CPU */
    uint8_t  ctrl;       /* $2000 PPUCTRL  */
    uint8_t  mask;       /* $2001 PPUMASK  */
    uint8_t  status;     /* $2002 PPUSTATUS*/
    uint8_t  oam_addr;   /* $2003 OAMADDR  */

    /* Internal state */
    uint16_t v;          /* Current VRAM address (15-bit) */
    uint16_t t;          /* Temporary VRAM address (15-bit) */
    uint8_t  x;          /* Fine X scroll (3-bit) */
    uint8_t  w;          /* Write toggle */
    uint8_t  data_buf;   /* PPUDATA read buffer */

    /* Scanline / cycle counters */
    int      scanline;
    int      dot;
    int      frame;
    int      nmi_occurred;
    int      nmi_output;

    /* Memory */
    uint8_t  oam[256];
    uint8_t  pal[32];
    uint8_t  vram[2048];   /* 2 nametables */

    /* Rendering pipeline */
    uint8_t  bg_nt;
    uint8_t  bg_attr;
    uint8_t  bg_lo;
    uint8_t  bg_hi;
    uint16_t bg_shift_lo;
    uint16_t bg_shift_hi;
    uint8_t  bg_attr_lo;
    uint8_t  bg_attr_hi;
    uint8_t  bg_attr_shift_lo;
    uint8_t  bg_attr_shift_hi;

    /* Sprite rendering */
    OAMEntry sprites[8];
    uint8_t  sprite_count;
    uint8_t  sprite_lo[8];
    uint8_t  sprite_hi[8];
    uint8_t  sprite0_hit_possible;

    /* Frame buffer (RGBA) */
    uint32_t framebuf[PPU_WIDTH * PPU_HEIGHT];

    NES *nes;
} PPU;

void     ppu_init(PPU *ppu, NES *nes);
void     ppu_reset(PPU *ppu);
void     ppu_step(PPU *ppu);   /* Execute 1 PPU clock */

uint8_t  ppu_read_reg(PPU *ppu, uint8_t reg);
void     ppu_write_reg(PPU *ppu, uint8_t reg, uint8_t val);
void     ppu_oam_dma(PPU *ppu, uint8_t *page);

uint8_t  ppu_read(PPU *ppu, uint16_t addr);
void     ppu_write(PPU *ppu, uint16_t addr, uint8_t val);

/* NES palette as RGBA */
extern const uint32_t nes_palette[64];

#endif /* PPU_H */
