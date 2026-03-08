#ifndef PSP_INPUT_H
#define PSP_INPUT_H

#include <stdint.h>
#include <pspctrl.h>
#include "nes.h"

typedef struct {
    uint32_t buttons;
    uint32_t prev_buttons;
    uint8_t  analog_x;
    uint8_t  analog_y;
} PSPInput;

void    psp_input_init(void);
void    psp_input_update(PSPInput *inp);
uint8_t psp_input_to_nes(PSPInput *inp, int player);

int psp_input_pressed(PSPInput *inp, uint32_t btn);
int psp_input_held(PSPInput *inp, uint32_t btn);

#endif /* PSP_INPUT_H */
