/*
 * CD access on the Saturn: the root directory of the disc the disc builder
 * made (tools/saturn_disc.py), MEGARACE.DAT in it, and a sequential reader
 * that streams an archive entry (an HNM video) a superchunk at a time.
 */
#include <yaul.h>

#include <string.h>

#include "sat_cd.h"
#include "sat_mem.h"
#include "sat_video.h"

#define SECTOR 2048u
#define ROOT_MAX 32

static cdfs_filelist_entry_t _entries[ROOT_MAX];
static cdfs_filelist_t _root;

static uint32_t _dat_fad, _dat_size;
static cryo_dat_entry _dat[SAT_DAT_MAX];
static int _dat_n;

#define _bounce SAT_BOUNCE_BUF

/* HIRQ flags */
#define HIRQ_REG  CD_BLOCK(0x0008UL)
#define HIRQ_CMOK 0x0001
#define HIRQ_ESEL 0x0040

/* libyaul's cd_block_init, with a vblank between the commands: issued
 * back to back, the emulators' CD blocks (Yabause, Kronos) can miss the
 * selector reset's ESEL and the boot hangs. A real drive doesn't mind. */
int sat_cd_init(void) {
    while ((MEMORY_READ(16, HIRQ_REG) & HIRQ_CMOK) == 0) {}
    if (cd_block_cmd_file_abort() != 0) return -1;
    sat_wait_vblank();
    if (cd_block_cmd_cd_system_init(0x0002) != 0) return -1;
    sat_wait_vblank();
    if (cd_block_cmd_data_transfer_end() != 0) return -1;
    sat_wait_vblank();
    if (cd_block_cmd_selector_reset(0xFC, 0) != 0) return -1;
    for (int i = 0; (MEMORY_READ(16, HIRQ_REG) & HIRQ_ESEL) == 0; i++) {
        if (i > 600) return -1;   /* ten seconds */
        sat_wait_vblank();
    }
    cdfs_config_default_set();
    cdfs_filelist_init(&_root, _entries, ROOT_MAX);
    cdfs_filelist_root_read(&_root);
    return _root.entries_count > 0 ? 0 : -1;
}

static int _same(const char* a, const char* b) {
    for (;; a++, b++) {
        int x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x == ';') x = 0;   /* ISO version suffix */
        if (y == ';') y = 0;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

int sat_cd_find(const char* name, uint32_t* fad, uint32_t* size) {
    for (uint32_t i = 0; i < _root.entries_count; i++) {
        const cdfs_filelist_entry_t* e = &_root.entries[i];
        if (e->type == CDFS_ENTRY_TYPE_FILE && _same(e->name, name)) {
            *fad = e->starting_fad;
            *size = e->size;
            return 0;
        }
    }
    return -1;
}

int sat_cd_read(uint32_t fad, uint32_t off, void* buf, uint32_t n) {
    uint8_t* out = buf;
    while (n) {
        const uint32_t sec = off / SECTOR, skip = off % SECTOR;
        if (skip == 0 && n >= SECTOR && ((uintptr_t)out & 3) == 0) {
            /* Whole sectors straight into the caller's buffer. */
            uint32_t len = n - n % SECTOR;
            if (cd_block_sectors_read(fad + sec, out, len) != 0) return -1;
            out += len; off += len; n -= len;
            continue;
        }
        uint32_t len = skip + n;
        if (len > SAT_BOUNCE_BUF_SIZE) len = SAT_BOUNCE_BUF_SIZE;
        len = (len + SECTOR - 1) / SECTOR * SECTOR;
        if (cd_block_sectors_read(fad + sec, _bounce, len) != 0) return -1;
        uint32_t take = len - skip;
        if (take > n) take = n;
        memcpy(out, _bounce + skip, take);
        out += take; off += take; n -= take;
    }
    return 0;
}

int sat_dat_open(void) {
    if (sat_cd_find("MEGARACE.DAT", &_dat_fad, &_dat_size) != 0) return -1;
    uint8_t head[2];
    if (sat_cd_read(_dat_fad, 0, head, 2) != 0) return -2;
    unsigned count = cryo_dat_count(head);
    if (count == 0 || count > 4096) return -3;
    /* The directory is at most 4096 * 25 bytes; read it in pieces. */
    _dat_n = 0;
    uint32_t off = 2;
    static uint8_t dir[CRYO_DAT_ENTRY_SIZE * 64];
    while (count && _dat_n < SAT_DAT_MAX) {
        unsigned k = count < 64 ? count : 64;
        if (sat_cd_read(_dat_fad, off, dir, k * CRYO_DAT_ENTRY_SIZE) != 0) return -2;
        _dat_n += cryo_dat_parse(dir, k * CRYO_DAT_ENTRY_SIZE, k, _dat + _dat_n, SAT_DAT_MAX - _dat_n);
        off += k * CRYO_DAT_ENTRY_SIZE;
        count -= k;
    }
    return _dat_n > 0 ? 0 : -3;
}

int sat_dat_count(void) { return _dat_n; }
const cryo_dat_entry* sat_dat_entry(int i) { return i >= 0 && i < _dat_n ? &_dat[i] : NULL; }
int sat_dat_find(const char* name) { return cryo_dat_find(_dat, _dat_n, name); }

int sat_dat_read(int entry, uint32_t off, void* buf, uint32_t n) {
    const cryo_dat_entry* e = sat_dat_entry(entry);
    if (!e || off > e->size || n > e->size - off) return -1;
    return sat_cd_read(_dat_fad, e->offset + off, buf, n);
}

/* ---- streaming reader ----
 *
 * One "play" command covers the whole entry; sectors arrive in the CD
 * block's buffer at the drive's speed and are moved into ours whenever the
 * game asks for data or calls sat_reader_pump() (the HNM player does while
 * it waits for a frame's time). Reading never stops the game unless the
 * data it needs right now hasn't arrived yet.
 */

static sat_reader* _streaming;

static void _stream_stop(void) {
    cd_block_cmd_disk_seek(0xFFFFFF);          /* pause */
    cd_block_cmd_selector_reset(0xFC, 0);      /* drop what's buffered */
    for (int i = 0; (MEMORY_READ(16, HIRQ_REG) & HIRQ_ESEL) == 0 && i < 600; i++) sat_wait_vblank();
    _streaming = NULL;
}

void sat_reader_open(sat_reader* r, int entry, uint8_t* buf, uint32_t cap) {
    if (_streaming) _stream_stop();
    const cryo_dat_entry* e = sat_dat_entry(entry);
    r->base = e ? e->offset : 0;
    r->size = e ? e->size : 0;
    r->pos = 0;
    r->buf = buf;
    r->cap = cap - cap % SECTOR;
    r->buf_start = r->base - r->base % SECTOR;
    r->buf_len = 0;
    r->next = r->buf_start;
    r->stalls = 0;
    const uint32_t end = r->base + r->size;
    const uint32_t sectors = (end - r->buf_start + SECTOR - 1) / SECTOR;
    if (!e || !sectors) return;
    cd_block_cmd_selector_reset(0, 0);
    cd_block_cmd_cd_dev_connection_set(0);
    cd_block_cmd_sector_length_set(SECTOR_LENGTH_2048);
    cd_block_cmd_disk_play(0, _dat_fad + r->buf_start / SECTOR, (int32_t)sectors);
    _streaming = r;
}

void sat_reader_close(sat_reader* r) {
    if (_streaming == r) _stream_stop();
}

/* Moves sectors that have arrived into our buffer; returns how many. */
uint32_t sat_reader_pump(sat_reader* r) {
    if (_streaming != r) return 0;
    const uint32_t end = r->base + r->size;
    if (r->next >= end) return 0;
    /* Make room: drop whole sectors before the read position. */
    const uint32_t abs = r->base + r->pos;
    if (r->cap - r->buf_len < 16 * SECTOR && abs > r->buf_start) {
        uint32_t drop = (abs - r->buf_start) / SECTOR * SECTOR;
        if (drop > r->buf_len) drop = r->buf_len;
        if (drop) {
            memmove(r->buf, r->buf + drop, r->buf_len - drop);
            r->buf_start += drop;
            r->buf_len -= drop;
        }
    }
    uint32_t room = (r->cap - r->buf_len) / SECTOR;
    if (!room) return 0;
    uint32_t ready = (uint32_t)cd_block_cmd_sector_number_get(0);
    if (!ready) return 0;
    uint32_t k = ready < room ? ready : room;
    if (k > 32) k = 32;
    if (cd_block_transfer_data(0, 0, r->buf + r->buf_len, k * SECTOR) != 0) return 0;
    r->buf_len += k * SECTOR;
    r->next += k * SECTOR;
    return k;
}

const uint8_t* sat_reader_peek(sat_reader* r, uint32_t n) {
    if (r->pos > r->size || n > r->size - r->pos) return NULL;
    if (n + 17 * SECTOR > r->cap) return NULL;
    const uint32_t abs = r->base + r->pos;
    int idle = 0;
    while (abs + n > r->buf_start + r->buf_len) {
        if (sat_reader_pump(r)) { idle = 0; continue; }
        if (_streaming != r || ++idle > 600) return NULL;   /* ten seconds without data */
        if (idle == 1) r->stalls++;
        sat_wait_vblank();
    }
    return r->buf + (abs - r->buf_start);
}

void sat_reader_skip(sat_reader* r, uint32_t n) { r->pos += n; }

const uint8_t* sat_reader_buffered(const sat_reader* r, uint32_t pos, uint32_t n) {
    if (pos > r->size || n > r->size - pos) return NULL;
    const uint32_t abs = r->base + pos;
    if (abs >= r->buf_start && abs + n <= r->buf_start + r->buf_len)
        return r->buf + (abs - r->buf_start);
    return NULL;
}
