/* Saturn picture: the game's 320x200 indexed screen on a VDP2 bitmap. */
#ifndef SAT_VIDEO_H
#define SAT_VIDEO_H

#include <stdint.h>

#define SAT_W 320
#define SAT_H 200

/* The CPU-side 320x200 screen the game draws into (indices). */
extern uint8_t sat_screen[SAT_W * SAT_H];

void sat_video_init(void);
/* Sets palette entries first..last from 8-bit RGB triples (pal[i*3]). */
void sat_video_palette(const uint8_t* pal, int first, int last);
/* Copies sat_screen to the back page and shows it at the next vblank. */
void sat_video_present(void);
/* Blanks the picture (palette index 0 everywhere, palette black). */
void sat_video_clear(void);

/* Vertical blanks since boot, and their rate (60 NTSC, 50 PAL). */
uint32_t sat_vblanks(void);
int sat_vblank_rate(void);
void sat_wait_vblank(void);

#endif
