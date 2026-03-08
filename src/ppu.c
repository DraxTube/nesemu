/*
 * NES PPU (Picture Processing Unit) emulator
 * Implements accurate scanline-based rendering
 */
#include "ppu.h"
#include "nes.h"
#include <string.h>

/* NES standard palette (RGBA) */
const uint32_t nes_palette[64] = {
    0x626262FF, 0x012094FF, 0x1C0BABFF, 0x3C0898FF,
    0x560060FF, 0x5E0028FF, 0x570500FF, 0x410D00FF,
    0x211B00FF, 0x002700FF, 0x002F00FF, 0x002A00FF,
    0x001D3DFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xABABABFF, 0x1355DAFF, 0x4337F5FF, 0x7425E4FF,
    0xA01AADFF, 0xAA1B5FFF, 0xA42000FF, 0x843400FF,
    0x544500FF, 0x215600FF, 0x005E00FF, 0x005900FF,
    0x00487BFF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xFFFFFFFF, 0x4EA0FFFF, 0x7E83FFFF, 0xB266FFFF,
    0xE458FFFF, 0xFF55CEFF, 0xFF5F73FF, 0xFF7021FF,
    0xD28600FF, 0x9A9600FF, 0x63A300FF, 0x359E1EFF,
    0x068F73FF, 0x2D2D2DFF, 0x000000FF, 0x000000FF,
    0xFFFFFFFF, 0xB0D4FFFF, 0xC3C7FFFF, 0xD9B8FFFF,
    0xEDB3FFFF, 0xFFB2F2FF, 0xFFB9C5FF, 0xFFC593FF,
    0xEDD178FF, 0xD8DC71FF, 0xC0E493FF, 0xAEDEB0FF,
    0x9FD8CDFF, 0xA6A6A6FF, 0x000000FF, 0x000000FF,
};

/* ---- PPU memory map ---- */
static uint8_t ppu_rd(PPU *p, uint16_t a) {
    a &= 0x3FFF;
    if (a < 0x2000) return nes_ppu_chr_read(p->nes, a);
    if (a < 0x3F00) {
        a = nes_ppu_nt_mirror(p->nes, a) & 0x7FF;
        return p->vram[a];
    }
    /* Palette */
    a &= 0x1F;
    if (a == 0x10 || a == 0x14 || a == 0x18 || a == 0x1C) a &= 0x0F;
    uint8_t v = p->pal[a] & ((p->mask & 0x01) ? 0x30 : 0x3F);
    return v;
}

static void ppu_wr(PPU *p, uint16_t a, uint8_t v) {
    a &= 0x3FFF;
    if (a < 0x2000) { nes_ppu_chr_write(p->nes, a, v); return; }
    if (a < 0x3F00) {
        p->vram[nes_ppu_nt_mirror(p->nes, a) & 0x7FF] = v;
        return;
    }
    a &= 0x1F;
    if (a == 0x10 || a == 0x14 || a == 0x18 || a == 0x1C) a &= 0x0F;
    p->pal[a] = v & 0x3F;
}

/* ---- VRAM address helpers ---- */
#define V_COARSE_X(v)    ((v) & 0x001F)
#define V_COARSE_Y(v)   (((v) >> 5) & 0x001F)
#define V_NT(v)         (((v) >> 10) & 0x3)
#define V_FINE_Y(v)     (((v) >> 12) & 0x7)

static void inc_hori(PPU *p) {
    if ((p->v & 0x001F) == 31) { p->v &= ~0x001F; p->v ^= 0x0400; }
    else p->v++;
}
static void inc_vert(PPU *p) {
    if ((p->v & 0x7000) != 0x7000) { p->v += 0x1000; }
    else {
        p->v &= ~0x7000;
        int y = V_COARSE_Y(p->v);
        if (y == 29) { y = 0; p->v ^= 0x0800; }
        else if (y == 31) y = 0;
        else y++;
        p->v = (p->v & ~0x03E0) | (y << 5);
    }
}
static void copy_hori(PPU *p) { p->v = (p->v & ~0x041F) | (p->t & 0x041F); }
static void copy_vert(PPU *p) { p->v = (p->v & ~0x7BE0) | (p->t & 0x7BE0); }

static int rendering_enabled(PPU *p) { return (p->mask & 0x18) != 0; }

/* ---- Tile fetch helpers ---- */
static void fetch_nt(PPU *p) {
    p->bg_nt = ppu_rd(p, 0x2000 | (p->v & 0x0FFF));
}
static void fetch_attr(PPU *p) {
    uint16_t at_addr = 0x23C0 | (p->v & 0x0C00)
                      | ((p->v >> 4) & 0x38) | ((p->v >> 2) & 0x07);
    uint8_t at = ppu_rd(p, at_addr);
    if (p->v & 0x40) at >>= 4;
    if (p->v & 0x02) at >>= 2;
    p->bg_attr = at & 0x03;
}
static void fetch_lo(PPU *p) {
    uint16_t base = (p->ctrl & 0x10) ? 0x1000 : 0x0000;
    p->bg_lo = ppu_rd(p, base + p->bg_nt * 16 + V_FINE_Y(p->v));
}
static void fetch_hi(PPU *p) {
    uint16_t base = (p->ctrl & 0x10) ? 0x1000 : 0x0000;
    p->bg_hi = ppu_rd(p, base + p->bg_nt * 16 + V_FINE_Y(p->v) + 8);
}
static void load_shifters(PPU *p) {
    p->bg_shift_lo = (p->bg_shift_lo & 0xFF00) | p->bg_lo;
    p->bg_shift_hi = (p->bg_shift_hi & 0xFF00) | p->bg_hi;
    p->bg_attr_lo  = (p->bg_attr & 1) ? 0xFF : 0x00;
    p->bg_attr_hi  = (p->bg_attr & 2) ? 0xFF : 0x00;
}
static void update_shifters(PPU *p) {
    if (p->mask & 0x08) {
        p->bg_shift_lo      <<= 1;
        p->bg_shift_hi      <<= 1;
        p->bg_attr_shift_lo  = (p->bg_attr_shift_lo << 1) | (p->bg_attr_lo & 1);
        p->bg_attr_shift_hi  = (p->bg_attr_shift_hi << 1) | (p->bg_attr_hi & 1);
    }
}

/* ---- Sprite evaluation for next scanline ---- */
static void evaluate_sprites(PPU *p) {
    p->sprite_count = 0;
    p->sprite0_hit_possible = 0;
    int spr_size = (p->ctrl & 0x20) ? 16 : 8;

    for (int i = 0; i < 64 && p->sprite_count < 8; i++) {
        int diff = p->scanline - (int)p->oam[i * 4];
        if (diff >= 0 && diff < spr_size) {
            if (i == 0) p->sprite0_hit_possible = 1;
            p->sprites[p->sprite_count].y    = p->oam[i*4 + 0];
            p->sprites[p->sprite_count].tile = p->oam[i*4 + 1];
            p->sprites[p->sprite_count].attr = p->oam[i*4 + 2];
            p->sprites[p->sprite_count].x    = p->oam[i*4 + 3];
            p->sprite_count++;
        }
    }
    if (p->sprite_count == 8) p->status |= 0x20; /* sprite overflow */
}

static void fetch_sprites(PPU *p) {
    int spr_size = (p->ctrl & 0x20) ? 16 : 8;
    uint16_t spr_base = (p->ctrl & 0x08) ? 0x1000 : 0x0000;

    for (int i = 0; i < p->sprite_count; i++) {
        int row = p->scanline - p->sprites[i].y;
        if (p->sprites[i].attr & 0x80) row = spr_size - 1 - row; /* flip V */

        uint16_t addr;
        if (spr_size == 8) {
            addr = spr_base + p->sprites[i].tile * 16 + row;
        } else {
            /* 8x16 sprite */
            uint16_t bank = (p->sprites[i].tile & 1) ? 0x1000 : 0x0000;
            uint8_t  tile = p->sprites[i].tile & 0xFE;
            if (row >= 8) { tile++; row -= 8; }
            addr = bank + tile * 16 + row;
        }
        p->sprite_lo[i] = ppu_rd(p, addr);
        p->sprite_hi[i] = ppu_rd(p, addr + 8);
        if (p->sprites[i].attr & 0x40) {
            /* flip H */
            uint8_t lo = p->sprite_lo[i], hi = p->sprite_hi[i];
            lo = ((lo * 0x0202020202ULL & 0x010884422010ULL) % 1023) & 0xFF;
            hi = ((hi * 0x0202020202ULL & 0x010884422010ULL) % 1023) & 0xFF;
            p->sprite_lo[i] = lo;
            p->sprite_hi[i] = hi;
        }
    }
}

/* ---- Get pixel ---- */
static void render_pixel(PPU *p) {
    int x = p->dot - 1;
    int y = p->scanline;
    if (x < 0 || x >= 256 || y < 0 || y >= 240) return;

    /* Background */
    uint8_t bg_pixel = 0, bg_pal = 0;
    if ((p->mask & 0x08) && (x >= 8 || (p->mask & 0x02))) {
        uint8_t bit = 0x80 >> p->x;
        bg_pixel  = ((p->bg_shift_lo & (bit << 8)) ? 2 : 0)
                  | ((p->bg_shift_hi & (bit << 8)) ? 1 : 0);
        bg_pal    = ((p->bg_attr_shift_lo & bit) ? 2 : 0)
                  | ((p->bg_attr_shift_hi & bit) ? 1 : 0);
    }

    /* Sprite */
    uint8_t spr_pixel = 0, spr_pal = 0, spr_priority = 0, spr_idx = 0;
    if ((p->mask & 0x10) && (x >= 8 || (p->mask & 0x04))) {
        for (int i = 0; i < p->sprite_count; i++) {
            int sx = x - p->sprites[i].x;
            if (sx < 0 || sx >= 8) continue;
            uint8_t lo = (p->sprite_lo[i] >> (7 - sx)) & 1;
            uint8_t hi = (p->sprite_hi[i] >> (7 - sx)) & 1;
            uint8_t px = (hi << 1) | lo;
            if (px == 0) continue;
            spr_pixel    = px;
            spr_pal      = (p->sprites[i].attr & 0x03) + 4;
            spr_priority = (p->sprites[i].attr >> 5) & 1;
            spr_idx      = i;
            break;
        }
    }

    /* Sprite 0 hit */
    if (p->sprite0_hit_possible && spr_idx == 0 && bg_pixel && spr_pixel
        && x >= 0 && x != 255 && (p->mask & 0x18) == 0x18) {
        p->status |= 0x40;
    }

    /* Compose */
    uint8_t pal_idx;
    if (!bg_pixel && !spr_pixel) {
        pal_idx = 0;
    } else if (!bg_pixel) {
        pal_idx = spr_pal * 4 + spr_pixel;
    } else if (!spr_pixel) {
        pal_idx = bg_pal * 4 + bg_pixel;
    } else {
        pal_idx = spr_priority ? (bg_pal * 4 + bg_pixel) : (spr_pal * 4 + spr_pixel);
    }

    p->framebuf[y * 256 + x] = nes_palette[ppu_rd(p, 0x3F00 + pal_idx) & 0x3F];
}

/* ---- Main PPU step (1 clock) ---- */
void ppu_step(PPU *p) {
    int sl  = p->scanline;
    int dot = p->dot;

    if (sl >= 0 && sl < 240) {
        /* Visible scanlines */
        if (dot == 0) {
            /* idle */
        } else if (dot >= 1 && dot <= 256) {
            update_shifters(p);
            switch ((dot - 1) & 7) {
            case 0: fetch_nt(p); break;
            case 2: fetch_attr(p); break;
            case 4: fetch_lo(p); break;
            case 6: fetch_hi(p); break;
            case 7: load_shifters(p); if (rendering_enabled(p)) inc_hori(p); break;
            }
            render_pixel(p);
        } else if (dot == 256) {
            if (rendering_enabled(p)) inc_vert(p);
        } else if (dot == 257) {
            load_shifters(p);
            if (rendering_enabled(p)) copy_hori(p);
            if (sl >= 0) evaluate_sprites(p);
        } else if (dot >= 321 && dot <= 336) {
            update_shifters(p);
            switch ((dot - 321) & 7) {
            case 0: fetch_nt(p); break;
            case 2: fetch_attr(p); break;
            case 4: fetch_lo(p); break;
            case 6: fetch_hi(p); break;
            case 7: load_shifters(p); if (rendering_enabled(p)) inc_hori(p); break;
            }
        }
        if (dot == 340) {
            fetch_sprites(p);
        }
    } else if (sl == 241 && dot == 1) {
        /* VBlank start */
        p->status |= 0x80;
        if (p->ctrl & 0x80) {
            cpu_nmi(&p->nes->cpu);
        }
    } else if (sl == 261) {
        /* Pre-render scanline */
        if (dot == 1) {
            p->status &= ~0xE0; /* clear VBL, spr0, overflow */
        }
        if (dot >= 280 && dot <= 304 && rendering_enabled(p)) {
            copy_vert(p);
        }
        if (dot >= 1 && dot <= 256) {
            update_shifters(p);
            switch ((dot - 1) & 7) {
            case 0: fetch_nt(p); break;
            case 2: fetch_attr(p); break;
            case 4: fetch_lo(p); break;
            case 6: fetch_hi(p); break;
            case 7: load_shifters(p); if (rendering_enabled(p)) inc_hori(p); break;
            }
        } else if (dot == 256 && rendering_enabled(p)) {
            inc_vert(p);
        } else if (dot == 257 && rendering_enabled(p)) {
            copy_hori(p);
        } else if (dot >= 321 && dot <= 336) {
            update_shifters(p);
            switch ((dot - 321) & 7) {
            case 0: fetch_nt(p); break;
            case 2: fetch_attr(p); break;
            case 4: fetch_lo(p); break;
            case 6: fetch_hi(p); break;
            case 7: load_shifters(p); if (rendering_enabled(p)) inc_hori(p); break;
            }
        }
        if (dot == 340) {
            fetch_sprites(p);
        }
    }

    /* Advance dot/scanline */
    p->dot++;
    if (p->dot > 340) {
        p->dot = 0;
        p->scanline++;
        if (p->scanline > 261) {
            p->scanline = 0;
            p->frame++;
            /* Skip dot on odd frames when rendering */
            if ((p->frame & 1) && rendering_enabled(p)) p->dot = 1;
        }
    }
}

/* ---- Register access ---- */
uint8_t ppu_read_reg(PPU *p, uint8_t reg) {
    uint8_t val = 0;
    switch (reg) {
    case 2: /* PPUSTATUS */
        val = (p->status & 0xE0) | (p->data_buf & 0x1F);
        p->status &= ~0x80;
        p->w = 0;
        break;
    case 4: /* OAMDATA */
        val = p->oam[p->oam_addr];
        break;
    case 7: /* PPUDATA */
        val = p->data_buf;
        p->data_buf = ppu_rd(p, p->v);
        if ((p->v & 0x3FFF) >= 0x3F00) val = p->data_buf;
        p->v += (p->ctrl & 0x04) ? 32 : 1;
        break;
    }
    return val;
}

void ppu_write_reg(PPU *p, uint8_t reg, uint8_t val) {
    switch (reg) {
    case 0: /* PPUCTRL */
        p->ctrl = val;
        p->t = (p->t & 0xF3FF) | ((val & 0x03) << 10);
        break;
    case 1: /* PPUMASK */
        p->mask = val;
        break;
    case 3: /* OAMADDR */
        p->oam_addr = val;
        break;
    case 4: /* OAMDATA */
        p->oam[p->oam_addr++] = val;
        break;
    case 5: /* PPUSCROLL */
        if (!p->w) {
            p->t = (p->t & 0xFFE0) | (val >> 3);
            p->x = val & 0x07;
            p->w = 1;
        } else {
            p->t = (p->t & 0x8FFF) | ((val & 0x07) << 12);
            p->t = (p->t & 0xFC1F) | ((val & 0xF8) << 2);
            p->w = 0;
        }
        break;
    case 6: /* PPUADDR */
        if (!p->w) {
            p->t = (p->t & 0x00FF) | ((val & 0x3F) << 8);
            p->w = 1;
        } else {
            p->t = (p->t & 0xFF00) | val;
            p->v = p->t;
            p->w = 0;
        }
        break;
    case 7: /* PPUDATA */
        ppu_wr(p, p->v, val);
        p->v += (p->ctrl & 0x04) ? 32 : 1;
        break;
    }
}

void ppu_oam_dma(PPU *p, uint8_t *page) {
    memcpy(p->oam, page, 256);
}

void ppu_init(PPU *p, NES *nes) {
    memset(p, 0, sizeof(PPU));
    p->nes = nes;
}

void ppu_reset(PPU *p) {
    p->ctrl = 0;
    p->mask = 0;
    p->status = 0;
    p->v = p->t = 0;
    p->x = p->w = 0;
    p->scanline = 0;
    p->dot = 0;
    p->frame = 0;
}

uint8_t ppu_read(PPU *p, uint16_t addr) { return ppu_rd(p, addr); }
void    ppu_write(PPU *p, uint16_t addr, uint8_t val) { ppu_wr(p, addr, val); }
