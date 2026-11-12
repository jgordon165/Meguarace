/* SDL_GetBasePath / SDL_GetPrefPath on the Xbox 360: the port's folder on
 * the USB stick, and its save/ folder. Takes the place of SDL's dummy file
 * system driver. */
#include "SDL_internal.h"

#include "SDL_filesystem.h"

#include "xenon_platform.h"

#include <sys/stat.h>

char *SDL_GetBasePath(void)
{
    xp_init();
    return SDL_strdup(xp_root());
}

char *SDL_GetPrefPath(const char *org, const char *app)
{
    size_t len;
    char *p;
    (void)org; (void)app;
    xp_init();
    len = SDL_strlen(xp_root()) + 6;
    p = (char *)SDL_malloc(len);
    if (!p) {
        SDL_OutOfMemory();
        return NULL;
    }
    SDL_snprintf(p, len, "%ssave/", xp_root());
    mkdir(p, 0777);
    return p;
}
