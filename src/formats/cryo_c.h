/*
 * Freestanding C decoders for MEGARACE.DAT, HSQ and HNM (1).
 *
 * The same formats as formats/cryo.h, written for small machines (the Sega
 * Saturn build): no allocation, no exceptions, no C++ library. The caller
 * owns every buffer. Each function returns a negative CRYO_E* code on
 * malformed or truncated input and never reads or writes out of bounds.
 * tests/test_formats.cpp checks these against the C++ decoders.
 *
 * Format notes: docs/formats.md.
 */
#ifndef MEGARACE_CRYO_C_H
#define MEGARACE_CRYO_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CRYO_OK          = 0,
    CRYO_E_FORMAT    = -1,   /* not this format */
    CRYO_E_TRUNCATED = -2,   /* input ends early */
    CRYO_E_CORRUPT   = -3,   /* bad back-reference, codebook exhausted, ... */
    CRYO_E_SPACE     = -4    /* an output or scratch buffer is too small */
};

/* ---- MEGARACE.DAT ------------------------------------------------------ */
/* u16 count, then count 25-byte entries: char name[16], u32 size, u32 offset,
 * u8 flag. Unused trailing slots have an empty name.                        */

#define CRYO_DAT_ENTRY_SIZE 25

typedef struct {
    char     name[17];
    uint32_t size;
    uint32_t offset;   /* absolute, into MEGARACE.DAT */
} cryo_dat_entry;

/* Number of directory slots, from the file's first two bytes. */
unsigned cryo_dat_count(const uint8_t head[2]);
/* Parses `count` slots from `dir` (n bytes); returns the number of named
 * entries written to `out` (at most max).                                  */
int cryo_dat_parse(const uint8_t* dir, size_t n, unsigned count,
                   cryo_dat_entry* out, int max);
/* Case-insensitive lookup; returns the index or -1. */
int cryo_dat_find(const cryo_dat_entry* e, int n, const char* name);

/* ---- HSQ --------------------------------------------------------------- */

int  cryo_hsq_is(const uint8_t* d, size_t n);
/* Declared unpacked size from the header (no checks beyond n >= 2). */
unsigned cryo_hsq_size(const uint8_t* d, size_t n);
/* Unpacks into out (cap bytes); returns the unpacked length or a CRYO_E*. */
long cryo_hsq_unpack(const uint8_t* d, size_t n, uint8_t* out, size_t cap);

/* ---- HNM (1) ----------------------------------------------------------- */

typedef struct {
    uint8_t pal[768];     /* 8-bit RGB, as the C++ decoder expands the 6-bit values */
    int     sound_rate;   /* 0 until the VOC header has been seen */
    int     voc_seen;
    /* Palette entries changed by the last cryo_hnm_header/decode call. */
    int     pal_first, pal_last;   /* pal_last < pal_first: none */
} cryo_hnm_state;

/* Parses the file header (palette). `d` must hold the first n bytes of the
 * file; returns the header size (offset of the first superchunk) or a
 * CRYO_E*. If n is smaller than the header, returns CRYO_E_TRUNCATED and
 * *need (if not null) is set to the header size.                            */
long cryo_hnm_header(cryo_hnm_state* st, const uint8_t* d, size_t n, size_t* need);

/* Size of the superchunk starting at sc[0..1] (its u16 size field). */
unsigned cryo_hnm_superchunk_size(const uint8_t sc[2]);

#define CRYO_HNM_MAX_SOUND 4

typedef struct {
    int w, h, x, y;
    int overlay;          /* mode 0xFF: index 0 is transparent */
    int has_video;
    int palette_changed;
    int subtitle_cue;     /* 0x40 next line, 0x20 clear, 0 none */
    /* Sound for this frame: 8-bit unsigned PCM pieces, pointing into the
     * superchunk (the VOC headers already skipped).                         */
    int            sound_count;
    const uint8_t* sound[CRYO_HNM_MAX_SOUND];
    size_t         sound_len[CRYO_HNM_MAX_SOUND];
    /* Track data chunks, pointing into the superchunk (null if absent). */
    const uint8_t* pt; size_t pt_len;
    const uint8_t* kl; size_t kl_len;
} cryo_hnm_frame;

/* Scratch space cryo_hnm_decode may need: the codebook (up to 64 KiB) or an
 * HSQ frame's unpacked bytes (up to 64 KiB).                                */
#define CRYO_HNM_SCRATCH (65536u + 16u)

/* Decodes one superchunk `sc` (n bytes, starting with its u16 size).
 * pixels receives w*h indices (cap bytes); scratch is CRYO_HNM_SCRATCH
 * bytes. Returns CRYO_OK or a CRYO_E*. A frame with no picture returns
 * CRYO_OK with has_video 0.                                                 */
int cryo_hnm_decode(cryo_hnm_state* st, const uint8_t* sc, size_t n,
                    cryo_hnm_frame* f, uint8_t* pixels, size_t cap,
                    uint8_t* scratch, size_t scratch_cap);

/* Only the typed chunks of a superchunk (palette, sound, pt, kl), without
 * the picture: cheap, for reading sound ahead of the picture. Returns the
 * offset of the picture data in sc, or a CRYO_E*.                           */
long cryo_hnm_chunks(cryo_hnm_state* st, const uint8_t* sc, size_t n, cryo_hnm_frame* f);

/* Starts a video over (the next decode is frame 0 again: sound header). */
void cryo_hnm_rewind(cryo_hnm_state* st);

#ifdef __cplusplus
}
#endif

#endif
