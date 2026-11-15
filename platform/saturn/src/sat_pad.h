/* Saturn control pad (port 1). */
#ifndef SAT_PAD_H
#define SAT_PAD_H

#include <stdint.h>

enum {
    SAT_PAD_UP = 1 << 0, SAT_PAD_DOWN = 1 << 1, SAT_PAD_LEFT = 1 << 2, SAT_PAD_RIGHT = 1 << 3,
    SAT_PAD_A = 1 << 4, SAT_PAD_B = 1 << 5, SAT_PAD_C = 1 << 6,
    SAT_PAD_X = 1 << 7, SAT_PAD_Y = 1 << 8, SAT_PAD_Z = 1 << 9,
    SAT_PAD_L = 1 << 10, SAT_PAD_R = 1 << 11, SAT_PAD_START = 1 << 12
};

void     sat_pad_init(void);
void     sat_pad_update(void);    /* once per frame */
uint32_t sat_pad_held(void);
uint32_t sat_pad_pressed(void);   /* went down since the last update */

#endif
