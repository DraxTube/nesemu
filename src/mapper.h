#ifndef MAPPER_H
#define MAPPER_H

#include <stdint.h>
#include "cart.h"

typedef struct NES NES;

typedef struct Mapper {
    int      id;
    void    *data;      /* mapper-specific state */
    Cart    *cart;
    NES     *nes;

    uint8_t (*prg_read)(struct Mapper *, uint16_t addr);
    void    (*prg_write)(struct Mapper *, uint16_t addr, uint8_t val);
    uint8_t (*chr_read)(struct Mapper *, uint16_t addr);
    void    (*chr_write)(struct Mapper *, uint16_t addr, uint8_t val);
    void    (*reset)(struct Mapper *);
    void    (*step)(struct Mapper *);   /* called each scanline for IRQ mappers */
    void    (*free)(struct Mapper *);
} Mapper;

Mapper *mapper_create(int id, Cart *cart, NES *nes);
void    mapper_destroy(Mapper *m);

/* NT mirroring helper */
uint16_t mapper_nt_mirror(int mirror, uint16_t addr);

#endif /* MAPPER_H */
