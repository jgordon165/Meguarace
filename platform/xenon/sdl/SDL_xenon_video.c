/* SDL video for the Xbox 360: one window the size of the TV picture, drawn
 * by SDL's software renderer into a framebuffer that xp_present() puts on
 * screen. Controllers arrive as keyboard keys (see key_map below). This file
 * takes the place of SDL's dummy video driver (DUMMY_bootstrap). */
#include "SDL_internal.h"

#include "SDL_video.h"
#include "video/SDL_sysvideo.h"
#include "events/SDL_events_c.h"
#include "events/SDL_keyboard_c.h"
#include "events/SDL_windowevents_c.h"

#include "xenon_platform.h"

extern void XENON_AudioPump(void);

#define XENON_SURFACE "_SDL_XenonSurface"

/* Controller -> keys. The game knows keys only, so each button stands for
 * the key that does the same job on a PC (README: "Xbox 360"). */
enum { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_RETURN, K_ESCAPE, K_SPACE, K_F10, K_M, K_BACKSPACE, K_COUNT };
static const SDL_Scancode key_code[K_COUNT] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_RETURN,
    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_SPACE, SDL_SCANCODE_F10, SDL_SCANCODE_M, SDL_SCANCODE_BACKSPACE,
};
static int key_down[K_COUNT];

#define STICK 16000
#define TRIGGER 60

static void key_map(const xp_pad *p, int *k)
{
    k[K_UP] |= p->up || p->ly > STICK || p->rt > TRIGGER;        /* RT accelerates */
    k[K_DOWN] |= p->down || p->ly < -STICK || p->lt > TRIGGER;   /* LT brakes */
    k[K_LEFT] |= p->left || p->lx < -STICK;
    k[K_RIGHT] |= p->right || p->lx > STICK;
    k[K_RETURN] |= p->a || p->start;
    k[K_ESCAPE] |= p->b || p->back;
    k[K_SPACE] |= p->x || p->rb;                                  /* fire */
    k[K_F10] |= p->y;                                             /* HD on/off */
    k[K_M] |= p->lb;                                              /* music on/off in the menu */
    k[K_BACKSPACE] |= p->rs;
}

static void XENON_PumpEvents(_THIS)
{
    int now[K_COUNT] = { 0 };
    int port, i;
    (void)_this;
    xp_poll();
    for (port = 0; port < 4; ++port) {
        xp_pad p;
        xp_get_pad(port, &p);
        if (p.connected) key_map(&p, now);
    }
    for (i = 0; i < K_COUNT; ++i) {
        if (now[i] != key_down[i]) {
            key_down[i] = now[i];
            SDL_SendKeyboardKey(now[i] ? SDL_PRESSED : SDL_RELEASED, key_code[i]);
        }
    }
    XENON_AudioPump();
}

static void fit_to_tv(SDL_Window *window)
{
    int w, h;
    xp_display_size(&w, &h);
    if (window->w != w || window->h != h) SDL_SendWindowEvent(window, SDL_WINDOWEVENT_RESIZED, w, h);
}

static int XENON_CreateWindow(_THIS, SDL_Window *window)
{
    int w, h;
    (void)_this;
    xp_display_size(&w, &h);
    window->x = 0;
    window->y = 0;
    window->w = w;
    window->h = h;
    SDL_SetKeyboardFocus(window);
    return 0;
}

static void XENON_SetWindowSize(_THIS, SDL_Window *window) { (void)_this; fit_to_tv(window); }
static void XENON_SetWindowFullscreen(_THIS, SDL_Window *window, SDL_VideoDisplay *d, SDL_bool on)
{
    (void)_this; (void)d; (void)on;
    fit_to_tv(window);
}

static int XENON_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    SDL_Surface *surface;
    int w, h;
    (void)_this;
    SDL_FreeSurface((SDL_Surface *)SDL_SetWindowData(window, XENON_SURFACE, NULL));
    SDL_GetWindowSizeInPixels(window, &w, &h);
    surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return -1;
    SDL_SetWindowData(window, XENON_SURFACE, surface);
    *format = SDL_PIXELFORMAT_ARGB8888;
    *pixels = surface->pixels;
    *pitch = surface->pitch;
    return 0;
}

static int XENON_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_Surface *surface = (SDL_Surface *)SDL_GetWindowData(window, XENON_SURFACE);
    (void)_this; (void)rects; (void)numrects;
    if (!surface) return SDL_SetError("no framebuffer");
    xp_present((const uint32_t *)surface->pixels, surface->w, surface->h, surface->pitch);
    XENON_AudioPump();
    return 0;
}

static void XENON_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    SDL_Surface *surface = (SDL_Surface *)SDL_SetWindowData(window, XENON_SURFACE, NULL);
    (void)_this;
    SDL_FreeSurface(surface);
}

static int XENON_VideoInit(_THIS)
{
    SDL_DisplayMode mode;
    (void)_this;
    if (xp_init() != 0) return SDL_SetError("Xbox 360 start-up failed");
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB888;
    xp_display_size(&mode.w, &mode.h);
    mode.refresh_rate = 60;
    if (SDL_AddBasicVideoDisplay(&mode) < 0) return -1;
    SDL_AddDisplayMode(&_this->displays[0], &mode);
    return 0;
}

static void XENON_VideoQuit(_THIS) { (void)_this; }

static void XENON_DeleteDevice(SDL_VideoDevice *device) { SDL_free(device); }

static SDL_VideoDevice *XENON_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }
    device->VideoInit = XENON_VideoInit;
    device->VideoQuit = XENON_VideoQuit;
    device->PumpEvents = XENON_PumpEvents;
    device->CreateSDLWindow = XENON_CreateWindow;
    device->SetWindowSize = XENON_SetWindowSize;
    device->SetWindowFullscreen = XENON_SetWindowFullscreen;
    device->CreateWindowFramebuffer = XENON_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = XENON_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = XENON_DestroyWindowFramebuffer;
    device->free = XENON_DeleteDevice;
    return device;
}

VideoBootStrap DUMMY_bootstrap = {
    "xenon", "Xbox 360 framebuffer", XENON_CreateDevice, NULL
};
