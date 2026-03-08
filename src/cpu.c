/*
 * NES 6502 CPU emulator
 * Implements all official + most common undocumented opcodes
 */
#include "cpu.h"
#include "nes.h"
#include <string.h>

/* ---- Stack helpers ---- */
#define STACK_BASE  0x0100
#define PUSH(cpu,v) cpu_write(cpu, STACK_BASE | (cpu)->SP--, (v))
#define POP(cpu)    cpu_read (cpu, STACK_BASE | ++(cpu)->SP)

/* ---- Flag helpers ---- */
#define SET_FLAG(cpu,f)     ((cpu)->P |=  (f))
#define CLR_FLAG(cpu,f)     ((cpu)->P &= ~(f))
#define GET_FLAG(cpu,f)     (!!((cpu)->P & (f)))
#define SET_NZ(cpu,v)       do { \
    if ((v) & 0x80) SET_FLAG(cpu, FLAG_N); else CLR_FLAG(cpu, FLAG_N); \
    if ((v) == 0)   SET_FLAG(cpu, FLAG_Z); else CLR_FLAG(cpu, FLAG_Z); \
} while(0)

/* ---- Addressing modes (return effective address) ---- */
static uint16_t addr_imm   (CPU *c) { return c->PC++; }
static uint16_t addr_zp    (CPU *c) { return cpu_read(c, c->PC++); }
static uint16_t addr_zpx   (CPU *c) { return (cpu_read(c, c->PC++) + c->X) & 0xFF; }
static uint16_t addr_zpy   (CPU *c) { return (cpu_read(c, c->PC++) + c->Y) & 0xFF; }
static uint16_t addr_abs   (CPU *c) {
    uint16_t lo = cpu_read(c, c->PC++);
    uint16_t hi = cpu_read(c, c->PC++);
    return (hi << 8) | lo;
}
static uint16_t addr_absx  (CPU *c, int *cross) {
    uint16_t base = addr_abs(c);
    uint16_t eff  = base + c->X;
    if (cross) *cross = ((base & 0xFF00) != (eff & 0xFF00));
    return eff;
}
static uint16_t addr_absy  (CPU *c, int *cross) {
    uint16_t base = addr_abs(c);
    uint16_t eff  = base + c->Y;
    if (cross) *cross = ((base & 0xFF00) != (eff & 0xFF00));
    return eff;
}
static uint16_t addr_ind   (CPU *c) {
    uint16_t ptr = addr_abs(c);
    uint16_t lo  = cpu_read(c, ptr);
    uint16_t hi  = cpu_read(c, (ptr & 0xFF00) | ((ptr + 1) & 0x00FF)); /* page wrap bug */
    return (hi << 8) | lo;
}
static uint16_t addr_indx  (CPU *c) {
    uint8_t  zp  = (cpu_read(c, c->PC++) + c->X) & 0xFF;
    uint16_t lo  = cpu_read(c, zp);
    uint16_t hi  = cpu_read(c, (zp + 1) & 0xFF);
    return (hi << 8) | lo;
}
static uint16_t addr_indy  (CPU *c, int *cross) {
    uint8_t  zp   = cpu_read(c, c->PC++);
    uint16_t lo   = cpu_read(c, zp);
    uint16_t hi   = cpu_read(c, (zp + 1) & 0xFF);
    uint16_t base = (hi << 8) | lo;
    uint16_t eff  = base + c->Y;
    if (cross) *cross = ((base & 0xFF00) != (eff & 0xFF00));
    return eff;
}

/* ---- ADC / SBC ---- */
static void do_adc(CPU *c, uint8_t val) {
    uint16_t res = c->A + val + GET_FLAG(c, FLAG_C);
    if (res & 0x100) SET_FLAG(c, FLAG_C); else CLR_FLAG(c, FLAG_C);
    if (~(c->A ^ val) & (c->A ^ res) & 0x80) SET_FLAG(c, FLAG_V); else CLR_FLAG(c, FLAG_V);
    c->A = res & 0xFF;
    SET_NZ(c, c->A);
}
static void do_sbc(CPU *c, uint8_t val) { do_adc(c, ~val); }

/* ---- Compare ---- */
static void do_cmp(CPU *c, uint8_t reg, uint8_t val) {
    uint16_t res = reg - val;
    if (reg >= val) SET_FLAG(c, FLAG_C); else CLR_FLAG(c, FLAG_C);
    SET_NZ(c, res & 0xFF);
}

/* ---- ASL / LSR / ROL / ROR ---- */
static uint8_t do_asl(CPU *c, uint8_t v) {
    if (v & 0x80) SET_FLAG(c, FLAG_C); else CLR_FLAG(c, FLAG_C);
    v <<= 1; SET_NZ(c, v); return v;
}
static uint8_t do_lsr(CPU *c, uint8_t v) {
    if (v & 0x01) SET_FLAG(c, FLAG_C); else CLR_FLAG(c, FLAG_C);
    v >>= 1; SET_NZ(c, v); return v;
}
static uint8_t do_rol(CPU *c, uint8_t v) {
    uint8_t carry = GET_FLAG(c, FLAG_C);
    if (v & 0x80) SET_FLAG(c, FLAG_C); else CLR_FLAG(c, FLAG_C);
    v = (v << 1) | carry; SET_NZ(c, v); return v;
}
static uint8_t do_ror(CPU *c, uint8_t v) {
    uint8_t carry = GET_FLAG(c, FLAG_C);
    if (v & 0x01) SET_FLAG(c, FLAG_C); else CLR_FLAG(c, FLAG_C);
    v = (v >> 1) | (carry << 7); SET_NZ(c, v); return v;
}

/* ---- BIT ---- */
static void do_bit(CPU *c, uint8_t v) {
    if (v & FLAG_N) SET_FLAG(c, FLAG_N); else CLR_FLAG(c, FLAG_N);
    if (v & FLAG_V) SET_FLAG(c, FLAG_V); else CLR_FLAG(c, FLAG_V);
    if (c->A & v)   CLR_FLAG(c, FLAG_Z); else SET_FLAG(c, FLAG_Z);
}

/* ---- Branch ---- */
static int do_branch(CPU *c, int cond) {
    int8_t off = (int8_t)cpu_read(c, c->PC++);
    if (cond) {
        uint16_t old = c->PC;
        c->PC += off;
        return ((old & 0xFF00) != (c->PC & 0xFF00)) ? 2 : 1;
    }
    return 0;
}

/* ---- NMI / IRQ / RESET vectors ---- */
#define VEC_NMI   0xFFFA
#define VEC_RESET 0xFFFC
#define VEC_IRQ   0xFFFE

static void do_nmi(CPU *c) {
    PUSH(c, c->PC >> 8);
    PUSH(c, c->PC & 0xFF);
    PUSH(c, (c->P | FLAG_U) & ~FLAG_B);
    SET_FLAG(c, FLAG_I);
    uint16_t lo = cpu_read(c, VEC_NMI);
    uint16_t hi = cpu_read(c, VEC_NMI + 1);
    c->PC = (hi << 8) | lo;
    c->cycles += 7;
}

static void do_irq(CPU *c) {
    if (GET_FLAG(c, FLAG_I)) return;
    PUSH(c, c->PC >> 8);
    PUSH(c, c->PC & 0xFF);
    PUSH(c, (c->P | FLAG_U) & ~FLAG_B);
    SET_FLAG(c, FLAG_I);
    uint16_t lo = cpu_read(c, VEC_IRQ);
    uint16_t hi = cpu_read(c, VEC_IRQ + 1);
    c->PC = (hi << 8) | lo;
    c->cycles += 7;
}

/* ---- Undocumented opcodes ---- */
static uint8_t do_slo(CPU *c, uint8_t v) { v = do_asl(c, v); c->A |= v; SET_NZ(c, c->A); return v; }
static uint8_t do_rla(CPU *c, uint8_t v) { v = do_rol(c, v); c->A &= v; SET_NZ(c, c->A); return v; }
static uint8_t do_sre(CPU *c, uint8_t v) { v = do_lsr(c, v); c->A ^= v; SET_NZ(c, c->A); return v; }
static uint8_t do_rra(CPU *c, uint8_t v) { v = do_ror(c, v); do_adc(c, v); return v; }
static uint8_t do_dcp(CPU *c, uint8_t v) { v--; do_cmp(c, c->A, v); return v; }
static uint8_t do_isc(CPU *c, uint8_t v) { v++; do_sbc(c, v); return v; }

void cpu_init(CPU *cpu, NES *nes) {
    memset(cpu, 0, sizeof(CPU));
    cpu->nes = nes;
    cpu->SP  = 0xFD;
    cpu->P   = FLAG_U | FLAG_I;
}

void cpu_reset(CPU *cpu) {
    uint16_t lo = cpu_read(cpu, VEC_RESET);
    uint16_t hi = cpu_read(cpu, VEC_RESET + 1);
    cpu->PC = (hi << 8) | lo;
    cpu->SP -= 3;
    SET_FLAG(cpu, FLAG_I);
    cpu->cycles = 7;
}

void cpu_nmi(CPU *cpu) { cpu->nmi_pending = 1; }
void cpu_irq(CPU *cpu) { cpu->irq_pending = 1; }

/* Main step function - returns cycles used */
int cpu_step(CPU *c) {
    /* Handle pending interrupts */
    if (c->nmi_pending) { c->nmi_pending = 0; do_nmi(c); }
    else if (c->irq_pending) { c->irq_pending = 0; do_irq(c); }

    uint8_t op = cpu_read(c, c->PC++);
    int cyc = 0;
    int cross = 0;
    uint16_t addr;
    uint8_t  val;

    switch (op) {
    /* ---- LDA ---- */
    case 0xA9: c->A = cpu_read(c, addr_imm(c));           SET_NZ(c,c->A); cyc=2; break;
    case 0xA5: c->A = cpu_read(c, addr_zp(c));            SET_NZ(c,c->A); cyc=3; break;
    case 0xB5: c->A = cpu_read(c, addr_zpx(c));           SET_NZ(c,c->A); cyc=4; break;
    case 0xAD: c->A = cpu_read(c, addr_abs(c));           SET_NZ(c,c->A); cyc=4; break;
    case 0xBD: c->A = cpu_read(c, addr_absx(c,&cross));   SET_NZ(c,c->A); cyc=4+cross; break;
    case 0xB9: c->A = cpu_read(c, addr_absy(c,&cross));   SET_NZ(c,c->A); cyc=4+cross; break;
    case 0xA1: c->A = cpu_read(c, addr_indx(c));          SET_NZ(c,c->A); cyc=6; break;
    case 0xB1: c->A = cpu_read(c, addr_indy(c,&cross));   SET_NZ(c,c->A); cyc=5+cross; break;
    /* ---- LDX ---- */
    case 0xA2: c->X = cpu_read(c, addr_imm(c));           SET_NZ(c,c->X); cyc=2; break;
    case 0xA6: c->X = cpu_read(c, addr_zp(c));            SET_NZ(c,c->X); cyc=3; break;
    case 0xB6: c->X = cpu_read(c, addr_zpy(c));           SET_NZ(c,c->X); cyc=4; break;
    case 0xAE: c->X = cpu_read(c, addr_abs(c));           SET_NZ(c,c->X); cyc=4; break;
    case 0xBE: c->X = cpu_read(c, addr_absy(c,&cross));   SET_NZ(c,c->X); cyc=4+cross; break;
    /* ---- LDY ---- */
    case 0xA0: c->Y = cpu_read(c, addr_imm(c));           SET_NZ(c,c->Y); cyc=2; break;
    case 0xA4: c->Y = cpu_read(c, addr_zp(c));            SET_NZ(c,c->Y); cyc=3; break;
    case 0xB4: c->Y = cpu_read(c, addr_zpx(c));           SET_NZ(c,c->Y); cyc=4; break;
    case 0xAC: c->Y = cpu_read(c, addr_abs(c));           SET_NZ(c,c->Y); cyc=4; break;
    case 0xBC: c->Y = cpu_read(c, addr_absx(c,&cross));   SET_NZ(c,c->Y); cyc=4+cross; break;
    /* ---- STA ---- */
    case 0x85: cpu_write(c, addr_zp(c),  c->A); cyc=3; break;
    case 0x95: cpu_write(c, addr_zpx(c), c->A); cyc=4; break;
    case 0x8D: cpu_write(c, addr_abs(c), c->A); cyc=4; break;
    case 0x9D: cpu_write(c, addr_absx(c,NULL), c->A); cyc=5; break;
    case 0x99: cpu_write(c, addr_absy(c,NULL), c->A); cyc=5; break;
    case 0x81: cpu_write(c, addr_indx(c), c->A); cyc=6; break;
    case 0x91: cpu_write(c, addr_indy(c,NULL), c->A); cyc=6; break;
    /* ---- STX ---- */
    case 0x86: cpu_write(c, addr_zp(c),  c->X); cyc=3; break;
    case 0x96: cpu_write(c, addr_zpy(c), c->X); cyc=4; break;
    case 0x8E: cpu_write(c, addr_abs(c), c->X); cyc=4; break;
    /* ---- STY ---- */
    case 0x84: cpu_write(c, addr_zp(c),  c->Y); cyc=3; break;
    case 0x94: cpu_write(c, addr_zpx(c), c->Y); cyc=4; break;
    case 0x8C: cpu_write(c, addr_abs(c), c->Y); cyc=4; break;
    /* ---- Transfer ---- */
    case 0xAA: c->X = c->A; SET_NZ(c,c->X); cyc=2; break;
    case 0xA8: c->Y = c->A; SET_NZ(c,c->Y); cyc=2; break;
    case 0x8A: c->A = c->X; SET_NZ(c,c->A); cyc=2; break;
    case 0x98: c->A = c->Y; SET_NZ(c,c->A); cyc=2; break;
    case 0xBA: c->X = c->SP; SET_NZ(c,c->X); cyc=2; break;
    case 0x9A: c->SP = c->X; cyc=2; break;
    /* ---- Stack ---- */
    case 0x48: PUSH(c, c->A); cyc=3; break;
    case 0x08: PUSH(c, c->P | FLAG_U | FLAG_B); cyc=3; break;
    case 0x68: c->A = POP(c); SET_NZ(c,c->A); cyc=4; break;
    case 0x28: c->P = (POP(c) | FLAG_U) & ~FLAG_B; cyc=4; break;
    /* ---- AND ---- */
    case 0x29: c->A &= cpu_read(c, addr_imm(c));          SET_NZ(c,c->A); cyc=2; break;
    case 0x25: c->A &= cpu_read(c, addr_zp(c));           SET_NZ(c,c->A); cyc=3; break;
    case 0x35: c->A &= cpu_read(c, addr_zpx(c));          SET_NZ(c,c->A); cyc=4; break;
    case 0x2D: c->A &= cpu_read(c, addr_abs(c));          SET_NZ(c,c->A); cyc=4; break;
    case 0x3D: c->A &= cpu_read(c, addr_absx(c,&cross));  SET_NZ(c,c->A); cyc=4+cross; break;
    case 0x39: c->A &= cpu_read(c, addr_absy(c,&cross));  SET_NZ(c,c->A); cyc=4+cross; break;
    case 0x21: c->A &= cpu_read(c, addr_indx(c));         SET_NZ(c,c->A); cyc=6; break;
    case 0x31: c->A &= cpu_read(c, addr_indy(c,&cross));  SET_NZ(c,c->A); cyc=5+cross; break;
    /* ---- ORA ---- */
    case 0x09: c->A |= cpu_read(c, addr_imm(c));          SET_NZ(c,c->A); cyc=2; break;
    case 0x05: c->A |= cpu_read(c, addr_zp(c));           SET_NZ(c,c->A); cyc=3; break;
    case 0x15: c->A |= cpu_read(c, addr_zpx(c));          SET_NZ(c,c->A); cyc=4; break;
    case 0x0D: c->A |= cpu_read(c, addr_abs(c));          SET_NZ(c,c->A); cyc=4; break;
    case 0x1D: c->A |= cpu_read(c, addr_absx(c,&cross));  SET_NZ(c,c->A); cyc=4+cross; break;
    case 0x19: c->A |= cpu_read(c, addr_absy(c,&cross));  SET_NZ(c,c->A); cyc=4+cross; break;
    case 0x01: c->A |= cpu_read(c, addr_indx(c));         SET_NZ(c,c->A); cyc=6; break;
    case 0x11: c->A |= cpu_read(c, addr_indy(c,&cross));  SET_NZ(c,c->A); cyc=5+cross; break;
    /* ---- EOR ---- */
    case 0x49: c->A ^= cpu_read(c, addr_imm(c));          SET_NZ(c,c->A); cyc=2; break;
    case 0x45: c->A ^= cpu_read(c, addr_zp(c));           SET_NZ(c,c->A); cyc=3; break;
    case 0x55: c->A ^= cpu_read(c, addr_zpx(c));          SET_NZ(c,c->A); cyc=4; break;
    case 0x4D: c->A ^= cpu_read(c, addr_abs(c));          SET_NZ(c,c->A); cyc=4; break;
    case 0x5D: c->A ^= cpu_read(c, addr_absx(c,&cross));  SET_NZ(c,c->A); cyc=4+cross; break;
    case 0x59: c->A ^= cpu_read(c, addr_absy(c,&cross));  SET_NZ(c,c->A); cyc=4+cross; break;
    case 0x41: c->A ^= cpu_read(c, addr_indx(c));         SET_NZ(c,c->A); cyc=6; break;
    case 0x51: c->A ^= cpu_read(c, addr_indy(c,&cross));  SET_NZ(c,c->A); cyc=5+cross; break;
    /* ---- ADC ---- */
    case 0x69: do_adc(c, cpu_read(c, addr_imm(c)));   cyc=2; break;
    case 0x65: do_adc(c, cpu_read(c, addr_zp(c)));    cyc=3; break;
    case 0x75: do_adc(c, cpu_read(c, addr_zpx(c)));   cyc=4; break;
    case 0x6D: do_adc(c, cpu_read(c, addr_abs(c)));   cyc=4; break;
    case 0x7D: do_adc(c, cpu_read(c, addr_absx(c,&cross))); cyc=4+cross; break;
    case 0x79: do_adc(c, cpu_read(c, addr_absy(c,&cross))); cyc=4+cross; break;
    case 0x61: do_adc(c, cpu_read(c, addr_indx(c)));  cyc=6; break;
    case 0x71: do_adc(c, cpu_read(c, addr_indy(c,&cross))); cyc=5+cross; break;
    /* ---- SBC ---- */
    case 0xE9: do_sbc(c, cpu_read(c, addr_imm(c)));   cyc=2; break;
    case 0xEB: do_sbc(c, cpu_read(c, addr_imm(c)));   cyc=2; break; /* undoc */
    case 0xE5: do_sbc(c, cpu_read(c, addr_zp(c)));    cyc=3; break;
    case 0xF5: do_sbc(c, cpu_read(c, addr_zpx(c)));   cyc=4; break;
    case 0xED: do_sbc(c, cpu_read(c, addr_abs(c)));   cyc=4; break;
    case 0xFD: do_sbc(c, cpu_read(c, addr_absx(c,&cross))); cyc=4+cross; break;
    case 0xF9: do_sbc(c, cpu_read(c, addr_absy(c,&cross))); cyc=4+cross; break;
    case 0xE1: do_sbc(c, cpu_read(c, addr_indx(c)));  cyc=6; break;
    case 0xF1: do_sbc(c, cpu_read(c, addr_indy(c,&cross))); cyc=5+cross; break;
    /* ---- CMP ---- */
    case 0xC9: do_cmp(c, c->A, cpu_read(c, addr_imm(c)));  cyc=2; break;
    case 0xC5: do_cmp(c, c->A, cpu_read(c, addr_zp(c)));   cyc=3; break;
    case 0xD5: do_cmp(c, c->A, cpu_read(c, addr_zpx(c)));  cyc=4; break;
    case 0xCD: do_cmp(c, c->A, cpu_read(c, addr_abs(c)));  cyc=4; break;
    case 0xDD: do_cmp(c, c->A, cpu_read(c, addr_absx(c,&cross))); cyc=4+cross; break;
    case 0xD9: do_cmp(c, c->A, cpu_read(c, addr_absy(c,&cross))); cyc=4+cross; break;
    case 0xC1: do_cmp(c, c->A, cpu_read(c, addr_indx(c))); cyc=6; break;
    case 0xD1: do_cmp(c, c->A, cpu_read(c, addr_indy(c,&cross))); cyc=5+cross; break;
    /* ---- CPX ---- */
    case 0xE0: do_cmp(c, c->X, cpu_read(c, addr_imm(c))); cyc=2; break;
    case 0xE4: do_cmp(c, c->X, cpu_read(c, addr_zp(c)));  cyc=3; break;
    case 0xEC: do_cmp(c, c->X, cpu_read(c, addr_abs(c))); cyc=4; break;
    /* ---- CPY ---- */
    case 0xC0: do_cmp(c, c->Y, cpu_read(c, addr_imm(c))); cyc=2; break;
    case 0xC4: do_cmp(c, c->Y, cpu_read(c, addr_zp(c)));  cyc=3; break;
    case 0xCC: do_cmp(c, c->Y, cpu_read(c, addr_abs(c))); cyc=4; break;
    /* ---- INC ---- */
    case 0xE6: addr=addr_zp(c);  val=cpu_read(c,addr)+1; cpu_write(c,addr,val); SET_NZ(c,val); cyc=5; break;
    case 0xF6: addr=addr_zpx(c); val=cpu_read(c,addr)+1; cpu_write(c,addr,val); SET_NZ(c,val); cyc=6; break;
    case 0xEE: addr=addr_abs(c); val=cpu_read(c,addr)+1; cpu_write(c,addr,val); SET_NZ(c,val); cyc=6; break;
    case 0xFE: addr=addr_absx(c,NULL); val=cpu_read(c,addr)+1; cpu_write(c,addr,val); SET_NZ(c,val); cyc=7; break;
    /* ---- DEC ---- */
    case 0xC6: addr=addr_zp(c);  val=cpu_read(c,addr)-1; cpu_write(c,addr,val); SET_NZ(c,val); cyc=5; break;
    case 0xD6: addr=addr_zpx(c); val=cpu_read(c,addr)-1; cpu_write(c,addr,val); SET_NZ(c,val); cyc=6; break;
    case 0xCE: addr=addr_abs(c); val=cpu_read(c,addr)-1; cpu_write(c,addr,val); SET_NZ(c,val); cyc=6; break;
    case 0xDE: addr=addr_absx(c,NULL); val=cpu_read(c,addr)-1; cpu_write(c,addr,val); SET_NZ(c,val); cyc=7; break;
    /* ---- INX/INY/DEX/DEY ---- */
    case 0xE8: c->X++; SET_NZ(c,c->X); cyc=2; break;
    case 0xC8: c->Y++; SET_NZ(c,c->Y); cyc=2; break;
    case 0xCA: c->X--; SET_NZ(c,c->X); cyc=2; break;
    case 0x88: c->Y--; SET_NZ(c,c->Y); cyc=2; break;
    /* ---- ASL ---- */
    case 0x0A: c->A = do_asl(c, c->A); cyc=2; break;
    case 0x06: addr=addr_zp(c);  cpu_write(c,addr,do_asl(c,cpu_read(c,addr))); cyc=5; break;
    case 0x16: addr=addr_zpx(c); cpu_write(c,addr,do_asl(c,cpu_read(c,addr))); cyc=6; break;
    case 0x0E: addr=addr_abs(c); cpu_write(c,addr,do_asl(c,cpu_read(c,addr))); cyc=6; break;
    case 0x1E: addr=addr_absx(c,NULL); cpu_write(c,addr,do_asl(c,cpu_read(c,addr))); cyc=7; break;
    /* ---- LSR ---- */
    case 0x4A: c->A = do_lsr(c, c->A); cyc=2; break;
    case 0x46: addr=addr_zp(c);  cpu_write(c,addr,do_lsr(c,cpu_read(c,addr))); cyc=5; break;
    case 0x56: addr=addr_zpx(c); cpu_write(c,addr,do_lsr(c,cpu_read(c,addr))); cyc=6; break;
    case 0x4E: addr=addr_abs(c); cpu_write(c,addr,do_lsr(c,cpu_read(c,addr))); cyc=6; break;
    case 0x5E: addr=addr_absx(c,NULL); cpu_write(c,addr,do_lsr(c,cpu_read(c,addr))); cyc=7; break;
    /* ---- ROL ---- */
    case 0x2A: c->A = do_rol(c, c->A); cyc=2; break;
    case 0x26: addr=addr_zp(c);  cpu_write(c,addr,do_rol(c,cpu_read(c,addr))); cyc=5; break;
    case 0x36: addr=addr_zpx(c); cpu_write(c,addr,do_rol(c,cpu_read(c,addr))); cyc=6; break;
    case 0x2E: addr=addr_abs(c); cpu_write(c,addr,do_rol(c,cpu_read(c,addr))); cyc=6; break;
    case 0x3E: addr=addr_absx(c,NULL); cpu_write(c,addr,do_rol(c,cpu_read(c,addr))); cyc=7; break;
    /* ---- ROR ---- */
    case 0x6A: c->A = do_ror(c, c->A); cyc=2; break;
    case 0x66: addr=addr_zp(c);  cpu_write(c,addr,do_ror(c,cpu_read(c,addr))); cyc=5; break;
    case 0x76: addr=addr_zpx(c); cpu_write(c,addr,do_ror(c,cpu_read(c,addr))); cyc=6; break;
    case 0x6E: addr=addr_abs(c); cpu_write(c,addr,do_ror(c,cpu_read(c,addr))); cyc=6; break;
    case 0x7E: addr=addr_absx(c,NULL); cpu_write(c,addr,do_ror(c,cpu_read(c,addr))); cyc=7; break;
    /* ---- JMP ---- */
    case 0x4C: c->PC = addr_abs(c); cyc=3; break;
    case 0x6C: c->PC = addr_ind(c); cyc=5; break;
    /* ---- JSR / RTS ---- */
    case 0x20:
        addr = addr_abs(c);
        PUSH(c, (c->PC - 1) >> 8);
        PUSH(c, (c->PC - 1) & 0xFF);
        c->PC = addr; cyc=6; break;
    case 0x60: {
        uint16_t lo2 = POP(c);
        uint16_t hi2 = POP(c);
        c->PC = ((hi2 << 8) | lo2) + 1;
        cyc=6; break;
    }
    /* ---- RTI ---- */
    case 0x40: {
        c->P = (POP(c) | FLAG_U) & ~FLAG_B;
        uint16_t lo2 = POP(c);
        uint16_t hi2 = POP(c);
        c->PC = (hi2 << 8) | lo2;
        cyc=6; break;
    }
    /* ---- BRK ---- */
    case 0x00:
        c->PC++;
        PUSH(c, c->PC >> 8);
        PUSH(c, c->PC & 0xFF);
        PUSH(c, c->P | FLAG_U | FLAG_B);
        SET_FLAG(c, FLAG_I);
        { uint16_t lo2 = cpu_read(c, VEC_IRQ);
          uint16_t hi2 = cpu_read(c, VEC_IRQ+1);
          c->PC = (hi2 << 8) | lo2; }
        cyc=7; break;
    /* ---- BIT ---- */
    case 0x24: do_bit(c, cpu_read(c, addr_zp(c)));  cyc=3; break;
    case 0x2C: do_bit(c, cpu_read(c, addr_abs(c))); cyc=4; break;
    /* ---- Branches ---- */
    case 0x90: cyc = 2 + do_branch(c, !GET_FLAG(c,FLAG_C)); break;
    case 0xB0: cyc = 2 + do_branch(c,  GET_FLAG(c,FLAG_C)); break;
    case 0xD0: cyc = 2 + do_branch(c, !GET_FLAG(c,FLAG_Z)); break;
    case 0xF0: cyc = 2 + do_branch(c,  GET_FLAG(c,FLAG_Z)); break;
    case 0x10: cyc = 2 + do_branch(c, !GET_FLAG(c,FLAG_N)); break;
    case 0x30: cyc = 2 + do_branch(c,  GET_FLAG(c,FLAG_N)); break;
    case 0x50: cyc = 2 + do_branch(c, !GET_FLAG(c,FLAG_V)); break;
    case 0x70: cyc = 2 + do_branch(c,  GET_FLAG(c,FLAG_V)); break;
    /* ---- Status flag ops ---- */
    case 0x18: CLR_FLAG(c, FLAG_C); cyc=2; break;
    case 0x38: SET_FLAG(c, FLAG_C); cyc=2; break;
    case 0x58: CLR_FLAG(c, FLAG_I); cyc=2; break;
    case 0x78: SET_FLAG(c, FLAG_I); cyc=2; break;
    case 0xB8: CLR_FLAG(c, FLAG_V); cyc=2; break;
    case 0xD8: CLR_FLAG(c, FLAG_D); cyc=2; break;
    case 0xF8: SET_FLAG(c, FLAG_D); cyc=2; break;
    /* ---- NOP ---- */
    case 0xEA: cyc=2; break;
    /* ---- Undocumented NOPs ---- */
    case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: cyc=2; break;
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: c->PC++; cyc=2; break;
    case 0x04: case 0x44: case 0x64: c->PC++; cyc=3; break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: c->PC++; cyc=4; break;
    case 0x0C: c->PC+=2; cyc=4; break;
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
        addr_absx(c,&cross); cyc=4+cross; break;
    /* ---- LAX (undoc) ---- */
    case 0xA7: c->A = c->X = cpu_read(c, addr_zp(c));  SET_NZ(c,c->A); cyc=3; break;
    case 0xB7: c->A = c->X = cpu_read(c, addr_zpy(c)); SET_NZ(c,c->A); cyc=4; break;
    case 0xAF: c->A = c->X = cpu_read(c, addr_abs(c)); SET_NZ(c,c->A); cyc=4; break;
    case 0xBF: c->A = c->X = cpu_read(c, addr_absy(c,&cross)); SET_NZ(c,c->A); cyc=4+cross; break;
    case 0xA3: c->A = c->X = cpu_read(c, addr_indx(c)); SET_NZ(c,c->A); cyc=6; break;
    case 0xB3: c->A = c->X = cpu_read(c, addr_indy(c,&cross)); SET_NZ(c,c->A); cyc=5+cross; break;
    /* ---- SAX (undoc) ---- */
    case 0x87: cpu_write(c, addr_zp(c),  c->A & c->X); cyc=3; break;
    case 0x97: cpu_write(c, addr_zpy(c), c->A & c->X); cyc=4; break;
    case 0x8F: cpu_write(c, addr_abs(c), c->A & c->X); cyc=4; break;
    case 0x83: cpu_write(c, addr_indx(c),c->A & c->X); cyc=6; break;
    /* ---- SLO (undoc) ---- */
    case 0x07: addr=addr_zp(c);  val=do_slo(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=5; break;
    case 0x17: addr=addr_zpx(c); val=do_slo(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0x0F: addr=addr_abs(c); val=do_slo(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0x1F: addr=addr_absx(c,NULL); val=do_slo(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0x1B: addr=addr_absy(c,NULL); val=do_slo(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0x03: addr=addr_indx(c); val=do_slo(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    case 0x13: addr=addr_indy(c,NULL); val=do_slo(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    /* ---- RLA (undoc) ---- */
    case 0x27: addr=addr_zp(c);  val=do_rla(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=5; break;
    case 0x37: addr=addr_zpx(c); val=do_rla(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0x2F: addr=addr_abs(c); val=do_rla(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0x3F: addr=addr_absx(c,NULL); val=do_rla(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0x3B: addr=addr_absy(c,NULL); val=do_rla(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0x23: addr=addr_indx(c); val=do_rla(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    case 0x33: addr=addr_indy(c,NULL); val=do_rla(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    /* ---- SRE (undoc) ---- */
    case 0x47: addr=addr_zp(c);  val=do_sre(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=5; break;
    case 0x57: addr=addr_zpx(c); val=do_sre(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0x4F: addr=addr_abs(c); val=do_sre(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0x5F: addr=addr_absx(c,NULL); val=do_sre(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0x5B: addr=addr_absy(c,NULL); val=do_sre(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0x43: addr=addr_indx(c); val=do_sre(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    case 0x53: addr=addr_indy(c,NULL); val=do_sre(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    /* ---- RRA (undoc) ---- */
    case 0x67: addr=addr_zp(c);  val=do_rra(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=5; break;
    case 0x77: addr=addr_zpx(c); val=do_rra(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0x6F: addr=addr_abs(c); val=do_rra(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0x7F: addr=addr_absx(c,NULL); val=do_rra(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0x7B: addr=addr_absy(c,NULL); val=do_rra(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0x63: addr=addr_indx(c); val=do_rra(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    case 0x73: addr=addr_indy(c,NULL); val=do_rra(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    /* ---- DCP (undoc) ---- */
    case 0xC7: addr=addr_zp(c);  val=do_dcp(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=5; break;
    case 0xD7: addr=addr_zpx(c); val=do_dcp(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0xCF: addr=addr_abs(c); val=do_dcp(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0xDF: addr=addr_absx(c,NULL); val=do_dcp(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0xDB: addr=addr_absy(c,NULL); val=do_dcp(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0xC3: addr=addr_indx(c); val=do_dcp(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    case 0xD3: addr=addr_indy(c,NULL); val=do_dcp(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    /* ---- ISC (undoc) ---- */
    case 0xE7: addr=addr_zp(c);  val=do_isc(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=5; break;
    case 0xF7: addr=addr_zpx(c); val=do_isc(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0xEF: addr=addr_abs(c); val=do_isc(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=6; break;
    case 0xFF: addr=addr_absx(c,NULL); val=do_isc(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0xFB: addr=addr_absy(c,NULL); val=do_isc(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=7; break;
    case 0xE3: addr=addr_indx(c); val=do_isc(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    case 0xF3: addr=addr_indy(c,NULL); val=do_isc(c,cpu_read(c,addr)); cpu_write(c,addr,val); cyc=8; break;
    /* ---- Default: treat unknown as 2-cycle NOP ---- */
    default: cyc=2; break;
    }

    c->total_cycles += cyc;
    return cyc;
}

uint8_t cpu_read(CPU *cpu, uint16_t addr) {
    return nes_cpu_read(cpu->nes, addr);
}

void cpu_write(CPU *cpu, uint16_t addr, uint8_t val) {
    nes_cpu_write(cpu->nes, addr, val);
}
