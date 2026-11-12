/* SDL's clock on the Xbox 360: the PowerPC time base (xp_counter). Takes the
 * place of SDL's per-platform timer file. */
#include "SDL_internal.h"
#include "SDL_error.h"

#include "SDL_timer.h"
#include "timer/SDL_timer_c.h"

#include "xenon_platform.h"

static SDL_bool ticks_started = SDL_FALSE;
static Uint64 start;

void SDL_TicksInit(void)
{
    if (ticks_started) return;
    ticks_started = SDL_TRUE;
    start = xp_counter();
}

void SDL_TicksQuit(void) { ticks_started = SDL_FALSE; }

Uint64 SDL_GetTicks64(void)
{
    if (!ticks_started) SDL_TicksInit();
    return (xp_counter() - start) * 1000 / xp_frequency();
}

Uint64 SDL_GetPerformanceCounter(void) { return xp_counter(); }
Uint64 SDL_GetPerformanceFrequency(void) { return xp_frequency(); }
void SDL_Delay(Uint32 ms) { xp_delay_ms(ms); }
