/* Saturn control pad on port 1 (the SMPC is polled at each vblank-out). */
#include <yaul.h>

#include "sat_pad.h"

static uint32_t _held, _pressed;

void sat_pad_init(void) { smpc_peripheral_init(); }

void sat_pad_update(void) {
    smpc_peripheral_digital_t d;
    smpc_peripheral_process();
    smpc_peripheral_digital_port(1, &d);
    uint32_t h = 0;
    if (d.held.button.up)    h |= SAT_PAD_UP;
    if (d.held.button.down)  h |= SAT_PAD_DOWN;
    if (d.held.button.left)  h |= SAT_PAD_LEFT;
    if (d.held.button.right) h |= SAT_PAD_RIGHT;
    if (d.held.button.a)     h |= SAT_PAD_A;
    if (d.held.button.b)     h |= SAT_PAD_B;
    if (d.held.button.c)     h |= SAT_PAD_C;
    if (d.held.button.x)     h |= SAT_PAD_X;
    if (d.held.button.y)     h |= SAT_PAD_Y;
    if (d.held.button.z)     h |= SAT_PAD_Z;
    if (d.held.button.l)     h |= SAT_PAD_L;
    if (d.held.button.r)     h |= SAT_PAD_R;
    if (d.held.button.start) h |= SAT_PAD_START;
    _pressed = h & ~_held;
    _held = h;
}

uint32_t sat_pad_held(void) { return _held; }
uint32_t sat_pad_pressed(void) { return _pressed; }
