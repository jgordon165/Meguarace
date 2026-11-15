/* xenon_platform.h on xsdk (openpak/x360-sdk): the Xbox 360 as a normal
 * title, a .xex launched from the dashboard, Aurora or a file manager on a
 * console that runs unsigned code (BadUpdate + FreeMyXe, RGH, JTAG).
 *
 * The picture: two 1280x720 front buffers the CPU fills in the tiled layout
 * the display reads (each pixel stored B, G, R, A), shown with
 * xsdk_display_show as xsdk's splash does. The sound: the kernel's render
 * driver asks for 256 frames at a time, 6 channels of floats one after the
 * other; a ring of 16-bit stereo, filled from the game's thread, feeds it.
 * Files: newlib paths from "game:\", the title's own folder. */
#include "xenon_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsdk/display.h"
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/thread.h"

/* In libxsdk.a, declared in its private gpu_private.h: the byte offset of
 * texel (x, y) in Xenos's tiled layout. */
extern uint32_t xsdk_tiled_2d(uint32_t x, uint32_t y, uint32_t pitch_texels, uint32_t bytes_per_texel_log2);

static int started;
static uint32_t tv_w = 1280, tv_h = 720;
static volatile uint32_t *buffers[2];
static uint32_t *staging;          /* the next frame, tiled, in cached memory */
static uint32_t *tile_index;       /* per linear pixel, its word in a tiled buffer */
static uint32_t buffer_words;
static int next_buffer;
static int display_up;

static uint64_t time_base(void)
{
    uint32_t upper, lower, check;
    do {
        __asm__ volatile("mftbu %0" : "=r"(upper));
        __asm__ volatile("mftb %0" : "=r"(lower));
        __asm__ volatile("mftbu %0" : "=r"(check));
    } while (upper != check);
    return (uint64_t)upper << 32 | lower;
}

int xp_init(void)
{
    uint32_t w = 0, h = 0, x, y;
    if (started) return 0;
    started = 1;
    xsdk_log_open();
    xsdk_log_write("Meguarace for the Xbox 360 (xsdk)");
    if (!NT_SUCCESS(xsdk_gpu_open())) xsdk_log_write("gpu open failed");
    if (NT_SUCCESS(xsdk_display_open(XSDK_DISPLAY_720P, &w, &h)) && w && h) {
        tv_w = w;
        tv_h = h;
        display_up = 1;
    } else {
        xsdk_log_write("display open failed");
    }
    buffer_words = xsdk_gpu_resolve_bytes(tv_w, tv_h) / 4;
    buffers[0] = xsdk_gpu_alloc(buffer_words * 4);
    buffers[1] = xsdk_gpu_alloc(buffer_words * 4);
    staging = (uint32_t *)calloc(buffer_words, 4);
    tile_index = (uint32_t *)malloc(sizeof(uint32_t) * tv_w * tv_h);
    if (!buffers[0] || !buffers[1] || !staging || !tile_index) {
        xsdk_log_write("out of memory for the picture");
        display_up = 0;
        return 0;
    }
    for (y = 0; y < tv_h; ++y)
        for (x = 0; x < tv_w; ++x) tile_index[y * tv_w + x] = xsdk_tiled_2d(x, y, tv_w, 2) >> 2;
    xsdk_log_write("display %ux%u", (unsigned)tv_w, (unsigned)tv_h);
    return 0;
}

const char *xp_root(void) { return "game:/"; }

void xp_display_size(int *w, int *h)
{
    xp_init();
    *w = (int)tv_w;
    *h = (int)tv_h;
}

void xp_present(const uint32_t *argb, int w, int h, int pitch)
{
    volatile uint32_t *dst;
    uint32_t i;
    int y;
    if (!display_up) return;
    if (w > (int)tv_w) w = (int)tv_w;
    if (h > (int)tv_h) h = (int)tv_h;
    for (y = 0; y < h; ++y) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)argb + (size_t)y * pitch);
        const uint32_t *idx = tile_index + (size_t)y * tv_w;
        int x;
        for (x = 0; x < w; ++x) staging[idx[x]] = __builtin_bswap32(src[x] | 0xFF000000u);
    }
    /* Write-combined memory: one pass of 32-bit stores, in order. */
    dst = buffers[next_buffer];
    for (i = 0; i < buffer_words; ++i) dst[i] = staging[i];
    __asm__ volatile("sync" ::: "memory");
    xsdk_display_show(xsdk_gpu_address(dst), tv_w);
    next_buffer ^= 1;
}

static xp_pad pads[4];

void xp_poll(void)
{
    int port;
    for (port = 0; port < 4; ++port) {
        INPUT_STATE s;
        xp_pad *p = &pads[port];
        memset(&s, 0, sizeof s);
        if (XamInputGetState((uint32_t)port, 0, &s) != ERROR_SUCCESS) {
            memset(p, 0, sizeof *p);
            continue;
        }
        p->connected = 1;
        p->up = !!(s.gamepad.buttons & INPUT_GAMEPAD_DPAD_UP);
        p->down = !!(s.gamepad.buttons & INPUT_GAMEPAD_DPAD_DOWN);
        p->left = !!(s.gamepad.buttons & INPUT_GAMEPAD_DPAD_LEFT);
        p->right = !!(s.gamepad.buttons & INPUT_GAMEPAD_DPAD_RIGHT);
        p->start = !!(s.gamepad.buttons & INPUT_GAMEPAD_START);
        p->back = !!(s.gamepad.buttons & INPUT_GAMEPAD_BACK);
        p->ls = !!(s.gamepad.buttons & INPUT_GAMEPAD_LEFT_THUMB);
        p->rs = !!(s.gamepad.buttons & INPUT_GAMEPAD_RIGHT_THUMB);
        p->lb = !!(s.gamepad.buttons & INPUT_GAMEPAD_LEFT_SHOULDER);
        p->rb = !!(s.gamepad.buttons & INPUT_GAMEPAD_RIGHT_SHOULDER);
        p->logo = !!(s.gamepad.buttons & INPUT_GAMEPAD_GUIDE);
        p->a = !!(s.gamepad.buttons & INPUT_GAMEPAD_A);
        p->b = !!(s.gamepad.buttons & INPUT_GAMEPAD_B);
        p->x = !!(s.gamepad.buttons & INPUT_GAMEPAD_X);
        p->y = !!(s.gamepad.buttons & INPUT_GAMEPAD_Y);
        p->lt = s.gamepad.left_trigger;
        p->rt = s.gamepad.right_trigger;
        p->lx = s.gamepad.thumb_lx;
        p->ly = s.gamepad.thumb_ly;
        p->rx = s.gamepad.thumb_rx;
        p->ry = s.gamepad.thumb_ry;
    }
}

void xp_get_pad(int port, xp_pad *out)
{
    if (port < 0 || port >= 4) memset(out, 0, sizeof *out);
    else *out = pads[port];
}

uint64_t xp_counter(void) { return time_base(); }
uint64_t xp_frequency(void) { return 49875000ull; }   /* xsdk src/time.c: the CPU's 3192 MHz over 64 */
void xp_delay_ms(uint32_t ms) { xsdk_thread_sleep(ms); }

/* ---- sound ----------------------------------------------------------------- */

enum { FRAME_SAMPLES = 256, FRAME_CHANNELS = 6, RING_FRAMES = 16384 };   /* the ring: 0.34 s */
static int16_t ring[RING_FRAMES * 2];
static volatile uint32_t ring_read, ring_write;   /* in frames, counting up */
static float *render_frame;
static uint32_t driver;

void xp_audio_callback(const uint32_t *context);
void xp_audio_callback(const uint32_t *context)
{
    const uint32_t d = __atomic_load_n(&driver, __ATOMIC_ACQUIRE);
    uint32_t r = __atomic_load_n(&ring_read, __ATOMIC_RELAXED);
    const uint32_t w = __atomic_load_n(&ring_write, __ATOMIC_ACQUIRE);
    int i;
    (void)context;
    if (!d) return;
    for (i = 0; i < FRAME_SAMPLES; ++i) {
        float l = 0.0f, rr = 0.0f;
        if (r != w) {
            const int16_t *s = &ring[(r % RING_FRAMES) * 2];
            l = (float)s[0] / 32768.0f;
            rr = (float)s[1] / 32768.0f;
            ++r;
        }
        render_frame[0 * FRAME_SAMPLES + i] = l;   /* front left */
        render_frame[1 * FRAME_SAMPLES + i] = rr;  /* front right */
        render_frame[2 * FRAME_SAMPLES + i] = 0.0f;
        render_frame[3 * FRAME_SAMPLES + i] = 0.0f;
        render_frame[4 * FRAME_SAMPLES + i] = 0.0f;
        render_frame[5 * FRAME_SAMPLES + i] = 0.0f;
    }
    __atomic_store_n(&ring_read, r, __ATOMIC_RELEASE);
    XAudioSubmitRenderDriverFrame(d, render_frame);
}

void xp_audio_init(void)
{
    XAUDIO_RENDER_DRIVER_CLIENT client;
    uint32_t registered = 0;
    if (render_frame) return;
    render_frame = (float *)MmAllocatePhysicalMemoryEx(0, FRAME_SAMPLES * FRAME_CHANNELS * sizeof(float),
                                                        PAGE_READWRITE, 0, UINT32_MAX, 0);
    if (!render_frame) {
        xsdk_log_write("audio: no memory");
        return;
    }
    memset(render_frame, 0, FRAME_SAMPLES * FRAME_CHANNELS * sizeof(float));
    client.callback = xp_audio_callback;
    client.context = 0;
    if (XAudioRegisterRenderDriverClient(&client, &registered) != ERROR_SUCCESS) {
        xsdk_log_write("audio: XAudioRegisterRenderDriverClient failed");
        return;
    }
    __atomic_store_n(&driver, registered, __ATOMIC_RELEASE);
}

int xp_audio_free(void)
{
    const uint32_t used = __atomic_load_n(&ring_write, __ATOMIC_RELAXED) - __atomic_load_n(&ring_read, __ATOMIC_ACQUIRE);
    if (!__atomic_load_n(&driver, __ATOMIC_RELAXED)) return 0;
    return (int)((RING_FRAMES - used) * 4);
}

/* Bytes of 16-bit little-endian stereo (what the SDL driver hands over). */
void xp_audio_submit(const void *s16le, int bytes)
{
    const uint8_t *b = (const uint8_t *)s16le;
    uint32_t w = __atomic_load_n(&ring_write, __ATOMIC_RELAXED);
    const uint32_t r = __atomic_load_n(&ring_read, __ATOMIC_ACQUIRE);
    int frames = bytes / 4, i;
    for (i = 0; i < frames && w - r < RING_FRAMES; ++i, ++w) {
        int16_t *d = &ring[(w % RING_FRAMES) * 2];
        d[0] = (int16_t)(b[4 * i] | b[4 * i + 1] << 8);
        d[1] = (int16_t)(b[4 * i + 2] | b[4 * i + 3] << 8);
    }
    __atomic_store_n(&ring_write, w, __ATOMIC_RELEASE);
}

/* The mixer's lock: the feeder thread mixes while the game queues sounds. */
static RTL_CRITICAL_SECTION mixer_lock;
static int mixer_lock_ready;

void xp_lock(void) { if (mixer_lock_ready) RtlEnterCriticalSection(&mixer_lock); }
void xp_unlock(void) { if (mixer_lock_ready) RtlLeaveCriticalSection(&mixer_lock); }

/* The feeder: a thread on hardware thread 2 (the game runs on 0) that tops
 * the ring up every 2 ms, so a slow frame doesn't starve the sound. */
static void (*feeder_fill)(void);
static volatile int feeder_run;
static HANDLE feeder_thread;

static uint32_t feeder_main(void *context)
{
    (void)context;
    while (__atomic_load_n(&feeder_run, __ATOMIC_ACQUIRE)) {
        feeder_fill();
        xsdk_thread_sleep(2);
    }
    return 0;
}

int xp_audio_start_feeder(void (*fill)(void))
{
    if (feeder_thread) return 1;
    if (!mixer_lock_ready) {
        RtlInitializeCriticalSection(&mixer_lock);
        mixer_lock_ready = 1;
    }
    feeder_fill = fill;
    __atomic_store_n(&feeder_run, 1, __ATOMIC_RELEASE);
    if (!NT_SUCCESS(xsdk_thread_create_on(&feeder_thread, 256 * 1024, feeder_main, NULL, 2))) {
        xsdk_log_write("audio: no feeder thread; filling between frames");
        feeder_thread = 0;
        return 0;
    }
    return 1;
}

void xp_audio_stop_feeder(void)
{
    if (!feeder_thread) return;
    __atomic_store_n(&feeder_run, 0, __ATOMIC_RELEASE);
    NtWaitForSingleObjectEx(feeder_thread, KERNEL_MODE, 0, NULL);
    NtClose(feeder_thread);
    feeder_thread = 0;
}

void xp_log(const char *msg) { xsdk_log_write("%s", msg); }

/* ---- what newlib asks of the system and xsdk leaves out ----------------------
 * system() (Lua's os.execute) wants fork, execve and wait: there are no
 * processes, so they fail. clock() (Lua's os.clock) wants times(): the time
 * base, counted as processor time. */
#include <errno.h>
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>

pid_t fork(void)
{
    errno = ENOSYS;
    return -1;
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    (void)path; (void)argv; (void)envp;
    errno = ENOSYS;
    return -1;
}

pid_t wait(int *status)
{
    (void)status;
    errno = ECHILD;
    return -1;
}

clock_t times(struct tms *t)
{
    const clock_t c = (clock_t)(time_base() / (49875000ull / CLOCKS_PER_SEC));
    if (t) {
        t->tms_utime = c;
        t->tms_stime = 0;
        t->tms_cutime = 0;
        t->tms_cstime = 0;
    }
    return c;
}
