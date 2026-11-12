/* Freestanding C decoders for MEGARACE.DAT, HSQ and HNM (1). See cryo_c.h. */
#include "formats/cryo_c.h"

#include <string.h>

/* ================================================================ DAT === */

unsigned cryo_dat_count(const uint8_t head[2]) { return head[0] | head[1] << 8; }

static uint32_t rd32(const uint8_t* p) {
    return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int cryo_dat_parse(const uint8_t* dir, size_t n, unsigned count, cryo_dat_entry* out, int max) {
    int k = 0;
    for (unsigned i = 0; i < count && (size_t)(i + 1) * CRYO_DAT_ENTRY_SIZE <= n && k < max; ++i) {
        const uint8_t* e = dir + (size_t)i * CRYO_DAT_ENTRY_SIZE;
        int len = 0;
        while (len < 16 && e[len]) ++len;
        if (len == 0) continue;   /* unused trailing slot */
        memcpy(out[k].name, e, (size_t)len);
        out[k].name[len] = 0;
        out[k].size   = rd32(e + 16);
        out[k].offset = rd32(e + 20);
        ++k;
    }
    return k;
}

static int up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

int cryo_dat_find(const cryo_dat_entry* e, int n, const char* name) {
    for (int i = 0; i < n; ++i) {
        const char* a = e[i].name;
        const char* b = name;
        while (*a && up((unsigned char)*a) == up((unsigned char)*b)) { ++a; ++b; }
        if (!*a && !*b) return i;
    }
    return -1;
}

/* ================================================================ HSQ === */

int cryo_hsq_is(const uint8_t* d, size_t n) {
    if (n < 6 || d[2] != 0) return 0;
    unsigned s = 0;
    for (int i = 0; i < 6; ++i) s += d[i];
    return (s & 0xFF) == 0xAB;
}

unsigned cryo_hsq_size(const uint8_t* d, size_t n) { return n >= 2 ? (unsigned)(d[0] | d[1] << 8) : 0; }

typedef struct {
    const uint8_t* d;
    size_t n, pos;
    uint32_t queue;
    int err;
} hsq_in;

static int hsq_bit(hsq_in* r) {
    int b = (int)(r->queue & 1);
    r->queue >>= 1;
    if (r->queue == 0) {
        if (r->pos + 2 > r->n) { r->err = CRYO_E_TRUNCATED; return 0; }
        r->queue = r->d[r->pos] | r->d[r->pos + 1] << 8;
        r->pos += 2;
        b = (int)(r->queue & 1);
        r->queue = (r->queue >> 1) | 0x8000;
    }
    return b;
}

long cryo_hsq_unpack(const uint8_t* d, size_t n, uint8_t* out, size_t cap) {
    if (!cryo_hsq_is(d, n)) return CRYO_E_FORMAT;
    hsq_in r = {d, n, 6, 0, 0};
    size_t len = 0;
    for (;;) {
        int b = hsq_bit(&r);
        if (r.err) return r.err;
        if (b) {
            if (r.pos + 1 > n) return CRYO_E_TRUNCATED;
            if (len >= cap) return CRYO_E_SPACE;
            out[len++] = d[r.pos++];
            continue;
        }
        int count;
        long offset;
        b = hsq_bit(&r);
        if (r.err) return r.err;
        if (b) {
            if (r.pos + 2 > n) return CRYO_E_TRUNCATED;
            unsigned w = d[r.pos] | d[r.pos + 1] << 8;
            r.pos += 2;
            count = (int)(w & 7);
            offset = (long)(w >> 3) - 8192;
            if (count == 0) {
                if (r.pos + 1 > n) return CRYO_E_TRUNCATED;
                count = d[r.pos++];
                if (count == 0) break;
            }
            count += 2;
        } else {
            int hi = hsq_bit(&r);
            int lo = hsq_bit(&r);
            if (r.err) return r.err;
            count = (hi << 1 | lo) + 2;
            if (r.pos + 1 > n) return CRYO_E_TRUNCATED;
            offset = (long)d[r.pos++] - 256;
        }
        long start = (long)len + offset;
        if (start < 0) return CRYO_E_CORRUPT;
        if (len + (size_t)count > cap) return CRYO_E_SPACE;
        for (int i = 0; i < count; ++i) out[len + (size_t)i] = out[(size_t)start + (size_t)i];
        len += (size_t)count;
    }
    return (long)len;
}

/* ================================================================ HNM === */

static void pal_ranges(cryo_hnm_state* st, const uint8_t* p, size_t n) {
    size_t i = 0;
    while (i + 2 <= n) {
        int start = p[i], count = p[i + 1];
        i += 2;
        if (start == 0xFF && count == 0xFF) break;
        if (count == 0) count = 256;
        for (int k = 0; k < count && i + 3 <= n; ++k, i += 3) {
            int idx = start + k;
            if (idx > 255) continue;
            for (int c = 0; c < 3; ++c) {
                uint8_t v = p[i + (size_t)c] & 63;
                st->pal[idx * 3 + c] = (uint8_t)(v << 2 | v >> 4);
            }
            if (idx < st->pal_first) st->pal_first = idx;
            if (idx > st->pal_last) st->pal_last = idx;
        }
    }
}

long cryo_hnm_header(cryo_hnm_state* st, const uint8_t* d, size_t n, size_t* need) {
    memset(st, 0, sizeof *st);
    st->pal_first = 256;
    st->pal_last = -1;
    if (n < 4) { if (need) *need = 4; return CRYO_E_TRUNCATED; }
    size_t hsz = d[0] | d[1] << 8;
    if (hsz < 2) return CRYO_E_FORMAT;
    if (hsz > n) { if (need) *need = hsz; return CRYO_E_TRUNCATED; }
    pal_ranges(st, d + 2, hsz - 2);
    return (long)hsz;
}

void cryo_hnm_rewind(cryo_hnm_state* st) { st->voc_seen = 0; }

unsigned cryo_hnm_superchunk_size(const uint8_t sc[2]) { return sc[0] | sc[1] << 8; }

static void add_sound(cryo_hnm_state* st, cryo_hnm_frame* f, const uint8_t* p, size_t n) {
    if (!st->voc_seen && n >= 26 && memcmp(p, "Creative Voice File", 19) == 0) {
        st->voc_seen = 1;
        size_t hdr = p[20] | p[21] << 8;
        if (hdr + 6 <= n && p[hdr] == 1) {
            uint8_t sr = p[hdr + 4];
            st->sound_rate = 1000000 / (256 - sr);
            p += hdr + 6; n -= hdr + 6;
        } else {
            size_t k = hdr < n ? hdr : n;
            p += k; n -= k;
        }
    }
    if (n && f->sound_count < CRYO_HNM_MAX_SOUND) {
        f->sound[f->sound_count] = p;
        f->sound_len[f->sound_count] = n;
        f->sound_count++;
    }
}

/* ---- method 173 (0xAD) ---- */

typedef struct {
    const uint8_t* d;
    size_t n, p;
    uint16_t q;
    int bits;
    int nib[2];
    int err;
} hnm_in;

static uint8_t in_byte(hnm_in* r) {
    if (r->p >= r->n) { r->err = CRYO_E_TRUNCATED; return 0; }
    return r->d[r->p++];
}

static int in_bit(hnm_in* r) {
    if (r->bits == 0) {
        uint8_t lo = in_byte(r);
        uint8_t hi = in_byte(r);
        r->q = (uint16_t)(lo | hi << 8);
        r->bits = 16;
    }
    --r->bits;
    return (r->q >> r->bits) & 1;
}

static int in_nibble(hnm_in* r, int slot) {
    if (r->nib[slot] < 0) {
        uint8_t b = in_byte(r);
        r->nib[slot] = b & 15;
        return b >> 4;
    }
    int v = r->nib[slot];
    r->nib[slot] = -1;
    return v;
}

static int nib_len(hnm_in* r) {
    int v = in_nibble(r, 1);
    if (v == 0) v = in_byte(r) + 16;
    return v + 4;
}

static int unpack173(const uint8_t* d, size_t n, cryo_hnm_frame* f, uint8_t* px, size_t wh,
                     uint8_t* cb, size_t cbcap) {
    if (n < 6) return CRYO_E_TRUNCATED;
    size_t frame_size = d[0] | d[1] << 8;
    size_t cb_size    = d[2] | d[3] << 8;
    uint8_t flags     = d[4];
    size_t pos = 6;
    f->x = f->y = 0;
    if (!(flags & 4)) {
        if (n < 10) return CRYO_E_TRUNCATED;
        f->x = d[6] | d[7] << 8;
        f->y = d[8] | d[9] << 8;
        pos = 10;
    }
    const uint8_t base = (flags & 0x40) ? 128 : 0;
    hnm_in r = {d, n, pos, 0, 0, {-1, -1}, 0};

    size_t cbn = 0;
    while (cbn < cb_size) {
        uint8_t tag = in_byte(&r);
        if (r.err) return r.err;
        if (tag & 0x80) {
            int t = in_nibble(&r, 0);
            if (r.err) return r.err;
            size_t off = (size_t)((tag & 0x7F) << 1 | (t & 1));
            int cnt = (t >> 1) + 2;
            if (off + 1 > cbn) return CRYO_E_CORRUPT;
            if (cbn + (size_t)cnt > cbcap) return CRYO_E_SPACE;
            size_t s = cbn - off - 1;
            for (int i = 0; i < cnt; ++i) { cb[cbn] = cb[s + (size_t)i]; ++cbn; }
        } else {
            if (cbn >= cbcap) return CRYO_E_SPACE;
            cb[cbn++] = tag ? (uint8_t)(tag + base) : 0;
        }
    }

    const size_t lim = frame_size < wh ? frame_size : wh;
    size_t o = 0, ci = 0;
    const int long_runs = flags & 0x80;
    while (o < frame_size) {
        int done = 0;
        for (;;) {
            int b = in_bit(&r);
            if (r.err) return r.err;
            if (b) break;
            if (ci >= cbn) return CRYO_E_CORRUPT;
            if (o < lim) px[o] = cb[ci];
            ++ci; ++o;
            if (o >= frame_size) { done = 1; break; }
        }
        if (done) break;
        if (ci >= cbn) return CRYO_E_CORRUPT;
        uint8_t c = cb[ci++];
        int len;
        if (long_runs) {
            if (!in_bit(&r)) len = nib_len(&r);
            else if (!in_bit(&r)) len = 2;
            else if (!in_bit(&r)) len = 3;
            else len = 4;
        } else {
            if (!in_bit(&r)) len = 2;
            else if (!in_bit(&r)) len = 3;
            else if (!in_bit(&r)) len = 4;
            else len = nib_len(&r);
        }
        if (r.err) return r.err;
        for (int i = 0; i < len; ++i, ++o) if (o < lim) px[o] = c;
    }
    if (lim < wh) memset(px + lim, 0, wh - lim);
    return CRYO_OK;
}

static int is_tag(uint8_t a, uint8_t b) {
    return (a == 'p' && b == 'l') || (a == 's' && b == 'd') ||
           (a == 'p' && b == 't') || (a == 'k' && b == 'l');
}

long cryo_hnm_chunks(cryo_hnm_state* st, const uint8_t* sc, size_t n, cryo_hnm_frame* f) {
    memset(f, 0, sizeof *f);
    st->pal_first = 256;
    st->pal_last = -1;
    if (n < 2) return CRYO_E_TRUNCATED;
    size_t end = sc[0] | sc[1] << 8;
    if (end < 2) return CRYO_E_FORMAT;
    if (end > n) return CRYO_E_TRUNCATED;
    size_t p = 2;
    while (p + 4 <= end) {
        size_t sz = sc[p + 2] | sc[p + 3] << 8;
        if (!is_tag(sc[p], sc[p + 1])) {
            /* A video header can't be wider than 320: a damaged 'kl' tag
             * (NGL1.HNM frame 2185 on the US v1.0.1 disc). */
            unsigned width = (sc[p] | sc[p + 1] << 8) & 0x1FF;
            if (width > 320 && sz >= 4 && p + sz < end) {
                f->kl = sc + p + 4; f->kl_len = sz - 4;
                p += sz;
                continue;
            }
            break;
        }
        if (sz < 4 || p + sz > end) return CRYO_E_CORRUPT;
        const uint8_t* body = sc + p + 4;
        size_t blen = sz - 4;
        if (sc[p] == 'p' && sc[p + 1] == 'l') { pal_ranges(st, body, blen); f->palette_changed = 1; }
        else if (sc[p] == 's') add_sound(st, f, body, blen);
        else if (sc[p] == 'p') { if (!f->pt) { f->pt = body; f->pt_len = blen; } }
        else { if (!f->kl) { f->kl = body; f->kl_len = blen; } }
        p += sz;
    }
    return (long)p;
}

int cryo_hnm_decode(cryo_hnm_state* st, const uint8_t* sc, size_t n,
                    cryo_hnm_frame* f, uint8_t* pixels, size_t cap,
                    uint8_t* scratch, size_t scratch_cap) {
    long pc = cryo_hnm_chunks(st, sc, n, f);
    if (pc < 0) return (int)pc;
    size_t p = (size_t)pc;
    const size_t end = sc[0] | sc[1] << 8;
    if (p + 4 > end) return CRYO_OK;
    uint32_t hdr = rd32(sc + p);
    f->w = (int)(hdr & 0x1FF);
    f->subtitle_cue = (hdr & 0x4000) ? 0x40 : ((hdr & 0x2000) ? 0x20 : 0);
    f->h = (int)((hdr >> 16) & 0xFF);
    f->overlay = (hdr >> 24) == 0xFF;
    p += 4;
    if (f->w == 0 || f->h == 0 || p + 6 > end) return CRYO_OK;
    const size_t wh = (size_t)f->w * (size_t)f->h;
    if (wh > cap) return CRYO_E_SPACE;
    const uint8_t* z = sc + p;
    unsigned sum = 0;
    for (int k = 0; k < 6; ++k) sum += z[k];
    int rc;
    switch (sum & 0xFF) {
    case 0xAD:
        rc = unpack173(z, end - p, f, pixels, wh, scratch, scratch_cap);
        if (rc) return rc;
        break;
    case 0xAB: {
        /* HSQ-packed: a full frame unpacks to its pixels; a smaller one
         * (CREDIT.HNM's overlays) to x, y as u16 then PackBits pixels. */
        long ul = cryo_hsq_unpack(z, end - p, scratch, scratch_cap);
        if (ul < 0) return (int)ul;
        const uint8_t* u = scratch;
        size_t un = (size_t)ul;
        f->x = f->y = 0;
        size_t o = 0;
        if (un >= 4 && un < wh) {
            f->x = u[0] | u[1] << 8;
            f->y = u[2] | u[3] << 8;
            if (f->x + f->w > 320 || f->y + f->h > 200) {
                f->x = f->y = 0;
                memcpy(pixels, u, un);
                o = un;
            } else {
                size_t r = 4;
                while (r < un && o < wh) {
                    const uint8_t c = u[r++];
                    if (c & 0x80) {
                        if (r >= un) break;
                        size_t k = (size_t)(257 - c);
                        uint8_t v = u[r++];
                        if (k > wh - o) k = wh - o;
                        memset(pixels + o, v, k);
                        o += k;
                    } else {
                        size_t k = (size_t)c + 1;
                        if (k > un - r) k = un - r;
                        if (k > wh - o) k = wh - o;
                        memcpy(pixels + o, u + r, k);
                        r += k;   /* the C++ decoder steps r by the copied count too */
                        o += k;
                    }
                }
            }
        } else {
            o = un < wh ? un : wh;
            memcpy(pixels, u, o);
        }
        if (o < wh) memset(pixels + o, 0, wh - o);
        break;
    }
    default:
        return CRYO_E_FORMAT;
    }
    f->has_video = 1;
    return CRYO_OK;
}
