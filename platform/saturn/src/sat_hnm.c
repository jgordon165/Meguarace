/*
 * HNM playback on the Saturn, streamed from MEGARACE.DAT.
 *
 * As the PC player (scripts/scenes/player.lua): a video with a soundtrack
 * is clocked by its sound, each frame shown once the sound queued before it
 * has played; a silent one (the track videos) runs at 12.5 fps. Overlay
 * frames build on the previous picture with index 0 transparent.
 *
 * Sound is read ahead of the picture: every superchunk already in the CD
 * buffer has its sound queued (up to the ring's three seconds), so the
 * blocking CD reads that refill the buffer never starve the SCSP.
 */
#include <yaul.h>

#include <string.h>

#include "formats/cryo_c.h"
#include "sat_audio.h"
#include "sat_cd.h"
#include "sat_hnm.h"
#include "sat_mem.h"
#include "sat_pad.h"
#include "sat_video.h"

#define SILENT_FPS_X2 25   /* 12.5 fps, doubled to stay in integers */

static uint8_t _pixels[SAT_W * SAT_H] __aligned(16);
static uint8_t _scratch[CRYO_HNM_SCRATCH] __aligned(16);
static sat_hnm_stats _stats;

const sat_hnm_stats* sat_hnm_last(void) { return &_stats; }

typedef struct {
    sat_reader r;
    cryo_hnm_state vid, snd;   /* the picture's state; the sound reader's own */
    uint32_t snd_pos;          /* next superchunk whose sound isn't queued yet */
    int sound;                 /* the video has a soundtrack */
    int started;               /* its stream is playing */
} player;

/* Queues the sound of every buffered superchunk ahead, while it fits. */
static void _sound_ahead(player* p) {
    for (;;) {
        const uint8_t* h = sat_reader_buffered(&p->r, p->snd_pos, 2);
        if (!h) return;
        const unsigned sz = cryo_hnm_superchunk_size(h);
        if (sz < 2) return;
        const uint8_t* sc = sat_reader_buffered(&p->r, p->snd_pos, sz);
        if (!sc) return;
        cryo_hnm_state save = p->snd;
        cryo_hnm_frame f;
        if (cryo_hnm_chunks(&p->snd, sc, sz, &f) < 0) { p->snd_pos += sz; continue; }
        size_t total = 0;
        for (int i = 0; i < f.sound_count; i++) total += f.sound_len[i];
        if (total) {
            if (!p->sound) {
                p->sound = 1;
                sat_stream_begin(p->snd.sound_rate);
            }
            if (total > sat_stream_space()) { p->snd = save; return; }   /* try again later */
            for (int i = 0; i < f.sound_count; i++) sat_stream_push(f.sound[i], f.sound_len[i]);
        }
        p->snd_pos += sz;
    }
}

static void _blit(const cryo_hnm_frame* f) {
    const int key = f->overlay;
    for (int y = 0; y < f->h; y++) {
        const int sy = f->y + y;
        if (sy < 0 || sy >= SAT_H) continue;
        const uint8_t* src = _pixels + y * f->w;
        uint8_t* dst = sat_screen + sy * SAT_W;
        int x0 = f->x, x1 = f->x + f->w;
        int from = 0;
        if (x0 < 0) { from = -x0; x0 = 0; }
        if (x1 > SAT_W) x1 = SAT_W;
        if (x1 <= x0) continue;
        if (!key) {
            memcpy(dst + x0, src + from, (size_t)(x1 - x0));
        } else {
            for (int x = x0, i = from; x < x1; x++, i++)
                if (src[i]) dst[x] = src[i];
        }
    }
}

/* Waits (feeding the sound) until frame `k` is due; returns a poll result. */
static int _wait(player* p, uint32_t sound_before, uint32_t t0, int k, sat_hnm_poll poll) {
    int first = 1;
    for (;;) {
        if (p->sound && !p->started) { sat_stream_start(); p->started = 1; }
        if (p->sound) {
            /* One vblank early: the flip itself waits for the next one. */
            const uint32_t lead = (uint32_t)sat_stream_rate() / (uint32_t)sat_vblank_rate();
            if (sat_stream_played() + lead >= sound_before) return 0;
        } else {
            const uint32_t due = t0 + (uint32_t)k * (uint32_t)sat_vblank_rate() * 2u / SILENT_FPS_X2;
            if (sat_vblanks() >= due) {
                if (first && sat_vblanks() > due + 2) _stats.late++;
                return 0;
            }
        }
        first = 0;
        sat_reader_pump(&p->r);
        _sound_ahead(p);
        if (p->sound && !p->started) { sat_stream_start(); p->started = 1; }
        sat_wait_vblank();
        sat_pad_update();
        if (poll) {
            int r = poll();
            if (r) return r;
        }
    }
}

int sat_hnm_play(int entry, sat_hnm_poll poll) {
    static player p;
    memset(&p, 0, sizeof p);
    memset(&_stats, 0, sizeof _stats);
    if (!sat_dat_entry(entry)) return -1;
    sat_reader_open(&p.r, entry, SAT_STREAM_BUF, SAT_STREAM_BUF_SIZE);

    const uint8_t* h = sat_reader_peek(&p.r, 4);
    if (!h) { sat_reader_close(&p.r); return -2; }
    size_t need = 0;
    long hs = cryo_hnm_header(&p.vid, h, 4, &need);
    if (hs == CRYO_E_TRUNCATED) {
        if (!(h = sat_reader_peek(&p.r, (uint32_t)need))) { sat_reader_close(&p.r); return -2; }
        hs = cryo_hnm_header(&p.vid, h, need, NULL);
    }
    if (hs < 0) { sat_reader_close(&p.r); return -3; }
    p.snd = p.vid;
    sat_reader_skip(&p.r, (uint32_t)hs);
    p.snd_pos = (uint32_t)hs;

    sat_stream_stop();
    sat_video_clear();
    sat_video_palette(p.vid.pal, 0, 255);

    uint32_t sound_before = 0;     /* sound bytes in the frames before this one */
    uint32_t t0 = sat_vblanks();
    int result = SAT_HNM_DONE;
    for (int k = 0; p.r.pos + 2 <= p.r.size; k++) {
        const uint8_t* sc = sat_reader_peek(&p.r, 2);
        if (!sc) break;
        const unsigned sz = cryo_hnm_superchunk_size(sc);
        if (sz < 2 || !(sc = sat_reader_peek(&p.r, sz))) break;
        _sound_ahead(&p);

        cryo_hnm_frame f;
        int rc = cryo_hnm_decode(&p.vid, sc, sz, &f, _pixels, sizeof _pixels, _scratch, sizeof _scratch);
        uint32_t here = 0;
        for (int i = 0; i < f.sound_count; i++) here += (uint32_t)f.sound_len[i];
        if (rc != CRYO_OK) _stats.errors++;

        if (k == 0) t0 = sat_vblanks();
        int r = _wait(&p, sound_before, t0, k, poll);
        if (r) { result = r; break; }

        if (rc == CRYO_OK) {
            if (f.palette_changed && p.vid.pal_last >= p.vid.pal_first)
                sat_video_palette(p.vid.pal, p.vid.pal_first, p.vid.pal_last);
            if (f.has_video) _blit(&f);
        }
        sat_video_present();
        sat_pad_update();
        if (poll && (r = poll())) { result = r; break; }

        sound_before += here;
        _stats.frames++;
        sat_reader_skip(&p.r, sz);
    }

    if (result == SAT_HNM_DONE && p.sound) {
        /* Let the soundtrack finish. */
        while (sat_stream_played() < sat_stream_queued()) {
            sat_wait_vblank();
            sat_pad_update();
            if (poll && (result = poll())) break;
        }
    }
    _stats.rate = p.snd.sound_rate;
    _stats.stalls = (int)p.r.stalls;
    sat_reader_close(&p.r);
    sat_stream_stop();
    return result;
}
