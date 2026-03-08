/*
 * NES APU emulator
 * All 5 channels: Pulse1, Pulse2, Triangle, Noise, DMC
 */
#include "apu.h"
#include "nes.h"
#include <string.h>
#include <math.h>

const uint8_t apu_length_table[32] = {
    10,254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

const uint16_t apu_noise_period[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

const uint16_t apu_dmc_rate[16] = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54
};

static const uint8_t duty_table[4][8] = {
    {0,1,0,0,0,0,0,0},
    {0,1,1,0,0,0,0,0},
    {0,1,1,1,1,0,0,0},
    {1,0,0,1,1,1,1,1}
};

static const uint8_t triangle_seq[32] = {
    15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
     0, 1, 2, 3, 4, 5,6,7,8,9,10,11,12,13,14,15
};

/* ---- Envelope clock ---- */
static void clock_envelope(uint8_t *start, uint8_t *div, uint8_t *vol,
                            uint8_t constant, uint8_t period, uint8_t halt) {
    (void)constant;
    if (*start) {
        *start = 0; *vol = 15; *div = period;
    } else {
        if (*div == 0) {
            *div = period;
            if (*vol > 0) (*vol)--;
            else if (halt) *vol = 15;
        } else {
            (*div)--;
        }
    }
}

/* ---- Sweep clock ---- */
static void clock_sweep(PulseChannel *p, int chan) {
    if (p->sweep_reload) {
        p->sweep_div = p->sweep_period;
        p->sweep_reload = 0;
    } else if (p->sweep_div > 0) {
        p->sweep_div--;
    } else {
        p->sweep_div = p->sweep_period;
        if (p->sweep_enabled && p->timer_period >= 8) {
            int change = p->timer_period >> p->sweep_shift;
            if (p->sweep_negate) {
                change = -change;
                if (chan == 0) change--; /* pulse 1 uses one's complement */
            }
            int target = (int)p->timer_period + change;
            if (target < 0x800 && target >= 8) p->timer_period = target;
        }
    }
}

/* ---- Length counter ---- */
static void clock_length(uint8_t *len, uint8_t halt) {
    if (*len > 0 && !halt) (*len)--;
}

/* ---- Frame counter step ---- */
static void apu_quarter_frame(APU *a) {
    /* Envelope: pulse1, pulse2, noise */
    clock_envelope(&a->pulse[0].env_start, &a->pulse[0].env_div, &a->pulse[0].env_vol,
                   a->pulse[0].constant_vol, a->pulse[0].volume, a->pulse[0].length_halt);
    clock_envelope(&a->pulse[1].env_start, &a->pulse[1].env_div, &a->pulse[1].env_vol,
                   a->pulse[1].constant_vol, a->pulse[1].volume, a->pulse[1].length_halt);
    clock_envelope(&a->noise.env_start, &a->noise.env_div, &a->noise.env_vol,
                   a->noise.constant_vol, a->noise.volume, a->noise.length_halt);
    /* Triangle linear counter */
    if (a->triangle.linear_reload) {
        a->triangle.linear_counter = a->triangle.linear_period;
    } else if (a->triangle.linear_counter > 0) {
        a->triangle.linear_counter--;
    }
    if (!a->triangle.length_halt) a->triangle.linear_reload = 0;
}

static void apu_half_frame(APU *a) {
    apu_quarter_frame(a);
    clock_length(&a->pulse[0].length, a->pulse[0].length_halt);
    clock_length(&a->pulse[1].length, a->pulse[1].length_halt);
    clock_length(&a->triangle.length, a->triangle.length_halt);
    clock_length(&a->noise.length, a->noise.length_halt);
    clock_sweep(&a->pulse[0], 0);
    clock_sweep(&a->pulse[1], 1);
}

/* ---- Output sample generation ---- */
static float pulse_output(PulseChannel *p) {
    if (!p->enabled || p->length == 0 || p->timer_period < 8 || p->timer_period > 0x7FF)
        return 0.0f;
    uint8_t vol = p->constant_vol ? p->volume : p->env_vol;
    return duty_table[p->duty][p->seq & 7] ? (float)vol : 0.0f;
}

static float triangle_output(TriangleChannel *t) {
    if (!t->enabled || t->length == 0 || t->linear_counter == 0) return 0.0f;
    return (float)triangle_seq[t->seq & 31];
}

static float noise_output(NoiseChannel *n) {
    if (!n->enabled || n->length == 0) return 0.0f;
    if (n->shift & 1) return 0.0f;
    uint8_t vol = n->constant_vol ? n->volume : n->env_vol;
    return (float)vol;
}

static float dmc_output(DMCChannel *d) {
    return (float)d->output;
}

/* Linear approximation mixer */
static int16_t mix_sample(APU *a) {
    float p1 = pulse_output(&a->pulse[0]);
    float p2 = pulse_output(&a->pulse[1]);
    float tri = triangle_output(&a->triangle);
    float noi = noise_output(&a->noise);
    float dmc = dmc_output(&a->dmc);

    float pulse_out = 0.0f;
    if (p1 + p2 > 0.0f) pulse_out = 95.88f / ((8128.0f / (p1 + p2)) + 100.0f);

    float tnd_out = 0.0f;
    if (tri + noi + dmc > 0.0f)
        tnd_out = 159.79f / ((1.0f / (tri/8227.0f + noi/12241.0f + dmc/22638.0f)) + 100.0f);

    float mixed = (pulse_out + tnd_out) * 32767.0f;
    if (mixed >  32767.0f) mixed =  32767.0f;
    if (mixed < -32768.0f) mixed = -32768.0f;
    return (int16_t)mixed;
}

/* ---- Timer clocking ---- */
static void clock_pulse(PulseChannel *p) {
    if (p->timer == 0) {
        p->timer = p->timer_period;
        p->seq = (p->seq + 1) & 7;
    } else {
        p->timer--;
    }
}

static void clock_triangle(TriangleChannel *t) {
    if (t->timer == 0) {
        t->timer = t->timer_period;
        if (t->length > 0 && t->linear_counter > 0)
            t->seq = (t->seq + 1) & 31;
    } else {
        t->timer--;
    }
}

static void clock_noise(NoiseChannel *n) {
    if (n->timer == 0) {
        n->timer = n->timer_period;
        uint16_t fb;
        if (n->mode) fb = ((n->shift >> 6) ^ n->shift) & 1;
        else         fb = ((n->shift >> 1) ^ n->shift) & 1;
        n->shift >>= 1;
        n->shift  |= (fb << 14);
    } else {
        n->timer--;
    }
}

static void clock_dmc(DMCChannel *d, NES *nes) {
    if (d->timer == 0) {
        d->timer = d->rate;
        /* Output unit */
        if (!d->silence) {
            if (d->shift_reg & 1) {
                if (d->output <= 125) d->output += 2;
            } else {
                if (d->output >= 2) d->output -= 2;
            }
        }
        d->shift_reg >>= 1;
        if (--d->bits_remaining == 0) {
            d->bits_remaining = 8;
            if (d->sample_buf_valid) {
                d->shift_reg = d->sample_buf;
                d->sample_buf_valid = 0;
                d->silence = 0;
            } else {
                d->silence = 1;
            }
        }
        /* Refill sample buffer */
        if (!d->sample_buf_valid && d->remaining > 0) {
            d->sample_buf = nes_cpu_read(nes, d->cur_addr);
            d->sample_buf_valid = 1;
            d->cur_addr = (d->cur_addr == 0xFFFF) ? 0x8000 : d->cur_addr + 1;
            d->remaining--;
            if (d->remaining == 0) {
                if (d->loop) {
                    d->cur_addr = d->sample_addr;
                    d->remaining = d->sample_len;
                } else if (d->irq_enable) {
                    cpu_irq(&nes->cpu);
                }
            }
        }
    } else {
        d->timer--;
    }
}

/* ---- Main step (called once per CPU cycle) ---- */
void apu_step(APU *a) {
    /* Frame counter (240Hz steps) */
    a->frame_counter++;

    int step_cycles[2][6] = {
        {7457, 14913, 22371, 29828, 29829, 29830},  /* 4-step */
        {7457, 14913, 22371, 29829, 37281, 37282}   /* 5-step */
    };
    int steps = a->frame_mode ? 5 : 4;
    int mode  = a->frame_mode ? 1 : 0;

    for (int i = 0; i < steps + 1; i++) {
        if (a->frame_counter == step_cycles[mode][i]) {
            if (i == 0 || i == 2) apu_quarter_frame(a);
            else if (i == 1 || (i == 3 && !a->frame_mode)) apu_half_frame(a);
            else if (i == 3 && !a->frame_mode) {
                if (!a->frame_irq_inhibit) { a->frame_irq_flag = 1; cpu_irq(&a->nes->cpu); }
            }
            if (i == steps) a->frame_counter = 0;
            break;
        }
    }
    if (a->frame_counter >= (a->frame_mode ? 37282 : 29830)) a->frame_counter = 0;

    /* Clock pulse channels every other CPU cycle */
    if (a->frame_counter & 1) {
        clock_pulse(&a->pulse[0]);
        clock_pulse(&a->pulse[1]);
        clock_noise(&a->noise);
    }
    /* Triangle clocks every CPU cycle */
    clock_triangle(&a->triangle);
    /* DMC */
    clock_dmc(&a->dmc, a->nes);

    /* Sample generation */
    a->cycle_acc += 1.0;
    if (a->cycle_acc >= a->cycles_per_sample) {
        a->cycle_acc -= a->cycles_per_sample;
        if (a->buf_pos < APU_BUFFER_SIZE) {
            a->buffer[a->buf_pos++] = mix_sample(a);
        }
        if (a->buf_pos >= APU_BUFFER_SIZE) {
            a->buf_ready = 1;
            a->buf_pos = 0;
        }
    }
}

/* ---- Register writes ---- */
void apu_write(APU *a, uint16_t addr, uint8_t val) {
    switch (addr) {
    /* Pulse 1 */
    case 0x4000:
        a->pulse[0].duty         = (val >> 6) & 3;
        a->pulse[0].length_halt  = (val >> 5) & 1;
        a->pulse[0].constant_vol = (val >> 4) & 1;
        a->pulse[0].volume       = val & 0x0F;
        break;
    case 0x4001:
        a->pulse[0].sweep_enabled = (val >> 7) & 1;
        a->pulse[0].sweep_period  = (val >> 4) & 7;
        a->pulse[0].sweep_negate  = (val >> 3) & 1;
        a->pulse[0].sweep_shift   = val & 7;
        a->pulse[0].sweep_reload  = 1;
        break;
    case 0x4002:
        a->pulse[0].timer_period = (a->pulse[0].timer_period & 0x700) | val;
        break;
    case 0x4003:
        a->pulse[0].timer_period = (a->pulse[0].timer_period & 0x0FF) | ((val & 7) << 8);
        if (a->pulse[0].enabled) a->pulse[0].length = apu_length_table[val >> 3];
        a->pulse[0].env_start = 1;
        a->pulse[0].seq = 0;
        break;
    /* Pulse 2 */
    case 0x4004:
        a->pulse[1].duty         = (val >> 6) & 3;
        a->pulse[1].length_halt  = (val >> 5) & 1;
        a->pulse[1].constant_vol = (val >> 4) & 1;
        a->pulse[1].volume       = val & 0x0F;
        break;
    case 0x4005:
        a->pulse[1].sweep_enabled = (val >> 7) & 1;
        a->pulse[1].sweep_period  = (val >> 4) & 7;
        a->pulse[1].sweep_negate  = (val >> 3) & 1;
        a->pulse[1].sweep_shift   = val & 7;
        a->pulse[1].sweep_reload  = 1;
        break;
    case 0x4006:
        a->pulse[1].timer_period = (a->pulse[1].timer_period & 0x700) | val;
        break;
    case 0x4007:
        a->pulse[1].timer_period = (a->pulse[1].timer_period & 0x0FF) | ((val & 7) << 8);
        if (a->pulse[1].enabled) a->pulse[1].length = apu_length_table[val >> 3];
        a->pulse[1].env_start = 1;
        a->pulse[1].seq = 0;
        break;
    /* Triangle */
    case 0x4008:
        a->triangle.length_halt  = (val >> 7) & 1;
        a->triangle.linear_period = val & 0x7F;
        break;
    case 0x400A:
        a->triangle.timer_period = (a->triangle.timer_period & 0x700) | val;
        break;
    case 0x400B:
        a->triangle.timer_period = (a->triangle.timer_period & 0x0FF) | ((val & 7) << 8);
        if (a->triangle.enabled) a->triangle.length = apu_length_table[val >> 3];
        a->triangle.linear_reload = 1;
        break;
    /* Noise */
    case 0x400C:
        a->noise.length_halt  = (val >> 5) & 1;
        a->noise.constant_vol = (val >> 4) & 1;
        a->noise.volume       = val & 0x0F;
        break;
    case 0x400E:
        a->noise.mode         = (val >> 7) & 1;
        a->noise.timer_period = apu_noise_period[val & 0x0F];
        break;
    case 0x400F:
        if (a->noise.enabled) a->noise.length = apu_length_table[val >> 3];
        a->noise.env_start = 1;
        break;
    /* DMC */
    case 0x4010:
        a->dmc.irq_enable = (val >> 7) & 1;
        a->dmc.loop       = (val >> 6) & 1;
        a->dmc.rate       = apu_dmc_rate[val & 0x0F];
        break;
    case 0x4011:
        a->dmc.output = val & 0x7F;
        break;
    case 0x4012:
        a->dmc.sample_addr = 0xC000 + (val << 6);
        break;
    case 0x4013:
        a->dmc.sample_len = (val << 4) + 1;
        break;
    /* Status */
    case 0x4015:
        a->pulse[0].enabled  = (val >> 0) & 1; if (!a->pulse[0].enabled)  a->pulse[0].length = 0;
        a->pulse[1].enabled  = (val >> 1) & 1; if (!a->pulse[1].enabled)  a->pulse[1].length = 0;
        a->triangle.enabled  = (val >> 2) & 1; if (!a->triangle.enabled)  a->triangle.length = 0;
        a->noise.enabled     = (val >> 3) & 1; if (!a->noise.enabled)     a->noise.length = 0;
        a->dmc.enabled       = (val >> 4) & 1;
        if (a->dmc.enabled && a->dmc.remaining == 0) {
            a->dmc.cur_addr   = a->dmc.sample_addr;
            a->dmc.remaining  = a->dmc.sample_len;
        }
        a->frame_irq_flag = 0;
        break;
    /* Frame counter */
    case 0x4017:
        a->frame_irq_inhibit = (val >> 6) & 1;
        a->frame_mode        = (val >> 7) & 1;
        if (a->frame_irq_inhibit) a->frame_irq_flag = 0;
        if (a->frame_mode) apu_half_frame(a);
        a->frame_counter = 0;
        break;
    }
}

uint8_t apu_read(APU *a, uint16_t addr) {
    if (addr == 0x4015) {
        uint8_t val = 0;
        if (a->pulse[0].length > 0)  val |= 0x01;
        if (a->pulse[1].length > 0)  val |= 0x02;
        if (a->triangle.length > 0)  val |= 0x04;
        if (a->noise.length > 0)     val |= 0x08;
        if (a->dmc.remaining > 0)    val |= 0x10;
        if (a->frame_irq_flag)       val |= 0x40;
        a->frame_irq_flag = 0;
        return val;
    }
    return 0;
}

void apu_init(APU *a, NES *nes) {
    memset(a, 0, sizeof(APU));
    a->nes = nes;
    a->noise.shift = 1;
    /* NTSC CPU: 1789773 Hz / sample_rate */
    a->cycles_per_sample = 1789773.0 / (double)APU_SAMPLE_RATE;
    a->dmc.bits_remaining = 8;
    a->dmc.silence = 1;
}

void apu_reset(APU *a) {
    apu_write(a, 0x4015, 0x00);
    a->frame_counter = 0;
    a->cycle_acc = 0.0;
}
