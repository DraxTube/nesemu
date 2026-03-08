#ifndef CPU_H
#define CPU_H

#include <stdint.h>

/* 6502 Status flags */
#define FLAG_C  0x01   /* Carry */
#define FLAG_Z  0x02   /* Zero */
#define FLAG_I  0x04   /* IRQ disable */
#define FLAG_D  0x08   /* Decimal (unused on NES) */
#define FLAG_B  0x10   /* Break */
#define FLAG_U  0x20   /* Unused (always 1) */
#define FLAG_V  0x40   /* Overflow */
#define FLAG_N  0x80   /* Negative */

typedef struct NES NES;

typedef struct CPU {
    uint8_t  A;          /* Accumulator */
    uint8_t  X;          /* Index X */
    uint8_t  Y;          /* Index Y */
    uint8_t  SP;         /* Stack Pointer */
    uint16_t PC;         /* Program Counter */
    uint8_t  P;          /* Processor Status */

    int      cycles;     /* Remaining cycles */
    int      total_cycles;

    uint8_t  nmi_pending;
    uint8_t  irq_pending;
    uint8_t  rst_pending;

    NES     *nes;
} CPU;

void cpu_init(CPU *cpu, NES *nes);
void cpu_reset(CPU *cpu);
int  cpu_step(CPU *cpu);
void cpu_nmi(CPU *cpu);
void cpu_irq(CPU *cpu);

/* Memory access (goes through NES bus) */
uint8_t  cpu_read(CPU *cpu, uint16_t addr);
void     cpu_write(CPU *cpu, uint16_t addr, uint8_t val);

#endif /* CPU_H */
