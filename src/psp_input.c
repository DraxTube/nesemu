/*
 * PSP Input handler
 */
#include "psp_input.h"
#include <pspctrl.h>
#include <string.h>

void psp_input_init(void) {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

void psp_input_update(PSPInput *inp) {
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);
    inp->prev_buttons = inp->buttons;
    inp->buttons   = pad.Buttons;
    inp->analog_x  = pad.Lx;
    inp->analog_y  = pad.Ly;
}

int psp_input_pressed(PSPInput *inp, uint32_t btn) {
    return ((inp->buttons & btn) && !(inp->prev_buttons & btn));
}

int psp_input_held(PSPInput *inp, uint32_t btn) {
    return !!(inp->buttons & btn);
}

/*
 * Default mapping:
 *   Cross   -> A
 *   Circle  -> B
 *   Select  -> Select
 *   Start   -> Start
 *   D-pad   -> D-pad
 *   L/R     -> not mapped (used for emulator functions)
 */
uint8_t psp_input_to_nes(PSPInput *inp, int player) {
    uint8_t nes_buttons = 0;
    uint32_t btns = inp->buttons;

    if (player == 0) {
        if (btns & PSP_CTRL_CROSS)    nes_buttons |= BTN_A;
        if (btns & PSP_CTRL_CIRCLE)   nes_buttons |= BTN_B;
        if (btns & PSP_CTRL_SELECT)   nes_buttons |= BTN_SELECT;
        if (btns & PSP_CTRL_START)    nes_buttons |= BTN_START;
        if (btns & PSP_CTRL_UP)       nes_buttons |= BTN_UP;
        if (btns & PSP_CTRL_DOWN)     nes_buttons |= BTN_DOWN;
        if (btns & PSP_CTRL_LEFT)     nes_buttons |= BTN_LEFT;
        if (btns & PSP_CTRL_RIGHT)    nes_buttons |= BTN_RIGHT;
        /* Also support analog stick */
        if (inp->analog_y < 64)       nes_buttons |= BTN_UP;
        if (inp->analog_y > 192)      nes_buttons |= BTN_DOWN;
        if (inp->analog_x < 64)       nes_buttons |= BTN_LEFT;
        if (inp->analog_x > 192)      nes_buttons |= BTN_RIGHT;
    }
    return nes_buttons;
}
