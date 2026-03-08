#ifndef APU_H
#define APU_H

#include <stdint.h>

#define APU_SAMPLE_RATE  44100
#define APU_BUFFER_SIZE  4096

typedef struct NES NES;

/* Pulse channel */
typedef struct {
    uint8_t  enabled;
    uint8_t  duty;
    uint8_t  length_halt;
    uint8_t  constant_vol;
    uint8_t  volume;
    uint8_t  env_start;
    uint8_t  env_div;
    uint8_t  env_vol;
    uint8_t  sweep_enabled;
    uint8_t  sweep_period;
    uint8_t  sweep_negate;
    uint8_t  sweep_shift;
    uint8_t  sweep_reload;
    uint8_t  sweep_div;
    uint16_t timer;
    uint16_t timer_period;
    uint8_t  length;
    uint8_t  seq;
} PulseChannel;

/* Triangle channel */
typedef struct {
    uint8_t  enabled;
    uint8_t  length_halt;
    uint8_t  linear_reload;
    uint8_t  linear_counter;
    uint8_t  linear_period;
    uint16_t timer;
    uint16_t timer_period;
    uint8_t  length;
    uint8_t  seq;
} TriangleChannel;

/* Noise channel */
typedef struct {
    uint8_t  enabled;
    uint8_t  length_halt;
    uint8_t  constant_vol;
    uint8_t  volume;
    uint8_t  env_start;
    uint8_t  env_div;
    uint8_t  env_vol;
    uint8_t  mode;
    uint16_t timer;
    uint16_t timer_period;
    uint8_t  length;
    uint16_t shift;
} NoiseChannel;

/* DMC channel */
typedef struct {
    uint8_t  enabled;
    uint8_t  irq_enable;
    uint8_t  loop;
    uint16_t rate;
    uint8_t  output;
    uint16_t sample_addr;
    uint16_t sample_len;
    uint16_t cur_addr;
    uint16_t remaining;
    uint8_t  sample_buf;
    uint8_t  sample_buf_valid;
    uint8_t  shift_reg;
    uint8_t  bits_remaining;
    uint8_t  silence;
    uint16_t timer;
} DMCChannel;

typedef struct APU {
    PulseChannel    pulse[2];
    TriangleChannel triangle;
    NoiseChannel    noise;
    DMCChannel      dmc;

    uint8_t  frame_mode;        /* 0=4-step, 1=5-step */
    uint8_t  frame_irq_inhibit;
    uint8_t  frame_irq_flag;
    int      frame_counter;
    int      frame_step;

    /* Output buffer */
    int16_t  buffer[APU_BUFFER_SIZE];
    int      buf_pos;
    int      buf_ready;

    /* Cycle accumulator for sample generation */
    double   cycle_acc;
    double   cycles_per_sample;

    NES     *nes;
} APU;

void    apu_init(APU *apu, NES *nes);
void    apu_reset(APU *apu);
void    apu_step(APU *apu);         /* 1 CPU cycle */
uint8_t apu_read(APU *apu, uint16_t addr);
void    apu_write(APU *apu, uint16_t addr, uint8_t val);

/* Length counter lookup table */
extern const uint8_t apu_length_table[32];
/* Noise period table (NTSC) */
extern const uint16_t apu_noise_period[16];
/* DMC rate table (NTSC) */
extern const uint16_t apu_dmc_rate[16];

#endif /* APU_H */
