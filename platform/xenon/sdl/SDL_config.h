/* SDL2 configuration for the Xbox 360 (LibXenon), and for the PC stand-in
 * that tests the same build (MEGARACE_XENON_SIM). Copied over SDL's own
 * SDL_config.h by platform/xenon/sdl2_xenon.cmake.
 *
 * SDL's software renderer draws everything; the drivers in this folder take
 * the place of SDL's dummy video and audio drivers, timer and file system. */
#ifndef SDL_config_h_
#define SDL_config_h_

#include "SDL_platform.h"

#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_LIBC 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_CTYPE_H 1
#define HAVE_MATH_H 1
#define HAVE_FLOAT_H 1
#define HAVE_LIMITS_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1
#define HAVE_QSORT 1
#define HAVE_ABS 1
#define HAVE_MEMSET 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_STRLEN 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_STRTOD 1
#define HAVE_ATOI 1
#define HAVE_ATOF 1
#define HAVE_STRCMP 1
#define HAVE_STRNCMP 1
#define HAVE_VSNPRINTF 1
#define HAVE_SQRT 1
#define HAVE_SQRTF 1
#define HAVE_FLOOR 1
#define HAVE_FLOORF 1
#define HAVE_CEIL 1
#define HAVE_CEILF 1
#define HAVE_FABS 1
#define HAVE_FABSF 1
#define HAVE_SIN 1
#define HAVE_SINF 1
#define HAVE_COS 1
#define HAVE_COSF 1
#define HAVE_POW 1
#define HAVE_POWF 1
#define HAVE_LOG 1
#define HAVE_LOGF 1
#define HAVE_EXP 1
#define HAVE_EXPF 1
#define HAVE_FMOD 1
#define HAVE_FMODF 1
#define HAVE_ATAN2 1
#define HAVE_ATAN2F 1
#define HAVE_LROUND 1
#define HAVE_LROUNDF 1
#define HAVE_ROUND 1
#define HAVE_ROUNDF 1
#define HAVE_TRUNC 1
#define HAVE_TRUNCF 1

#ifdef __GNUC__
#define HAVE_GCC_SYNC_LOCK_TEST_AND_SET 1
#define HAVE_GCC_ATOMICS 1
#endif

/* No dlopen, so no dynamic API (see src/dynapi/SDL_dynapi.h). */
#define DYNAPI_NEEDS_DLOPEN 1

/* Blitters in plain C (SDL's AltiVec ones aren't needed and VMX128 differs). */
#define SDL_ALTIVEC_BLITTERS 0

/* The drivers in platform/xenon/sdl replace these dummy ones. */
#define SDL_VIDEO_DRIVER_DUMMY 1
#define SDL_AUDIO_DRIVER_DUMMY 1
#define SDL_FILESYSTEM_DUMMY 1
#define SDL_VIDEO_RENDER_SW 1

#define SDL_JOYSTICK_DISABLED 1
#define SDL_HAPTIC_DISABLED 1
#define SDL_HIDAPI_DISABLED 1
#define SDL_SENSOR_DISABLED 1
#define SDL_LOADSO_DISABLED 1
#define SDL_THREADS_DISABLED 1
#define SDL_POWER_DISABLED 1

#endif /* SDL_config_h_ */
