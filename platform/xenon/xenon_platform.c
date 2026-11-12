/* The Xbox 360 side of xenon_platform.h, on LibXenon (Free60). */
#include "xenon_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <console/console.h>
#include <diskio/ata.h>
#include <input/input.h>
#include <ppc/cache.h>
#include <ppc/timebase.h>
#include <time/time.h>
#include <usb/usbmain.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_sound/sound.h>
#include <xenos/xenos.h>
#include <libfat/fat.h>

extern int bdev_enum(int handle, const char **name);
/* LibXenon's console prints to the TV through this; once the game draws,
 * it's turned off so log lines don't scribble over the picture. */
extern void (*stdout_hook)(const char *buf, int len);

/* Where the GPU scans out from (as LibXenon's console finds it). */
struct ati_info {
    uint32_t unknown1[4];
    uint32_t base;
    uint32_t unknown2[8];
    uint32_t width;
    uint32_t height;
} __attribute__((__packed__));

static int started;
static char root[64] = "uda0:/meguarace/";
static uint32_t *fb;
static int fb_w, fb_h, fb_pitch_tiles;   /* visible size; rows of 32-pixel tiles */
static int *col_off;                      /* per x: offset inside a tile row */

static int has_port(const char *dir)
{
    char p[96];
    struct stat st;
    snprintf(p, sizeof p, "%sscripts/main.lua", dir);
    return stat(p, &st) == 0;
}

/* The first USB / hard drive with a meguarace folder holding the scripts. */
static void find_root(void)
{
    static const char *folders[] = { "meguarace", "Meguarace", "MEGUARACE" };
    int tries;
    for (tries = 0; tries < 30; ++tries) {
        int h = 0;
        const char *name;
        while ((h = bdev_enum(h, &name)) >= 0) {
            size_t i;
            for (i = 0; i < sizeof folders / sizeof *folders; ++i) {
                char dir[64];
                snprintf(dir, sizeof dir, "%s:/%s/", name, folders[i]);
                if (has_port(dir)) {
                    strcpy(root, dir);
                    printf(" * Meguarace found at %s\n", root);
                    return;
                }
            }
        }
        usb_do_poll();      /* USB sticks can take a moment to appear */
        mdelay(100);
    }
    printf(" ! No meguarace folder with scripts/ on any USB stick; trying %s\n", root);
}

int xp_init(void)
{
    struct ati_info *ai;
    int x;
    if (started) return 0;
    started = 1;
    xenos_init(VIDEO_MODE_AUTO);
    console_init();
    xenon_make_it_faster(XENON_SPEED_FULL);
    printf("Meguarace for the Xbox 360\n");
    usb_init();
    usb_do_poll();
    xenon_ata_init();
    fatInitDefault();
    find_root();

    ai = (struct ati_info *)0xec806100ULL;
    fb = (uint32_t *)(long)(ai->base | 0x80000000);
    fb_w = (int)ai->width;
    fb_h = (int)ai->height;
    fb_pitch_tiles = (fb_w + 31) >> 5;
    col_off = (int *)malloc(sizeof(int) * fb_w);
    for (x = 0; x < fb_w; ++x) col_off[x] = ((x >> 5) << 10) + (x & 3) + (((x & 31) >> 2) << 3);
    return 0;
}

const char *xp_root(void) { return root; }

void xp_display_size(int *w, int *h)
{
    xp_init();
    *w = fb_w;
    *h = fb_h;
}

/* The framebuffer is in 32x32 tiles, each pixel stored as bytes B, G, R, x
 * (LibXenon console_pset32). */
void xp_present(const uint32_t *argb, int w, int h, int pitch)
{
    int x0 = (fb_w - w) / 2, y0 = (fb_h - h) / 2, y;
    if (!fb) return;
    stdout_hook = NULL;
    if (w > fb_w) w = fb_w;
    if (h > fb_h) h = fb_h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    for (y = 0; y < h; ++y) {
        const int ty = y + y0;
        const uint32_t *src = (const uint32_t *)((const uint8_t *)argb + (size_t)y * pitch);
        const int rowbase = (ty >> 5) * 32 * fb_pitch_tiles * 32 + ((ty & 1) << 2) + (((ty & 31) >> 1) << 6);
        const int flip = (ty & 8) << 2;
        int x;
        for (x = 0; x < w; ++x) fb[(rowbase + col_off[x + x0]) ^ flip] = __builtin_bswap32(src[x]);
    }
    memdcbst(fb, fb_pitch_tiles * 32 * ((fb_h + 31) & ~31) * 4);
}

void xp_poll(void) { usb_do_poll(); }

/* LibXenon hands out a report only when a new one has come in, so the last
 * one is kept per port. */
static xp_pad pads[4];

void xp_get_pad(int port, xp_pad *out)
{
    struct controller_data_s c;
    if (port < 0 || port >= 4) {
        memset(out, 0, sizeof *out);
        return;
    }
    memset(&c, 0, sizeof c);
    if (get_controller_data(&c, port)) {
        xp_pad *p = &pads[port];
        p->connected = 1;
        p->up = c.up; p->down = c.down; p->left = c.left; p->right = c.right;
        p->a = c.a; p->b = c.b; p->x = c.x; p->y = c.y;
        p->lb = c.lb; p->rb = c.rb; p->start = c.start; p->back = c.back; p->logo = c.logo;
        p->ls = c.s1_z; p->rs = c.s2_z;
        p->lx = c.s1_x; p->ly = c.s1_y; p->rx = c.s2_x; p->ry = c.s2_y;
        p->lt = c.lt; p->rt = c.rt;
    }
    *out = pads[port];
}

uint64_t xp_counter(void) { return mftb(); }
uint64_t xp_frequency(void) { return PPC_TIMEBASE_FREQ; }

void xp_delay_ms(uint32_t ms)
{
    const uint64_t end = mftb() + (uint64_t)ms * (PPC_TIMEBASE_FREQ / 1000);
    while (mftb() < end) {
    }
}

static int audio_started;
void xp_audio_init(void)
{
    if (audio_started) return;
    audio_started = 1;
    xenon_sound_init();
}

int xp_audio_free(void) { return audio_started ? xenon_sound_get_free() : 0; }

void xp_audio_submit(const void *s16le, int bytes)
{
    if (audio_started) xenon_sound_submit((void *)s16le, bytes);
}

void xp_log(const char *msg) { printf("%s\n", msg); }

/* newlib's clock() (Lua's os.clock) asks for times(), which LibXenon lacks:
 * the time base, counted as processor time. */
#include <sys/times.h>
#include <time.h>
clock_t times(struct tms *t)
{
    clock_t c = (clock_t)(mftb() / (PPC_TIMEBASE_FREQ / CLOCKS_PER_SEC));
    if (t) {
        t->tms_utime = c;
        t->tms_stime = 0;
        t->tms_cutime = 0;
        t->tms_cstime = 0;
    }
    return c;
}
