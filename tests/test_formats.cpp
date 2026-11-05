// Format decoder tests. Uses hand-built inputs only: no MegaRace data is
// needed (or allowed) in the repository, so these run anywhere, including CI.
//
// Tests that need the real game run separately when MEGARACE_DATA points at
// a MegaRace CD (see tests/lua/main.lua).

#include "formats/cryo.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                           \
    } while (0)

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// Minimal HSQ encoder, the exact mirror of unHsq's reader: bits go into
// 16-bit little-endian words, LSB first, and each word's slot is reserved in
// the output at the moment the decoder will fetch it.
class HsqWriter {
public:
    void literal(uint8_t b) { bit(1); out_.push_back(b); }
    void shortMatch(int count, int offset) {          // count 2..5, offset -1..-256
        bit(0); bit(0);
        bit((count - 2) >> 1); bit((count - 2) & 1);
        out_.push_back(uint8_t(offset + 256));
    }
    void longMatch(int count, int offset) {           // count 3..9 here, offset -1..-8192
        bit(0); bit(1);
        unsigned w = unsigned(offset + 8192) << 3 | unsigned(count - 2);
        out_.push_back(uint8_t(w)); out_.push_back(uint8_t(w >> 8));
    }
    Bytes finish(uint16_t unpacked) {
        bit(0); bit(1);
        out_.push_back(0); out_.push_back(0); out_.push_back(0);   // count 0, then 0 = end
        Bytes r = {uint8_t(unpacked), uint8_t(unpacked >> 8), 0, 0, 0, 0};
        size_t packed = out_.size() + 6;
        r[3] = uint8_t(packed); r[4] = uint8_t(packed >> 8);
        unsigned s = 0;
        for (int i = 0; i < 5; ++i) s += r[size_t(i)];
        r[5] = uint8_t(0xAB - s);
        r.insert(r.end(), out_.begin(), out_.end());
        return r;
    }

private:
    void bit(int b) {
        if (used_ == 16) {
            slot_ = out_.size();
            out_.push_back(0); out_.push_back(0);
            used_ = 0;
        }
        if (b) {
            uint16_t w = uint16_t(out_[slot_] | out_[slot_ + 1] << 8);
            w = uint16_t(w | 1u << used_);
            out_[slot_] = uint8_t(w); out_[slot_ + 1] = uint8_t(w >> 8);
        }
        ++used_;
    }
    Bytes out_;
    size_t slot_ = 0;
    int used_ = 16;
};

static void testHsq() {
    // "ABCABCABCX" + a long back-reference.
    HsqWriter w;
    w.literal('A'); w.literal('B'); w.literal('C');
    w.shortMatch(5, -3);          // ABCAB
    w.literal('X');
    w.longMatch(9, -9);           // BCABCABCX
    Bytes packed = w.finish(18);
    CHECK(isHsq(packed.data(), packed.size()));
    std::vector<uint8_t> out;
    try { out = unHsq(packed.data(), packed.size()); }
    catch (const std::exception& e) { std::fprintf(stderr, "unHsq threw: %s\n", e.what()); }
    std::string s(out.begin(), out.end());
    // Exact expected output: literals ABC, copy 5 from -3 => ABCAB, X, copy 9 from -9.
    std::string expect = "ABCABCAB";
    expect += 'X';
    std::string tail = expect.substr(expect.size() - 9, 9);   // overlapping copy of the last 9
    expect += tail;
    CHECK(s == expect);

    // Not HSQ: wrong checksum.
    Bytes bad = packed;
    bad[5] ^= 1;
    CHECK(!isHsq(bad.data(), bad.size()));
    bool threw = false;
    try { unHsq(bad.data(), bad.size()); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Truncated stream must throw, not read out of bounds.
    Bytes cut(packed.begin(), packed.begin() + 8);
    threw = false;
    try { unHsq(cut.data(), cut.size()); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

// ---------------------------------------------------------------------------
// HNM container built in memory.

static void put16(Bytes& b, unsigned v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }

static Bytes chunk(const char* tag, const Bytes& body) {
    Bytes c = {uint8_t(tag[0]), uint8_t(tag[1])};
    put16(c, unsigned(body.size() + 4));
    c.insert(c.end(), body.begin(), body.end());
    return c;
}

static Bytes videoChunk(int w, int h, uint8_t mode, const Bytes& data) {
    uint32_t hdr = uint32_t(w & 0x1FF) | uint32_t(h) << 16 | uint32_t(mode) << 24;
    Bytes v(4 + data.size());
    for (size_t i = 0; i < 4; ++i) v[i] = uint8_t(hdr >> (8 * i));
    std::copy(data.begin(), data.end(), v.begin() + 4);
    return v;
}

static Bytes hnmFile(const std::vector<Bytes>& frames) {
    Bytes pal = {1, 2, 63, 0, 0, 0, 63, 0, 0xFF, 0xFF};   // index 1 red, 2 green (6-bit)
    Bytes f;
    put16(f, unsigned(2 + pal.size()));
    f.insert(f.end(), pal.begin(), pal.end());
    for (const auto& fr : frames) {
        put16(f, unsigned(fr.size() + 2));
        f.insert(f.end(), fr.begin(), fr.end());
    }
    return f;
}

static void testHnm173() {
    // 0xAD frame, long runs, no x/y: codebook [5,7,9]. Raster bits (MSB first
    // from a little-endian word): 0 literal 5 | 1 end | 10 run of 2 (7) |
    // 1 end | 110 run of 3 (9)  =>  01101110 = 0x6E00.
    Bytes a = {6, 0, 3, 0, 0x84, 0x20, 5, 7, 9, 0x00, 0x6E};
    // 0xAD frame with a codebook back-reference: 1 2 [copy 2 from 2 back] 3.
    Bytes b = {5, 0, 5, 0, 0x84, 0x1F, 1, 2, 0x80, 0x10, 3, 0x00, 0x00};
    // Overlay frame placed at x=3, y=4 (flags bit 2 clear), codebook [0, 8].
    Bytes c = {2, 0, 2, 0, 0x80, 0x00, 3, 0, 4, 0, 0, 8, 0x00, 0x00};
    unsigned s = 0;
    for (int i = 0; i < 5; ++i) s += c[size_t(i)];
    c[5] = uint8_t(0xAD - s);

    Bytes pt(58, 0x11);
    Bytes frame0 = chunk("pt", pt);
    Bytes v0 = videoChunk(3, 2, 0xFE, a);
    frame0.insert(frame0.end(), v0.begin(), v0.end());
    Bytes frame1 = videoChunk(5, 1, 0xFE, b);
    Bytes frame2 = videoChunk(2, 1, 0xFF, c);
    // A damaged-tag kl chunk (as on the real disc, NGL1 frame 2185), then video.
    Bytes kl = {0xF8, 0x4B};
    put16(kl, 8);
    kl.insert(kl.end(), {1, 2, 3, 4});
    Bytes frame3 = kl;
    Bytes v3 = videoChunk(5, 1, 0xFE, b);
    frame3.insert(frame3.end(), v3.begin(), v3.end());

    HnmVideo v;
    CHECK(v.load(hnmFile({frame0, frame1, frame2, frame3})));
    CHECK(v.frameCount() == 4);
    CHECK(v.palette()[3] == 255);   // index 1 red: 6-bit 63 expands to 255
    CHECK(v.palette()[3 + 1] == 0);

    HnmVideo::Frame f;
    CHECK(v.decode(0, f));
    CHECK(f.hasVideo && f.w == 3 && f.h == 2 && !f.overlay);
    CHECK((f.pixels == Bytes{5, 7, 7, 9, 9, 9}));
    CHECK(f.chunks.count("pt") == 1 && f.chunks["pt"].size() == 58);

    CHECK(v.decode(1, f));
    CHECK((f.pixels == Bytes{1, 2, 1, 2, 3}));

    CHECK(v.decode(2, f));
    CHECK(f.overlay && f.x == 3 && f.y == 4);
    CHECK((f.pixels == Bytes{0, 8}));

    CHECK(v.decode(3, f));
    CHECK((f.pixels == Bytes{1, 2, 1, 2, 3}));
    CHECK(f.chunks.count("kl") == 1 && (f.chunks["kl"] == Bytes{1, 2, 3, 4}));

    std::map<std::string, std::vector<uint8_t>> only;
    CHECK(v.dataChunks(0, only) && only.count("pt") == 1);
    CHECK(v.dataChunks(3, only) && only.count("kl") == 1);

    CHECK(!v.decode(4, f));      // out of range
}

static void testHnmCorrupt() {
    // Truncated frame data must fail cleanly (exception or false), never crash.
    Bytes a = {6, 0, 3, 0, 0x84, 0x20, 5, 7};
    HnmVideo v;
    CHECK(v.load(hnmFile({videoChunk(3, 2, 0xFE, a)})));
    HnmVideo::Frame f;
    bool ok = true;
    try { ok = v.decode(0, f); } catch (const std::exception&) { ok = false; }
    CHECK(!ok);
}

int main() {
    for (auto* t : {testHsq, testHnm173, testHnmCorrupt}) {
        try { t(); }
        catch (const std::exception& e) { ++g_failures; std::fprintf(stderr, "exception: %s\n", e.what()); }
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
