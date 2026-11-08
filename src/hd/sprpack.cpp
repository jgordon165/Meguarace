#include "hd/sprpack.h"

#include <webp/decode.h>
#include <webp/encode.h>

#include <cstdio>
#include <cstring>
#include <fstream>

namespace hd {

static std::vector<uint8_t> toRgba(const uint32_t* argb, size_t n) {
    std::vector<uint8_t> b(n * 4);
    for (size_t i = 0; i < n; ++i) {
        b[i * 4] = uint8_t(argb[i] >> 16);
        b[i * 4 + 1] = uint8_t(argb[i] >> 8);
        b[i * 4 + 2] = uint8_t(argb[i]);
        b[i * 4 + 3] = uint8_t(argb[i] >> 24);
    }
    return b;
}

std::vector<uint8_t> encodeWebpRgba(const uint32_t* argb, int w, int h, int quality) {
    auto rgba = toRgba(argb, size_t(w) * size_t(h));
    uint8_t* out = nullptr;
    size_t n = quality > 100 ? WebPEncodeLosslessRGBA(rgba.data(), w, h, w * 4, &out)
                             : WebPEncodeRGBA(rgba.data(), w, h, w * 4, float(quality < 0 ? 0 : quality), &out);
    std::vector<uint8_t> r(out, out + n);
    WebPFree(out);
    return r;
}

bool decodeWebpRgba(const uint8_t* d, size_t n, int w, int h, std::vector<uint32_t>& argb) {
    int iw = 0, ih = 0;
    if (!WebPGetInfo(d, n, &iw, &ih) || iw != w || ih != h) return false;
    std::vector<uint8_t> b(size_t(w) * size_t(h) * 4);
    if (!WebPDecodeRGBAInto(d, n, b.data(), b.size(), w * 4)) return false;
    argb.resize(size_t(w) * size_t(h));
    for (size_t i = 0; i < argb.size(); ++i)
        argb[i] = uint32_t(b[i * 4 + 3]) << 24 | uint32_t(b[i * 4]) << 16 | uint32_t(b[i * 4 + 1]) << 8 | b[i * 4 + 2];
    return true;
}

static void put(std::vector<uint8_t>& b, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
static uint32_t get(const uint8_t* p, int bytes) {
    uint32_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= uint32_t(p[i]) << (8 * i);
    return v;
}
static constexpr size_t kHeader = 4 + 2 + 1 + 1 + 8 + 4 + 4 + 768;
static constexpr size_t kEntry = 16;

bool writeSpritePack(const std::string& path, const SpritePackData& p, int quality, std::string& err) {
    std::vector<std::vector<uint8_t>> blobs;
    for (const auto& im : p.images)
        blobs.push_back(im.px.empty() ? std::vector<uint8_t>{}
                                      : encodeWebpRgba(im.px.data(), im.hdW, im.hdH, p.kind == 1 ? 101 : quality));
    std::vector<uint8_t> b = {'M', 'R', 'H', 'S'};
    put(b, kSpritePackVersion, 2);
    b.push_back(uint8_t(p.kind));
    b.push_back(uint8_t((p.upscaler & 3) | (p.sharpened ? 4 : 0) | (p.grained ? 8 : 0)));   // bit 2: sharpened, 3: grain
    put(b, uint32_t(p.pictureW), 2); put(b, uint32_t(p.pictureH), 2);
    put(b, uint32_t(p.width), 2);    put(b, uint32_t(p.height), 2);
    put(b, uint32_t(p.images.size()), 4);
    put(b, p.sourceCrc, 4);
    b.insert(b.end(), p.palette.begin(), p.palette.begin() + 768);
    uint64_t off = kHeader + kEntry * p.images.size();
    for (size_t i = 0; i < p.images.size(); ++i) {
        const auto& im = p.images[i];
        put(b, uint32_t(im.w), 2); put(b, uint32_t(im.h), 2);
        put(b, uint32_t(im.hdW), 2); put(b, uint32_t(im.hdH), 2);
        put(b, blobs[i].empty() ? 0u : uint32_t(off), 4);
        put(b, uint32_t(blobs[i].size()), 4);
        off += blobs[i].size();
    }
    for (const auto& bl : blobs) b.insert(b.end(), bl.begin(), bl.end());
    std::string part = path + ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
        if (!out) { err = "cannot write " + part; return false; }
    }
    std::remove(path.c_str());
    if (std::rename(part.c_str(), path.c_str()) != 0) { err = "cannot rename " + part; return false; }
    return true;
}

bool readSpritePack(const std::string& path, uint32_t sourceCrc, SpritePackData& p, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "not found"; return false; }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < kHeader || std::memcmp(d.data(), "MRHS", 4) != 0) { err = "not an HD sprite pack"; return false; }
    if (get(&d[4], 2) != kSpritePackVersion) { err = "made by another version: run hd-conversion-utility again"; return false; }
    p.kind = d[6];
    p.upscaler = d[7] & 3u;
    p.sharpened = (d[7] & 4u) != 0;
    p.grained = (d[7] & 8u) != 0;
    p.pictureW = int(get(&d[8], 2)); p.pictureH = int(get(&d[10], 2));
    p.width = int(get(&d[12], 2));   p.height = int(get(&d[14], 2));
    uint32_t count = get(&d[16], 4);
    p.sourceCrc = get(&d[20], 4);
    if (p.sourceCrc != sourceCrc) { err = "made from a different version of this sprite set"; return false; }
    if (p.kind > 1 || count > 4096 || d.size() < kHeader + kEntry * count || p.width <= 0 || p.height <= 0) {
        err = "damaged pack";
        return false;
    }
    p.palette.assign(d.begin() + 24, d.begin() + 24 + 768);
    p.images.assign(count, {});
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = &d[kHeader + kEntry * i];
        auto& im = p.images[i];
        im.w = int(get(e, 2)); im.h = int(get(e + 2, 2));
        im.hdW = int(get(e + 4, 2)); im.hdH = int(get(e + 6, 2));
        uint32_t off = get(e + 8, 4), size = get(e + 12, 4);
        if (!size) continue;
        if (uint64_t(off) + size > d.size() || !decodeWebpRgba(&d[off], size, im.hdW, im.hdH, im.px)) {
            err = "damaged image " + std::to_string(i);
            return false;
        }
    }
    return true;
}

}  // namespace hd
