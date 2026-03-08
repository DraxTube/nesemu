#ifndef CART_H
#define CART_H

#include <stdint.h>

#define MIRROR_HORIZ    0
#define MIRROR_VERT     1
#define MIRROR_SINGLE0  2
#define MIRROR_SINGLE1  3
#define MIRROR_4SCREEN  4

typedef struct Mapper Mapper;

typedef struct Cart {
    uint8_t *prg_rom;
    int      prg_size;   /* in 16KB banks */
    uint8_t *chr_rom;
    int      chr_size;   /* in 8KB banks */
    uint8_t  chr_ram[8192];
    int      has_chr_ram;
    uint8_t  prg_ram[8192];
    int      has_prg_ram;
    int      mapper_id;
    int      mirror;
    int      battery;
    Mapper  *mapper;
} Cart;

int  cart_load(Cart *cart, const char *path);
void cart_free(Cart *cart);

#endif /* CART_H */
