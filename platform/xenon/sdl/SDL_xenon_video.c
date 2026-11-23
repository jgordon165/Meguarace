/* SDL video for the Xbox 360: one window the size of the TV picture, drawn
 * by SDL's software renderer into a framebuffer that xp_present() puts on
 * screen. Controllers arrive as keyboard keys (see key_map below). This file
 * takes the place of SDL's dummy video driver (DUMMY_bootstrap). */
#include "SDL_internal.h"
#include "SDL_error.h"

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
enum { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_RETURN, K_ESCAPE, K_SPACE, K_F10, K_M, K_BACKSPACE, K_P, K_COUNT };
static const SDL_Scancode key_code[K_COUNT] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_RETURN,
    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_SPACE, SDL_SCANCODE_F10, SDL_SCANCODE_M, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_P,
};
static int key_down[K_COUNT];

/* The buttons themselves, for the game's controller setup (Options >
 * CONTROLLER): XENON_PadButtons() gives the XP_BTN_ bits held on any pad,
 * and XENON_SetPadKeyMask() stops the buttons in a mask from also being
 * keys (a race takes its bound buttons for itself, so B as fire doesn't
 * also give up the race as ESC). xenon_platform.h has the bits. */
static unsigned pad_buttons, pad_key_mask;
unsigned XENON_PadButtons(void) { return pad_buttons; }
void XENON_SetPadKeyMask(unsigned mask) { pad_key_mask = mask; }

#define STICK 16000
#define TRIGGER 60

static unsigned pad_bits(const xp_pad *p)
{
    unsigned b = 0;
    if (p->a) b |= XP_BTN_A;
    if (p->b) b |= XP_BTN_B;
    if (p->x) b |= XP_BTN_X;
    if (p->y) b |= XP_BTN_Y;
    if (p->lb) b |= XP_BTN_LB;
    if (p->rb) b |= XP_BTN_RB;
    if (p->lt > TRIGGER) b |= XP_BTN_LT;
    if (p->rt > TRIGGER) b |= XP_BTN_RT;
    if (p->ls) b |= XP_BTN_LS;
    if (p->rs) b |= XP_BTN_RS;
    if (p->start) b |= XP_BTN_START;
    if (p->back) b |= XP_BTN_BACK;
    if (p->up || p->ly > STICK) b |= XP_BTN_UP;        /* the D-pad or the left stick */
    if (p->down || p->ly < -STICK) b |= XP_BTN_DOWN;
    if (p->left || p->lx < -STICK) b |= XP_BTN_LEFT;
    if (p->right || p->lx > STICK) b |= XP_BTN_RIGHT;
    return b;
}

/* A pad with the masked buttons let go (they stay buttons, not keys). */
static void unmask(xp_pad *p, unsigned m)
{
    if (m & XP_BTN_A) p->a = 0;
    if (m & XP_BTN_B) p->b = 0;
    if (m & XP_BTN_X) p->x = 0;
    if (m & XP_BTN_Y) p->y = 0;
    if (m & XP_BTN_LB) p->lb = 0;
    if (m & XP_BTN_RB) p->rb = 0;
    if (m & XP_BTN_LT) p->lt = 0;
    if (m & XP_BTN_RT) p->rt = 0;
    if (m & XP_BTN_LS) p->ls = 0;
    if (m & XP_BTN_RS) p->rs = 0;
    if (m & XP_BTN_START) p->start = 0;
    if (m & XP_BTN_BACK) p->back = 0;
    if (m & XP_BTN_UP) { p->up = 0; if (p->ly > 0) p->ly = 0; }
    if (m & XP_BTN_DOWN) { p->down = 0; if (p->ly < 0) p->ly = 0; }
    if (m & XP_BTN_LEFT) { p->left = 0; if (p->lx < 0) p->lx = 0; }
    if (m & XP_BTN_RIGHT) { p->right = 0; if (p->lx > 0) p->lx = 0; }
}

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
    k[K_P] |= p->start;                                          /* pause in races (START is RETURN too) */
}

static void XENON_PumpEvents(_THIS)
{
    int now[K_COUNT] = { 0 };
    unsigned buttons = 0;
    int port, i;
    (void)_this;
    xp_poll();
    for (port = 0; port < 4; ++port) {
        xp_pad p;
        xp_get_pad(port, &p);
        if (!p.connected) continue;
        buttons |= pad_bits(&p);
        unmask(&p, pad_key_mask);
        key_map(&p, now);
    }
    pad_buttons = buttons;
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
        SDL_Error(SDL_ENOMEM);
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
