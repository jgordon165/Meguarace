#include "hd/hdpack.h"

#include <webp/decode.h>
#include <webp/encode.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace hd {

const char* upscalerName(uint32_t code) {
    switch (code) {
        case kUpscalerRealEsrgan: return "REAL-ESRGAN";
        case kUpscalerSharp: return "SHARP";
        case kUpscalerSmooth: return "SMOOTH";
        default: return "";
    }
}

uint32_t crc32(const uint8_t* d, size_t n) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// WebP works on bytes; our pixels are 0xAARRGGBB words, which in memory on a
// little-endian machine are B,G,R,A. Convert explicitly so it works anywhere.
static std::vector<uint8_t> toRgb(const uint32_t* argb, size_t n) {
    std::vector<uint8_t> rgb(n * 3);
    for (size_t i = 0; i < n; ++i) {
        rgb[i * 3]     = uint8_t(argb[i] >> 16);
        rgb[i * 3 + 1] = uint8_t(argb[i] >> 8);
        rgb[i * 3 + 2] = uint8_t(argb[i]);
    }
    return rgb;
}

std::vector<uint8_t> encodeWebp(const uint32_t* argb, int w, int h, int quality) {
    std::vector<uint8_t> rgb = toRgb(argb, size_t(w) * size_t(h));
    uint8_t* out = nullptr;
    size_t n = quality > 100
        ? WebPEncodeLosslessRGB(rgb.data(), w, h, w * 3, &out)
        : WebPEncodeRGB(rgb.data(), w, h, w * 3, float(std::clamp(quality, 0, 100)), &out);
    std::vector<uint8_t> r(out, out + n);
    WebPFree(out);
    return r;
}

bool decodeWebp(const uint8_t* d, size_t n, int w, int h, std::vector<uint32_t>& argb) {
    int iw = 0, ih = 0;
    if (!WebPGetInfo(d, n, &iw, &ih) || iw != w || ih != h) return false;
    std::vector<uint8_t> rgb(size_t(w) * size_t(h) * 3);
    if (!WebPDecodeRGBInto(d, n, rgb.data(), rgb.size(), w * 3)) return false;
    argb.resize(size_t(w) * size_t(h));
    for (size_t i = 0; i < argb.size(); ++i)
        argb[i] = 0xFF000000u | uint32_t(rgb[i * 3]) << 16 | uint32_t(rgb[i * 3 + 1]) << 8 | rgb[i * 3 + 2];
    return true;
}

// ---- little-endian helpers ---------------------------------------------------
static void put(std::vector<uint8_t>& b, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
static uint32_t get(const uint8_t* p, int bytes) {
    uint32_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= uint32_t(p[i]) << (8 * i);
    return v;
}
static constexpr size_t kHeaderSize = 4 + 2 + 2 + 2 + 2 + 2 + 4 + 4 + 4;

static std::vector<uint8_t> headerBytes(const PackHeader& h) {
    std::vector<uint8_t> b = {'M', 'R', 'H', 'D'};
    put(b, kPackVersion, 2);
    put(b, uint32_t(h.pictureW), 2);
    put(b, uint32_t(h.pictureH), 2);
    put(b, uint32_t(h.width), 2);
    put(b, uint32_t(h.height), 2);
    put(b, h.frames, 4);
    put(b, h.sourceCrc, 4);
    put(b, h.upscaler & 3, 4);
    return b;
}

// ---- writer ------------------------------------------------------------------
bool PackWriter::open(const std::string& path, const PackHeader& h) {
    if (h.pictureW < 1 || h.pictureH < 1 || h.pictureW > kMaxPicture || h.pictureH > kMaxPicture ||
        h.width <= 0 || h.height <= 0) {
        err_ = "bad pack parameters";
        return false;
    }
    h_ = h;
    path_ = path;
    index_.clear();
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_) { err_ = "cannot write " + path; return false; }
    // Header and a zeroed index first; finish() fills in the index.
    std::vector<uint8_t> b = headerBytes(h);
    b.resize(kHeaderSize + size_t(h.frames) * 8, 0);
    out_.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    pos_ = b.size();
    return bool(out_);
}

bool PackWriter::addFrame(const std::vector<uint8_t>& webp) {
    if (index_.size() >= h_.frames) { err_ = "too many frames"; return false; }
    if (webp.empty() || pos_ + webp.size() > 0xFFFFFFFFull) { err_ = "frame too large"; return false; }
    index_.emplace_back(uint32_t(pos_), uint32_t(webp.size()));
    out_.write(reinterpret_cast<const char*>(webp.data()), std::streamsize(webp.size()));
    pos_ += webp.size();
    return bool(out_);
}

bool PackWriter::addRepeat() {
    if (index_.size() >= h_.frames) { err_ = "too many frames"; return false; }
    index_.emplace_back(0u, 0u);
    return true;
}

bool PackWriter::finish() {
    if (index_.size() != h_.frames) { err_ = "frame count mismatch"; return false; }
    std::vector<uint8_t> b;
    for (const auto& [off, size] : index_) { put(b, off, 4); put(b, size, 4); }
    out_.seekp(std::streamoff(kHeaderSize));
    out_.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    out_.close();
    if (out_.fail()) { err_ = "write failed: " + path_; return false; }
    return true;
}

// ---- reader ------------------------------------------------------------------
static bool parseHeader(const uint8_t* hdr, PackHeader& h) {
    h.pictureW = int(get(hdr + 6, 2));
    h.pictureH = int(get(hdr + 8, 2));
    h.width = int(get(hdr + 10, 2));
    h.height = int(get(hdr + 12, 2));
    h.frames = get(hdr + 14, 4);
    h.sourceCrc = get(hdr + 18, 4);
    h.upscaler = get(hdr + 22, 4) & 3;
    return h.pictureW >= 1 && h.pictureH >= 1 && h.pictureW <= kMaxPicture && h.pictureH <= kMaxPicture &&
           h.width > 0 && h.height > 0 && h.frames <= 1000000;
}

bool Pack::peek(const std::string& path, PackHeader& h) {
    std::ifstream in(path, std::ios::binary);
    uint8_t hdr[kHeaderSize];
    if (!in.read(reinterpret_cast<char*>(hdr), sizeof hdr) || std::memcmp(hdr, "MRHD", 4) != 0 ||
        get(hdr + 4, 2) != kPackVersion)
        return false;
    return parseHeader(hdr, h);
}

bool Pack::open(const std::string& path, uint32_t sourceCrc) {
    in_.open(path, std::ios::binary);
    if (!in_) { err_ = "not found"; return false; }
    uint8_t hdr[kHeaderSize];
    if (!in_.read(reinterpret_cast<char*>(hdr), sizeof hdr) || std::memcmp(hdr, "MRHD", 4) != 0) {
        err_ = "not an HD pack";
        return false;
    }
    if (get(hdr + 4, 2) != kPackVersion) {
        err_ = "made by an older hd-conversion-utility; run hd-conversion-utility again";
        return false;
    }
    if (!parseHeader(hdr, h_)) { err_ = "damaged pack header"; return false; }
    if (h_.sourceCrc != sourceCrc) {
        err_ = "made from a different version of this video";
        return false;
    }
    std::vector<uint8_t> idx(size_t(h_.frames) * 8);
    if (!in_.read(reinterpret_cast<char*>(idx.data()), std::streamsize(idx.size()))) {
        err_ = "damaged pack index";
        return false;
    }
    index_.resize(h_.frames);
    picture_.assign(h_.frames, -1);
    int last = -1;
    for (uint32_t i = 0; i < h_.frames; ++i) {
        index_[i] = {get(&idx[i * 8], 4), get(&idx[i * 8 + 4], 4)};
        if (index_[i].second) last = int(i);
        picture_[i] = last;
    }
    return true;
}

const std::vector<uint32_t>* Pack::frame(int i) {
    if (i < 0 || i >= int(picture_.size()) || picture_[size_t(i)] < 0) return nullptr;
    int p = picture_[size_t(i)];
    if (p == cached_) return &cache_;
    const auto& [off, size] = index_[size_t(p)];
    std::vector<uint8_t> d(size);
    in_.clear();
    in_.seekg(std::streamoff(off));
    if (!in_.read(reinterpret_cast<char*>(d.data()), std::streamsize(size))) return nullptr;
    if (!decodeWebp(d.data(), d.size(), hdWidth(), hdHeight(), cache_)) { cached_ = -1; return nullptr; }
    cached_ = p;
    return &cache_;
}

}  // namespace hd
