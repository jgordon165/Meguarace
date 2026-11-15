/* Saturn CD access: MEGARACE.DAT on the disc, and streaming from it. */
#ifndef SAT_CD_H
#define SAT_CD_H

#include <stddef.h>
#include <stdint.h>

#include "formats/cryo_c.h"

#define SAT_DAT_MAX 512

int sat_cd_init(void);   /* 0 on success */
/* Finds a file in the disc's root directory; 0 if found. */
int sat_cd_find(const char* name, uint32_t* fad, uint32_t* size);
/* Reads n bytes at byte `off` of the file starting at `fad`. 0 on success. */
int sat_cd_read(uint32_t fad, uint32_t off, void* buf, uint32_t n);

/* MEGARACE.DAT */
int sat_dat_open(void);  /* 0 on success */
int sat_dat_count(void);
const cryo_dat_entry* sat_dat_entry(int i);
int sat_dat_find(const char* name);   /* index or -1 */
int sat_dat_read(int entry, uint32_t off, void* buf, uint32_t n);

/* Streaming reader over one archive entry: the drive reads ahead into the
 * CD block while the game runs. One stream at a time, forward only; don't
 * call sat_cd_read/sat_dat_read while one is open (close it first).      */
typedef struct {
    uint32_t base;       /* entry offset inside MEGARACE.DAT */
    uint32_t size;       /* entry size */
    uint32_t pos;        /* entry-relative position */
    uint32_t buf_start;  /* absolute (DAT) offset of buf[0], sector-aligned */
    uint32_t buf_len;
    uint32_t next;       /* absolute offset of the next sector to arrive */
    uint8_t* buf;
    uint32_t cap;
    uint32_t stalls;     /* times the game had to wait for the drive */
} sat_reader;

void sat_reader_open(sat_reader* r, int entry, uint8_t* buf, uint32_t cap);
void sat_reader_close(sat_reader* r);
/* Moves sectors that have arrived into the buffer (never waits). */
uint32_t sat_reader_pump(sat_reader* r);
/* Makes n bytes at the current position available (waiting for the drive
 * if they haven't arrived); returns a pointer to them, or NULL (end of
 * entry, read error, or n too large for the buffer).                     */
const uint8_t* sat_reader_peek(sat_reader* r, uint32_t n);
void sat_reader_skip(sat_reader* r, uint32_t n);
/* n bytes at entry-relative `pos` if they are already buffered (no CD
 * read), else NULL.                                                      */
const uint8_t* sat_reader_buffered(const sat_reader* r, uint32_t pos, uint32_t n);

#endif
