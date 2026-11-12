/*
 * Meguarace for the Sega Saturn.
 *
 * First milestone: the engine's Saturn platform layer. It boots from the
 * disc the disc builder made (tools/saturn_disc.py: this program plus the
 * player's own MEGARACE.DAT), plays the original boot sequence (Mindscape,
 * Cryo, the intro with Lance Boyle) from the archive with its sound, then
 * opens a video explorer: every HNM in MEGARACE.DAT, playable on the
 * console. The Lua game comes next (docs/saturn.md).
 *
 * Pad: A, C or START skips a video, B skips the rest of a sequence.
 * In the explorer: UP/DOWN choose, LEFT/RIGHT page, A plays.
 */
#include <yaul.h>

#include <stdio.h>
#include <string.h>

#include "sat_audio.h"
#include "sat_cd.h"
#include "sat_hnm.h"
#include "sat_pad.h"
#include "sat_video.h"

#define ESC_CLEAR "\x1b[H\x1b[2J"

static int _cart_mib;   /* 0, 1 or 4: the RAM cartridge found at boot */
static int _videos[SAT_DAT_MAX];
static int _video_count;

static void _text_clear(void) {
    dbgio_puts(ESC_CLEAR);
    dbgio_flush();
}

static int _poll_video(void) {
    const uint32_t p = sat_pad_pressed();
    if (p & SAT_PAD_B) return SAT_HNM_SKIP_ALL;
    if (p & (SAT_PAD_A | SAT_PAD_C | SAT_PAD_START)) return SAT_HNM_SKIPPED;
    return 0;
}

static void _halt(const char* why, const char* detail) {
    sat_video_clear();
    dbgio_puts(ESC_CLEAR);
    dbgio_printf("\n  MEGUARACE for SEGA SATURN\n\n  %s\n\n%s", why, detail);
    dbgio_flush();
    for (;;) sat_wait_vblank();
}

static int _play(const char* name) {
    const int e = sat_dat_find(name);
    if (e < 0) return 0;
    _text_clear();
    return sat_hnm_play(e, _poll_video);
}

static void _boot_sequence(void) {
    static const char* const seq[] = {"MINDSCAP.HNM", "CRYO.HNM", "INTRO.HNM"};
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++)
        if (_play(seq[i]) == SAT_HNM_SKIP_ALL) break;
}

static int _is_hnm(const char* n) {
    size_t l = strlen(n);
    return l > 4 && (strcmp(n + l - 4, ".HNM") == 0 || strcmp(n + l - 4, ".hnm") == 0);
}

#define ROWS 18

static void _explorer(void) {
    _video_count = 0;
    for (int i = 0; i < sat_dat_count(); i++)
        if (_is_hnm(sat_dat_entry(i)->name)) _videos[_video_count++] = i;
    int sel = 0, top = 0, last = -1, last_rc = 0;
    for (;;) {
        sat_video_clear();
        dbgio_puts(ESC_CLEAR);
        dbgio_printf(" MEGUARACE SATURN  video explorer\n");
        dbgio_printf(" RAM 2 MiB%s  %d files %d videos\n\n",
                     _cart_mib == 4 ? " + 4 MiB cart" : _cart_mib == 1 ? " + 1 MiB cart" : "",
                     sat_dat_count(), _video_count);
        if (sel < top) top = sel;
        if (sel >= top + ROWS) top = sel - ROWS + 1;
        for (int r = 0; r < ROWS && top + r < _video_count; r++) {
            const cryo_dat_entry* e = sat_dat_entry(_videos[top + r]);
            dbgio_printf(" %c %-13s %7lu KiB\n", top + r == sel ? '>' : ' ', e->name,
                         (unsigned long)(e->size / 1024));
        }
        dbgio_printf("\n");
        if (last >= 0) {
            const sat_hnm_stats* s = sat_hnm_last();
            dbgio_printf(" %s: %d frames, %d Hz", sat_dat_entry(last)->name, s->frames, s->rate);
            if (s->errors) dbgio_printf(", %d bad", s->errors);
            if (s->stalls) dbgio_printf(", %d CD waits", s->stalls);
            if (last_rc < 0) dbgio_printf(", error %d", last_rc);
            dbgio_printf("\n");
        }
        dbgio_printf(" A play  UP/DOWN  LEFT/RIGHT page\n");
        dbgio_flush();
        for (;;) {
            sat_wait_vblank();
            sat_pad_update();
            const uint32_t p = sat_pad_pressed();
            if (!p) continue;
            if (p & SAT_PAD_UP) sel = sel > 0 ? sel - 1 : _video_count - 1;
            else if (p & SAT_PAD_DOWN) sel = sel + 1 < _video_count ? sel + 1 : 0;
            else if (p & SAT_PAD_LEFT) sel = sel >= ROWS ? sel - ROWS : 0;
            else if (p & SAT_PAD_RIGHT) sel = sel + ROWS < _video_count ? sel + ROWS : _video_count - 1;
            else if (p & (SAT_PAD_A | SAT_PAD_C | SAT_PAD_START)) {
                last = _videos[sel];
                _text_clear();
                last_rc = sat_hnm_play(last, _poll_video);
            } else continue;
            break;
        }
    }
}

int main(void) {
    dbgio_init();
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
    dbgio_dev_font_load();

    if (sat_cd_init() != 0)
        _halt("No disc.", "  Build a disc image with\n  tools/saturn_disc.py and your own\n  MegaRace CD (see docs/saturn.md).\n");
    if (sat_dat_open() != 0)
        _halt("MEGARACE.DAT is not on this disc.",
              "  The disc builder copies it from\n  your own MegaRace CD:\n\n"
              "  python tools/saturn_disc.py \\\n    --data <your CD> --out megarace\n");

    _boot_sequence();
    _explorer();
    return 0;
}

void user_init(void) {
    sat_video_init();
    sat_pad_init();
    sat_audio_init();

    dram_cart_init();
    switch (dram_cart_id_get()) {
    case DRAM_CART_ID_4MIB: _cart_mib = 4; break;
    case DRAM_CART_ID_1MIB: _cart_mib = 1; break;
    default:                _cart_mib = 0; break;
    }
}
