/*
 * iNES / NES 2.0 cartridge loader
 */
#include "cart.h"
#include "mapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cart_load(Cart *cart, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open ROM: %s\n", path); return -1; }

    uint8_t header[16];
    if (fread(header, 1, 16, f) != 16) { fclose(f); return -1; }

    /* Check magic */
    if (memcmp(header, "NES\x1A", 4) != 0) {
        fprintf(stderr, "Not a valid iNES ROM\n");
        fclose(f); return -1;
    }

    int prg_banks = header[4];
    int chr_banks = header[5];
    int flags6    = header[6];
    int flags7    = header[7];

    /* Detect NES 2.0 */
    int nes2 = ((flags7 & 0x0C) == 0x08);

    int mapper_id = ((flags6 >> 4) & 0x0F) | (flags7 & 0xF0);
    if (nes2) mapper_id |= ((header[8] & 0x0F) << 8);

    int mirror   = (flags6 & 0x01) ? MIRROR_VERT : MIRROR_HORIZ;
    if (flags6 & 0x08) mirror = MIRROR_4SCREEN;

    int battery  = (flags6 & 0x02) ? 1 : 0;
    int trainer  = (flags6 & 0x04) ? 1 : 0;

    memset(cart, 0, sizeof(Cart));
    cart->prg_size = prg_banks;
    cart->chr_size = chr_banks;
    cart->mapper_id = mapper_id;
    cart->mirror = mirror;
    cart->battery = battery;

    /* Skip trainer */
    if (trainer) fseek(f, 512, SEEK_CUR);

    /* Load PRG-ROM */
    int prg_bytes = prg_banks * 16384;
    cart->prg_rom = (uint8_t *)malloc(prg_bytes);
    if (!cart->prg_rom || fread(cart->prg_rom, 1, prg_bytes, f) != (size_t)prg_bytes) {
        fprintf(stderr, "Failed to read PRG-ROM\n");
        fclose(f); return -1;
    }

    /* Load CHR-ROM */
    if (chr_banks > 0) {
        int chr_bytes = chr_banks * 8192;
        cart->chr_rom = (uint8_t *)malloc(chr_bytes);
        if (!cart->chr_rom || fread(cart->chr_rom, 1, chr_bytes, f) != (size_t)chr_bytes) {
            fprintf(stderr, "Failed to read CHR-ROM\n");
            fclose(f); return -1;
        }
        cart->has_chr_ram = 0;
    } else {
        /* CHR-RAM */
        cart->chr_rom = cart->chr_ram;
        cart->chr_size = 1;
        cart->has_chr_ram = 1;
        memset(cart->chr_ram, 0, sizeof(cart->chr_ram));
    }

    cart->has_prg_ram = 1;
    memset(cart->prg_ram, 0, sizeof(cart->prg_ram));

    fclose(f);

    fprintf(stderr, "ROM loaded: mapper=%d prg=%d chr=%d mirror=%d\n",
            mapper_id, prg_banks, chr_banks, mirror);
    return 0;
}

void cart_free(Cart *cart) {
    if (cart->prg_rom) free(cart->prg_rom);
    if (!cart->has_chr_ram && cart->chr_rom) free(cart->chr_rom);
    if (cart->mapper) mapper_destroy(cart->mapper);
    memset(cart, 0, sizeof(Cart));
}
