/*
 * The game's picture on the Saturn.
 *
 * NBG0 is a 512x256 bitmap in 256-colour palette mode, the same 8-bit
 * indexed picture the DOS game drew, so frames go to the screen unchanged.
 * Two pages (VRAM banks A0 and A1) are swapped at vblank: the game never
 * draws into the page on show.
 *
 * The picture is 200 lines on a 224-line display: 12 black lines above and
 * below. Palette index 0 is a real colour in MegaRace (opaque here), so the
 * border can't be drawn with it: VDP2 window 0 limits NBG0 to the picture's
 * 200 lines, and the lines outside show the black back screen.
 */
#include <yaul.h>

#include <string.h>

#include "sat_video.h"

#define DISP_LINES 224
#define TOP        ((DISP_LINES - SAT_H) / 2)

#define PAGE_ADDR(n)  VDP2_VRAM_ADDR((n), 0x00000)
#define BACK_ADDR     VDP2_VRAM_ADDR(2, 0x00000)

uint8_t sat_screen[SAT_W * SAT_H] __aligned(16);

static int _page;               /* the page on show */
static volatile uint32_t _vblanks;
static int _rate = 60;

static vdp2_scrn_bitmap_format_t _format = {
    .scroll_screen = VDP2_SCRN_NBG0,
    .ccc           = VDP2_SCRN_CCC_PALETTE_256,
    .bitmap_size   = VDP2_SCRN_BITMAP_SIZE_512X256,
    .palette_base  = 0x00000000,
    .bitmap_base   = PAGE_ADDR(0)
};

static void _vblank_in(void* work __unused) { _vblanks++; }

static void _vblank_out(void* work __unused) { smpc_peripheral_intback_issue(); }

static rgb1555_t _rgb(const uint8_t* c) {
    return RGB1555(1, c[0] >> 3, c[1] >> 3, c[2] >> 3);
}

void sat_video_init(void) {
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A,
                              VDP2_TVMD_VERT_224);
    _rate = (MEMORY_READ(16, VDP2(TVSTAT)) & 0x0001) ? 50 : 60;

    vdp2_cram_mode_set(0);
    vdp2_scrn_bitmap_format_set(&_format);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG0, 6);
    /* NBG0 on, index 0 opaque (N0TPON). */
    vdp2_scrn_display_set(VDP2_SCRN_DISP_NBG0);
    vdp2_scrn_back_color_set(BACK_ADDR, RGB1555(1, 0, 0, 0));
    /* Window 0 over the picture's lines; NBG0 drawn inside it only (W0E,
     * W0A). Horizontal window coordinates are in half pixels here. */
    vdp2_ioregs_t* regs = vdp2_regs_get();
    regs->wpsx0 = 0;
    regs->wpex0 = (SAT_W - 1) << 1;
    regs->wpsy0 = TOP;
    regs->wpey0 = TOP + SAT_H - 1;
    regs->wctla = (regs->wctla & 0xFF00) | 0x0003;
    vdp2_scrn_scroll_x_set(VDP2_SCRN_NBG0, FIX16(0));
    vdp2_scrn_scroll_y_set(VDP2_SCRN_NBG0, FIX16(-TOP));

    /* 8-bit bitmaps take two access slots; give NBG0 four in both pages. */
    vdp2_vram_cycp_t cycp;
    memset(&cycp, 0, sizeof cycp);
    for (int bank = 0; bank < 4; bank++) {
        vdp2_vram_cycp_bank_t* b = &cycp.pt[bank];
        b->t0 = b->t1 = b->t2 = b->t3 = b->t4 = b->t5 = b->t6 = b->t7 = VDP2_VRAM_CYCP_NO_ACCESS;
    }
    for (int bank = 0; bank < 2; bank++) {
        vdp2_vram_cycp_bank_t* b = &cycp.pt[bank];
        b->t0 = b->t1 = b->t2 = b->t3 = VDP2_VRAM_CYCP_CHPNDR_NBG0;
    }
    vdp2_vram_cycp_set(&cycp);

    memset((void*)PAGE_ADDR(0), 0, 512 * 256);
    memset((void*)PAGE_ADDR(1), 0, 512 * 256);
    sat_video_clear();

    vdp_sync_vblank_in_set(_vblank_in, NULL);
    vdp_sync_vblank_out_set(_vblank_out, NULL);
    vdp2_tvmd_display_set();
    vdp2_sync();
    vdp2_sync_wait();
}

int sat_vblank_rate(void) { return _rate; }
uint32_t sat_vblanks(void) { return _vblanks; }

void sat_wait_vblank(void) {
    vdp2_sync();
    vdp2_sync_wait();
}

void sat_video_palette(const uint8_t* pal, int first, int last) {
    if (first < 0) first = 0;
    if (last > 255) last = 255;
    volatile uint16_t* cram = (volatile uint16_t*)VDP2_CRAM_ADDR(0);
    for (int i = first; i <= last; i++) cram[i] = _rgb(pal + i * 3).raw;
}

void sat_video_clear(void) {
    memset(sat_screen, 0, sizeof sat_screen);
    volatile uint16_t* cram = (volatile uint16_t*)VDP2_CRAM_ADDR(0);
    for (int i = 0; i < 256; i++) cram[i] = RGB1555(1, 0, 0, 0).raw;
}

void sat_video_present(void) {
    const int back = _page ^ 1;
    uint8_t* dst = (uint8_t*)PAGE_ADDR(back);
    const uint8_t* src = sat_screen;
    for (int y = 0; y < SAT_H; y++, dst += 512, src += SAT_W) memcpy(dst, src, SAT_W);
    _format.bitmap_base = PAGE_ADDR(back);
    vdp2_scrn_bitmap_base_set(&_format);
    _page = back;
    vdp2_sync();
    vdp2_sync_wait();
}
