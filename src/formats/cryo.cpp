#include "formats/cryo.h"
#include "formats/binreader.h"
#include "vfs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

static std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// ============================================================ DAT ==========

bool CryoDat::open(const Vfs& vfs, const std::string& datPath) {
    auto head = vfs.readRange(datPath, 0, 2);
    if (!head || head->size() < 2) return false;
    uint32_t count = (*head)[0] | (*head)[1] << 8;
    auto dir = vfs.readRange(datPath, 2, count * 25ull);
    if (!dir) return false;
    entries_.clear(); index_.clear();
    for (uint32_t i = 0; i + 1 <= count && (i + 1) * 25 <= dir->size(); ++i) {
        const uint8_t* e = dir->data() + i * 25;
        Entry en;
        en.name.assign(reinterpret_cast<const char*>(e), strnlen(reinterpret_cast<const char*>(e), 16));
        if (en.name.empty()) continue;   // the table has unused trailing slots
        en.size   = e[16] | e[17] << 8 | e[18] << 16 | uint32_t(e[19]) << 24;
        en.offset = e[20] | e[21] << 8 | e[22] << 16 | uint32_t(e[23]) << 24;
        en.flag   = e[24];
        index_[upper(en.name)] = entries_.size();
        entries_.push_back(std::move(en));
    }
    vfs_ = &vfs;
    path_ = datPath;
    return !entries_.empty();
}

const CryoDat::Entry* CryoDat::find(const std::string& name) const {
    auto it = index_.find(upper(name));
    return it == index_.end() ? nullptr : &entries_[it->second];
}

std::optional<std::vector<uint8_t>> CryoDat::readRange(const Entry& e, uint32_t off, uint32_t len) const {
    if (!vfs_ || off > e.size) return std::nullopt;
    len = std::min(len, e.size - off);
    return vfs_->readRange(path_, uint64_t(e.offset) + off, len);
}

std::optional<std::vector<uint8_t>> CryoDat::read(const std::string& name) const {
    const Entry* e = find(name);
    if (!e) return std::nullopt;
    return readRange(*e, 0, e->size);
}

// ============================================================ HSQ ==========

bool isHsq(const uint8_t* d, size_t n) {
    if (n < 6 || d[2] != 0) return false;
    unsigned s = 0;
    for (int i = 0; i < 6; ++i) s += d[i];
    return (s & 0xFF) == 0xAB;
}

std::vector<uint8_t> unHsq(const uint8_t* d, size_t n) {
    if (!isHsq(d, n)) throw std::runtime_error("not HSQ");
    size_t pos = 6;
    uint32_t queue = 0;
    std::vector<uint8_t> out;
    out.reserve(d[0] | d[1] << 8);
    auto need = [&](size_t k) { if (pos + k > n) throw std::runtime_error("HSQ truncated"); };
    auto bit = [&]() -> int {
        int b = queue & 1;
        queue >>= 1;
        if (queue == 0) {
            need(2);
            queue = d[pos] | d[pos + 1] << 8;
            pos += 2;
            b = queue & 1;
            queue = (queue >> 1) | 0x8000;
        }
        return b;
    };
    for (;;) {
        if (bit()) { need(1); out.push_back(d[pos++]); continue; }
        int count;
        long offset;
        if (bit()) {
            need(2);
            unsigned w = d[pos] | d[pos + 1] << 8;
            pos += 2;
            count = w & 7;
            offset = long(w >> 3) - 8192;
            if (count == 0) {
                need(1);
                count = d[pos++];
                if (count == 0) break;
            }
            count += 2;
        } else {
            count = (bit() << 1 | bit()) + 2;
            need(1);
            offset = long(d[pos++]) - 256;
        }
        long start = long(out.size()) + offset;
        if (start < 0) throw std::runtime_error("HSQ bad back-reference");
        for (int i = 0; i < count; ++i) out.push_back(out[size_t(start + i)]);
    }
    return out;
}

// ======================================================== sprites ==========

bool SpriteSet::load(const std::vector<uint8_t>& d) {
    images_.clear();
    palette_.clear();
    auto u16 = [&](size_t at) -> unsigned {
        if (at + 2 > d.size()) throw std::runtime_error("sprite set truncated");
        return d[at] | d[at + 1] << 8;
    };
    size_t tbl = u16(0);
    if (tbl < 2 || tbl + 2 > d.size()) return false;
    palette_.assign(d.begin() + 2, d.begin() + long(tbl));
    size_t first = u16(tbl);
    size_t n = first / 2;
    if (n == 0 || tbl + first > d.size()) return false;
    std::vector<size_t> offs(n);
    for (size_t i = 0; i < n; ++i) offs[i] = tbl + u16(tbl + 2 * i);
    images_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        size_t o = offs[i];
        size_t end = i + 1 < n ? offs[i + 1] : d.size();
        if (o + 4 > d.size() || end > d.size() || end < o + 4) continue;
        unsigned wf = u16(o);
        int w = int(wf & 0x7FFF), h = d[o + 2];
        bool packed = wf & 0x8000;
        if (w == 0 || h == 0) continue;               // data table, not an image
        const size_t want = size_t(w) * size_t(h);
        const uint8_t* p = d.data() + o + 4;
        const uint8_t* e = d.data() + end;
        Image& img = images_[i];
        if (!packed) {
            if (size_t(e - p) < want) continue;       // not a raw image either
            img.pixels.assign(p, p + want);
        } else {
            // PackBits: n < 0x80 -> copy n+1 bytes; n >= 0x80 -> repeat next byte 257-n times.
            img.pixels.reserve(want);
            while (p < e && img.pixels.size() < want) {
                uint8_t c = *p++;
                if (c & 0x80) {
                    if (p >= e) break;
                    img.pixels.insert(img.pixels.end(), size_t(257 - c), *p++);
                } else {
                    size_t k = std::min<size_t>(size_t(c) + 1, size_t(e - p));
                    img.pixels.insert(img.pixels.end(), p, p + k);
                    p += k;
                }
            }
            img.pixels.resize(want, 0);
        }
        img.w = w;
        img.h = h;
    }
    return true;
}

// ============================================================ HNM ==========
//
// Overlay method 173 ("0xAD"), verified on every MegaRace video:
//  * bit queue: 16-bit little-endian words, consumed from the high bit down
//  * nibbles: high first; the codebook and the raster each keep their own
//    pending low nibble

namespace {
struct HnmReader {
    const uint8_t* d; size_t n; size_t p;
    uint16_t q = 0; int bits = 0;
    int nib[2] = {-1, -1};

    uint8_t byte() {
        if (p >= n) throw std::runtime_error("HNM truncated");
        return d[p++];
    }
    int bit() {
        if (bits == 0) { uint8_t lo = byte(); q = uint16_t(lo | byte() << 8); bits = 16; }
        --bits;
        return (q >> bits) & 1;
    }
    int nibble(int slot) {
        if (nib[slot] < 0) { uint8_t b = byte(); nib[slot] = b & 15; return b >> 4; }
        int v = nib[slot]; nib[slot] = -1; return v;
    }
};

void unpack173(const uint8_t* d, size_t n, HnmVideo::Frame& f) {
    if (n < 6) throw std::runtime_error("HNM frame too short");
    unsigned frameSize = d[0] | d[1] << 8;
    unsigned cbSize    = d[2] | d[3] << 8;
    uint8_t  flags     = d[4];
    size_t pos = 6;
    f.x = f.y = 0;
    if (!(flags & 4)) {
        if (n < 10) throw std::runtime_error("HNM frame too short");
        f.x = d[6] | d[7] << 8;
        f.y = d[8] | d[9] << 8;
        pos = 10;
    }
    const uint8_t base = (flags & 0x40) ? 128 : 0;
    HnmReader r{d, n, pos};

    std::vector<uint8_t> cb;
    cb.reserve(cbSize);
    while (cb.size() < cbSize) {
        uint8_t tag = r.byte();
        if (tag & 0x80) {
            int t = r.nibble(0);
            size_t off = size_t((tag & 0x7F) << 1 | (t & 1));
            int cnt = (t >> 1) + 2;
            if (off + 1 > cb.size()) throw std::runtime_error("HNM codebook bad reference");
            size_t s = cb.size() - off - 1;
            for (int i = 0; i < cnt; ++i) cb.push_back(cb[s + i]);
        } else {
            cb.push_back(tag ? uint8_t(tag + base) : 0);
        }
    }

    auto& out = f.pixels;
    out.clear();
    out.reserve(frameSize);
    size_t ci = 0;
    auto next = [&]() -> uint8_t {
        if (ci >= cb.size()) throw std::runtime_error("HNM codebook exhausted");
        return cb[ci++];
    };
    auto nibLen = [&]() { int v = r.nibble(1); return (v == 0 ? r.byte() + 16 : v) + 4; };
    const bool longRuns = flags & 0x80;

    while (out.size() < frameSize) {
        bool done = false;
        while (r.bit() == 0) {
            out.push_back(next());
            if (out.size() >= frameSize) { done = true; break; }
        }
        if (done) break;
        uint8_t px = next();
        int len;
        if (longRuns) {
            if (!r.bit()) len = nibLen();
            else if (!r.bit()) len = 2;
            else if (!r.bit()) len = 3;
            else len = 4;
        } else {
            if (!r.bit()) len = 2;
            else if (!r.bit()) len = 3;
            else if (!r.bit()) len = 4;
            else len = nibLen();
        }
        out.insert(out.end(), size_t(len), px);
    }
    out.resize(frameSize);
}
} // namespace

size_t HnmVideo::parsePalette(const uint8_t* p, size_t n) {
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
                uint8_t v = p[i + c] & 63;
                pal_[idx * 3 + c] = uint8_t(v << 2 | v >> 4);
            }
        }
    }
    return i;
}

bool HnmVideo::load(std::vector<uint8_t> file) {
    d_ = std::move(file);
    offsets_.clear();
    pal_.fill(0);
    soundRate_ = 0;
    vocHeaderSeen_ = false;
    if (d_.size() < 4) return false;
    size_t hsz = d_[0] | d_[1] << 8;
    if (hsz > d_.size()) return false;
    parsePalette(d_.data() + 2, hsz - 2);
    // Superchunks are contiguous, so walk them rather than trusting the table.
    size_t p = hsz;
    while (p + 2 <= d_.size()) {
        size_t sz = d_[p] | d_[p + 1] << 8;
        if (sz < 2 || p + sz > d_.size()) break;
        offsets_.push_back(uint32_t(p));
        p += sz;
    }
    return !offsets_.empty();
}

void HnmVideo::appendVoc(const uint8_t* p, size_t n, bool& headerSeen, int* rate,
                         std::vector<uint8_t>& out) {
    // The 'sd' chunks together form one VOC file. Skip its header and the
    // first block header (type 1: 8-bit PCM) once, reading the sample rate.
    if (!headerSeen && n >= 26 && std::memcmp(p, "Creative Voice File", 19) == 0) {
        headerSeen = true;
        size_t hdr = p[20] | p[21] << 8;
        if (hdr + 6 <= n && p[hdr] == 1) {
            uint8_t sr = p[hdr + 4];
            if (rate) *rate = 1000000 / (256 - sr);
            p += hdr + 6; n -= hdr + 6;
        } else {
            p += std::min(hdr, n); n -= std::min(hdr, n);
        }
    }
    out.insert(out.end(), p, p + n);
}

void HnmVideo::addSound(const uint8_t* p, size_t n, std::vector<uint8_t>& out) {
    appendVoc(p, n, vocHeaderSeen_, &soundRate_, out);
}

std::vector<uint8_t> HnmVideo::soundTrack(int* rate) const {
    std::vector<uint8_t> out;
    bool seen = false;
    if (rate) *rate = 0;
    for (int i = 0; i < frameCount(); ++i) {
        size_t p = offsets_[size_t(i)];
        size_t end = p + (d_[p] | d_[p + 1] << 8);
        p += 2;
        // Chunks come before the picture; stop at the first that isn't one
        // (the damaged-tag case in decode() has no sound, so it ends it too).
        while (p + 4 <= end) {
            char t0 = char(d_[p]), t1 = char(d_[p + 1]);
            size_t sz = d_[p + 2] | d_[p + 3] << 8;
            bool known = (t0 == 'p' && t1 == 'l') || (t0 == 's' && t1 == 'd') ||
                         (t0 == 'p' && t1 == 't') || (t0 == 'k' && t1 == 'l');
            if (!known || sz < 4 || p + sz > end) break;
            if (t0 == 's' && t1 == 'd') appendVoc(d_.data() + p + 4, sz - 4, seen, rate, out);
            p += sz;
        }
    }
    return out;
}

bool HnmVideo::dataChunks(int i, std::map<std::string, std::vector<uint8_t>>& out) const {
    out.clear();
    if (i < 0 || i >= frameCount()) return false;
    size_t p = offsets_[size_t(i)];
    size_t end = p + (d_[p] | d_[p + 1] << 8);
    p += 2;
    while (p + 4 <= end) {
        char tag[3] = {char(d_[p]), char(d_[p + 1]), 0};
        size_t sz = d_[p + 2] | d_[p + 3] << 8;
        bool known = !std::strcmp(tag, "pl") || !std::strcmp(tag, "sd") ||
                     !std::strcmp(tag, "pt") || !std::strcmp(tag, "kl");
        if (!known) {
            unsigned width = (d_[p] | d_[p + 1] << 8) & 0x1FF;
            if (width > 320 && sz >= 4 && p + sz < end) {   // damaged 'kl' tag (see decode)
                out["kl"].assign(d_.begin() + long(p + 4), d_.begin() + long(p + sz));
                p += sz;
                continue;
            }
            break;
        }
        if (sz < 4 || p + sz > end) return false;
        if (tag[0] == 'p' && tag[1] == 't') out["pt"].assign(d_.begin() + long(p + 4), d_.begin() + long(p + sz));
        if (tag[0] == 'k' && tag[1] == 'l') out["kl"].assign(d_.begin() + long(p + 4), d_.begin() + long(p + sz));
        p += sz;
    }
    return true;
}

bool HnmVideo::decode(int i, Frame& f) {
    if (i < 0 || i >= frameCount()) return false;
    if (i == 0) vocHeaderSeen_ = false;
    size_t p = offsets_[size_t(i)];
    size_t end = p + (d_[p] | d_[p + 1] << 8);
    p += 2;
    f.chunks.clear();
    f.sound.clear();
    f.hasVideo = false;
    f.paletteChanged = false;
    f.subtitleCue = 0;
    while (p + 4 <= end) {
        char tag[3] = {char(d_[p]), char(d_[p + 1]), 0};
        bool typed = !std::strcmp(tag, "pl") || !std::strcmp(tag, "sd") ||
                     !std::strcmp(tag, "pt") || !std::strcmp(tag, "kl");
        size_t sz = d_[p + 2] | d_[p + 3] << 8;
        if (!typed) {
            // A video header can't be wider than the 320-pixel screen. The
            // US v1.0.1 disc has one chunk with a damaged tag (NGL1.HNM frame
            // 2185, a 'kl' tagged f8 4b); skip such chunks by their size.
            unsigned width = (d_[p] | d_[p + 1] << 8) & 0x1FF;
            if (width > 320 && sz >= 4 && p + sz < end) {
                auto& v = f.chunks["kl"];   // its size and contents are a normal 'kl' mask
                v.assign(d_.begin() + long(p + 4), d_.begin() + long(p + sz));
                p += sz;
                continue;
            }
            break;
        }
        if (sz < 4 || p + sz > end) return false;
        const uint8_t* body = d_.data() + p + 4;
        size_t blen = sz - 4;
        if (tag[0] == 'p' && tag[1] == 'l') { parsePalette(body, blen); f.paletteChanged = true; }
        else if (tag[0] == 's' && tag[1] == 'd') addSound(body, blen, f.sound);
        else { auto& v = f.chunks[tag]; v.insert(v.end(), body, body + blen); }
        p += sz;
    }
    if (p + 4 > end) return true;
    uint32_t hdr = d_[p] | d_[p + 1] << 8 | d_[p + 2] << 16 | uint32_t(d_[p + 3]) << 24;
    f.w = int(hdr & 0x1FF);
    f.subtitleCue = (hdr & 0x4000) ? 0x40 : ((hdr & 0x2000) ? 0x20 : 0);
    f.h = int((hdr >> 16) & 0xFF);
    f.overlay = (hdr >> 24) == 0xFF;
    p += 4;
    if (f.w == 0 || f.h == 0 || p + 6 > end) return true;
    const uint8_t* z = d_.data() + p;
    unsigned sum = 0;
    for (int k = 0; k < 6; ++k) sum += z[k];
    switch (sum & 0xFF) {
    case 0xAD: unpack173(z, end - p, f); break;
    case 0xAB: {
        // HSQ-packed. A full frame unpacks to its raw pixels. A smaller one
        // (CREDIT.HNM's overlays: the credits text) unpacks to its place on
        // screen, x and y as 16-bit words, then PackBits pixels (as sprite
        // sets have: n < 0x80 copies n+1 bytes, else repeats one 257-n times).
        std::vector<uint8_t> u = unHsq(z, end - p);
        const size_t want = size_t(f.w) * size_t(f.h);
        f.x = f.y = 0;
        if (u.size() >= 4 && u.size() < want) {
            f.x = u[0] | u[1] << 8;
            f.y = u[2] | u[3] << 8;
            if (f.x + f.w > 320 || f.y + f.h > 200) { f.x = f.y = 0; f.pixels = std::move(u); break; }
            f.pixels.clear();
            f.pixels.reserve(want);
            size_t r = 4;
            while (r < u.size() && f.pixels.size() < want) {
                const uint8_t c = u[r++];
                if (c & 0x80) {
                    if (r >= u.size()) break;
                    f.pixels.insert(f.pixels.end(), size_t(257 - c), u[r++]);
                } else {
                    const size_t k = std::min<size_t>(size_t(c) + 1, u.size() - r);
                    f.pixels.insert(f.pixels.end(), u.begin() + long(r), u.begin() + long(r + k));
                    r += k;
                }
            }
        } else {
            f.pixels = std::move(u);
        }
        break;
    }
    default:   return false;
    }
    f.pixels.resize(size_t(f.w) * f.h);
    f.hasVideo = true;
    return true;
}
