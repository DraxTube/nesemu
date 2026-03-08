#ifndef PSP_INPUT_H
#define PSP_INPUT_H

#include <stdint.h>
#include "nes.h"

/* PSP button bits (from pspctrl.h) */
#define PSP_CTRL_SELECT   0x000001
#define PSP_CTRL_START    0x000008
#define PSP_CTRL_UP       0x000010
#define PSP_CTRL_RIGHT    0x000020
#define PSP_CTRL_DOWN     0x000040
#define PSP_CTRL_LEFT     0x000080
#define PSP_CTRL_LTRIGGER 0x000100
#define PSP_CTRL_RTRIGGER 0x000200
#define PSP_CTRL_TRIANGLE 0x001000
#define PSP_CTRL_CIRCLE   0x002000
#define PSP_CTRL_CROSS    0x004000
#define PSP_CTRL_SQUARE   0x008000
#define PSP_CTRL_HOME     0x010000
#define PSP_CTRL_HOLD     0x020000

typedef struct {
    uint32_t buttons;
    uint32_t prev_buttons;
    uint8_t  analog_x;
    uint8_t  analog_y;
} PSPInput;

void psp_input_init(void);
void psp_input_update(PSPInput *inp);
uint8_t psp_input_to_nes(PSPInput *inp, int player);

/* Returns 1 if combo was pressed (for emulator controls) */
int psp_input_pressed(PSPInput *inp, uint32_t btn);   /* just pressed */
int psp_input_held(PSPInput *inp, uint32_t btn);      /* held down */

#endif /* PSP_INPUT_H */
