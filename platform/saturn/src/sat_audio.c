/*
 * The videos' soundtracks on the SCSP, driven from the SH-2.
 *
 * The 68000 is switched off: one SCSP slot loops over a ring of 8-bit PCM
 * in sound RAM, and the game writes ahead of it. Slot pitch is set from the
 * stream's rate (the SCSP's base is 44,100 Hz; OCT is the power of two below
 * the rate and FNS the rest, in 1/1024ths).
 *
 * The play position is estimated from vertical blanks since the key-on. The
 * ring holds about three seconds at 11 kHz, so a slow CD read never runs it
 * dry, and it is padded with silence ahead of what's queued: if the game
 * ever falls behind, the slot plays quiet instead of old sound.
 */
#include <yaul.h>

#include <string.h>

#include "sat_audio.h"
#include "sat_video.h"

#define SOUND_RAM       ((volatile uint8_t*)0x25A00000UL)
#define SLOT_REG(s, r)  (*(volatile uint16_t*)SCSP((s) * 0x20 + (r)))
#define COMMON_REG(r)   (*(volatile uint16_t*)SCSP(0x400 + (r)))

#define RING      0x8000u    /* samples (bytes) */
#define PAD       2048u      /* silence kept ahead of the queue */
#define SLOT      0

static int      _rate;
static uint32_t _queued;
static uint32_t _start_vbl;
static int      _playing;

void sat_audio_init(void) {
    smpc_smc_sndoff_call();
    /* Every slot quiet, keyed off. */
    for (int s = 0; s < 32; s++)
        for (int r = 0; r < 0x18; r += 2) SLOT_REG(s, r) = 0;
    SLOT_REG(0, 0x00) = 0x1000;          /* KYONEX: apply (all off) */
    COMMON_REG(0x00) = 0x0200 | 0x000F;  /* MEM4MB (512 KiB), master volume max */
    for (uint32_t i = 0; i < RING; i++) SOUND_RAM[i] = 0;
}

static void _pitch(int rate, uint16_t* reg) {
    /* rate = 44100 * 2^oct * (1 + fns / 1024), oct in -8..7 */
    int oct = 0;
    uint32_t base = 44100;
    if ((uint32_t)rate >= base) {
        while (oct < 7 && (uint32_t)rate >= base * 2) { base *= 2; oct++; }
    } else {
        while (oct > -8 && (uint32_t)rate < base) { base /= 2; oct--; }
    }
    uint32_t fns = (((uint32_t)rate - base) * 1024 + base / 2) / base;
    if (fns > 1023) fns = 1023;
    *reg = (uint16_t)(((oct & 0xF) << 11) | fns);
}

int sat_stream_rate(void) { return _rate; }

void sat_stream_begin(int rate) {
    sat_stream_stop();
    _rate = rate > 0 ? rate : 11111;
    _queued = 0;
    for (uint32_t i = 0; i < RING; i++) SOUND_RAM[i] = 0;
}

uint32_t sat_stream_queued(void) { return _queued; }

uint32_t sat_stream_played(void) {
    if (!_playing) return 0;
    const uint32_t v = sat_vblanks() - _start_vbl;
    return (uint32_t)((uint64_t)v * (uint32_t)_rate / (uint32_t)sat_vblank_rate());
}

size_t sat_stream_space(void) {
    const uint32_t ahead = _queued - (sat_stream_played() < _queued ? sat_stream_played() : _queued);
    const uint32_t room = RING - PAD - 1;
    return ahead >= room ? 0 : room - ahead;
}

size_t sat_stream_push(const uint8_t* pcm, size_t n) {
    const size_t room = sat_stream_space();
    if (n > room) n = room;
    uint32_t w = _queued % RING;
    for (size_t i = 0; i < n; i++) {
        SOUND_RAM[w] = (uint8_t)(pcm[i] ^ 0x80);   /* unsigned -> signed */
        if (++w == RING) w = 0;
    }
    _queued += (uint32_t)n;
    for (uint32_t i = 0; i < PAD; i++) {
        SOUND_RAM[w] = 0;
        if (++w == RING) w = 0;
    }
    return n;
}

void sat_stream_start(void) {
    if (_playing) return;
    uint16_t pitch;
    _pitch(_rate, &pitch);
    SLOT_REG(SLOT, 0x02) = 0;                 /* SA (low) */
    SLOT_REG(SLOT, 0x04) = 0;                 /* LSA */
    SLOT_REG(SLOT, 0x06) = RING - 1;          /* LEA */
    SLOT_REG(SLOT, 0x08) = 0x001F;            /* AR fastest */
    SLOT_REG(SLOT, 0x0A) = 0x3C1F;            /* KRS off, RR fastest */
    SLOT_REG(SLOT, 0x0C) = 0x0100;            /* SDIR: no envelope, TL 0 */
    SLOT_REG(SLOT, 0x0E) = 0;
    SLOT_REG(SLOT, 0x10) = pitch;
    SLOT_REG(SLOT, 0x12) = 0;
    SLOT_REG(SLOT, 0x14) = 0;
    SLOT_REG(SLOT, 0x16) = 0xE000;            /* DISDL 7, centre */
    /* KYONB, normal loop, 8-bit, then KYONEX. */
    SLOT_REG(SLOT, 0x00) = 0x0830;
    _start_vbl = sat_vblanks();
    SLOT_REG(SLOT, 0x00) = 0x1830;
    _playing = 1;
}

void sat_stream_stop(void) {
    if (!_playing) return;
    SLOT_REG(SLOT, 0x00) = 0x0030;            /* KYONB off */
    SLOT_REG(SLOT, 0x00) = 0x1030;            /* KYONEX */
    _playing = 0;
}
