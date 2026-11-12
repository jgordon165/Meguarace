/* The Xbox 360 underneath the port's SDL2 build (platform/xenon/sdl/*):
 * video, sound, controllers, time and the USB stick. Two implementations:
 * xenon_platform.c on the console (LibXenon) and xenon_platform_sim.c, which
 * stands in for it on a PC so the same SDL build can be tested there. */
#ifndef XENON_PLATFORM_H
#define XENON_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Brings up the TV output, USB and the FAT driver, and finds the folder the
 * port was copied to. Returns 0 on success. Safe to call more than once. */
int xp_init(void);

/* The port's folder on the USB stick, ending in '/' ("uda0:/meguarace/"). */
const char *xp_root(void);

/* The TV picture's size. */
void xp_display_size(int *w, int *h);

/* Shows a frame of 0xAARRGGBB pixels (alpha ignored), w x h with `pitch`
 * bytes per row, centred on the TV. */
void xp_present(const uint32_t *argb, int w, int h, int pitch);

/* One controller's state (port 0..3). */
typedef struct xp_pad {
    int connected;
    int up, down, left, right;           /* the D-pad */
    int a, b, x, y, lb, rb, start, back, logo, ls, rs;
    int lx, ly, rx, ry;                  /* sticks, -32768..32767 (up is +) */
    int lt, rt;                          /* triggers, 0..255 */
} xp_pad;

/* Polls USB; then xp_get_pad() returns the latest state. */
void xp_poll(void);
void xp_get_pad(int port, xp_pad *out);

uint64_t xp_counter(void);
uint64_t xp_frequency(void);
void xp_delay_ms(uint32_t ms);

/* Sound: 48 kHz, 16-bit signed little-endian stereo. */
#define XP_AUDIO_RATE 48000
void xp_audio_init(void);
int xp_audio_free(void);                  /* bytes that can be queued now */
void xp_audio_submit(const void *s16le, int bytes);

/* A line for the log (the TV console before the game starts, the UART). */
void xp_log(const char *msg);

#ifdef __cplusplus
}
#endif

#endif
