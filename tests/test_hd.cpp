// HD mode tests: pack format, compositing and conversion, on hand-built
// inputs only (no game data in the repository, as in test_formats.cpp).

#include "formats/cryo.h"
#include "hd/convert.h"
#include "hd/hdpack.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                           \
    } while (0)

static fs::path scratch(const char* name) {
    fs::path p = fs::temp_directory_path() / "megarace_hd_tests";
    fs::create_directories(p);
    return p / name;
}

static void testCrc() {
    const char* s = "123456789";
    CHECK(hd::crc32(reinterpret_cast<const uint8_t*>(s), 9) == 0xCBF43926u);
}

static void testWebpLossless() {
    std::vector<uint32_t> px(8 * 4);
    for (size_t i = 0; i < px.size(); ++i) px[i] = 0xFF000000u | uint32_t(i * 0x050307);
    auto w = hd::encodeWebp(px.data(), 8, 4, 101);
    std::vector<uint32_t> back;
    CHECK(!w.empty() && hd::decodeWebp(w.data(), w.size(), 8, 4, back));
    CHECK(back == px);
    CHECK(!hd::decodeWebp(w.data(), w.size(), 4, 8, back));   // wrong size refused
}

static void testPack() {
    const int PW = 8, PH = 6;        // tiny canvas 4x2 shown as an 8x6 picture
    std::vector<uint32_t> a(size_t(PW * PH), 0xFF112233u), b(a.size(), 0xFFAABBCCu);
    hd::PackHeader h;
    h.pictureW = PW; h.pictureH = PH; h.width = 4; h.height = 2; h.frames = 3; h.sourceCrc = 0x1234;
    std::string path = scratch("pack.hdv").string();
    hd::PackWriter w;
    CHECK(w.open(path, h));
    CHECK(w.addFrame(hd::encodeWebp(a.data(), PW, PH, 101)));
    CHECK(w.addRepeat());
    CHECK(w.addFrame(hd::encodeWebp(b.data(), PW, PH, 101)));
    CHECK(!w.addRepeat());   // one too many
    CHECK(w.finish());

    hd::Pack p;
    CHECK(!p.open(path, 0x9999));          // made from a different video
    hd::Pack q;
    CHECK(q.open(path, 0x1234));
    CHECK(q.header().frames == 3 && q.hdWidth() == PW && q.hdHeight() == PH);
    hd::PackHeader peeked;
    CHECK(hd::Pack::peek(path, peeked) && peeked.pictureW == PW && peeked.width == 4);
    const auto* f0 = q.frame(0);
    CHECK(f0 && *f0 == a);
    const auto* f1 = q.frame(1);           // repeat of frame 0
    CHECK(f1 && *f1 == a);
    const auto* f2 = q.frame(2);
    CHECK(f2 && *f2 == b);
    CHECK(q.frame(3) == nullptr && q.frame(-1) == nullptr);

    hd::PackWriter short_;
    CHECK(short_.open(scratch("short.hdv").string(), h));
    CHECK(short_.addRepeat());
    CHECK(!short_.finish());               // frame count mismatch
}

static void testResample() {
    // A flat picture stays flat at any size, up or down.
    std::vector<uint32_t> flat(size_t(320 * 200), 0xFF336699u);
    auto up = hd::resample(flat, 320, 200, 1440, 1080);
    CHECK(up.size() == 1440u * 1080u);
    CHECK(up.front() == 0xFF336699u && up.back() == 0xFF336699u && up[size_t(540) * 1440 + 720] == 0xFF336699u);
    auto down = hd::resample(up, 1440, 1080, 800, 600);
    CHECK(down.size() == 800u * 600u && down[size_t(300) * 800 + 400] == 0xFF336699u);
    // Left half black, right half white: the edge stays in the middle.
    std::vector<uint32_t> half(size_t(8 * 2));
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 8; ++x) half[size_t(y * 8 + x)] = x < 4 ? 0xFF000000u : 0xFFFFFFFFu;
    auto r = hd::resample(half, 8, 2, 32, 8);
    CHECK((r[0] & 0xFF) == 0 && (r[31] & 0xFF) == 255);
    CHECK((r[14] & 0xFF) < 128 && (r[17] & 0xFF) > 128);
}

// ---- a small HNM built in memory (see test_formats.cpp for the format) -----
static void put16(Bytes& b, unsigned v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
static Bytes videoChunk(int w, int h, uint8_t mode, const Bytes& data) {
    uint32_t hdr = uint32_t(w & 0x1FF) | uint32_t(h) << 16 | uint32_t(mode) << 24;
    Bytes v(4 + data.size());
    for (size_t i = 0; i < 4; ++i) v[i] = uint8_t(hdr >> (8 * i));
    std::copy(data.begin(), data.end(), v.begin() + 4);
    return v;
}
static Bytes hnmFile(const std::vector<Bytes>& frames) {
    Bytes pal = {1, 2, 63, 0, 0, 0, 63, 0, 0xFF, 0xFF};   // index 1 red, 2 green
    Bytes f;
    put16(f, unsigned(2 + pal.size()));
    f.insert(f.end(), pal.begin(), pal.end());
    for (const auto& fr : frames) {
        put16(f, unsigned(fr.size() + 2));
        f.insert(f.end(), fr.begin(), fr.end());
    }
    return f;
}
static Bytes testVideo() {
    // 5x1 at 0,0: pixels 1 2 1 2 3 (red green red green black).
    Bytes b = {5, 0, 5, 0, 0x84, 0x1F, 1, 2, 0x80, 0x10, 3, 0x00, 0x00};
    // Overlay 2x1 at 3,4: pixels 0 (transparent) 8.
    Bytes c = {2, 0, 2, 0, 0x80, 0x00, 3, 0, 4, 0, 0, 8, 0x00, 0x00};
    unsigned s = 0;
    for (int i = 0; i < 5; ++i) s += c[size_t(i)];
    c[5] = uint8_t(0xAD - s);
    return hnmFile({videoChunk(5, 1, 0xFE, b), videoChunk(2, 1, 0xFF, c)});
}

static void testComposite() {
    Bytes v = testVideo();
    std::vector<std::vector<uint32_t>> pics;
    std::string err;
    CHECK(hd::compositeFrames(v, [&](int, const std::vector<uint32_t>* p) {
        pics.push_back(p ? *p : std::vector<uint32_t>{});
        return true;
    }, err));
    CHECK(pics.size() == 2);
    if (pics.size() == 2) {
        CHECK(pics[0].size() == 320u * 200u);
        CHECK(pics[0][0] == 0xFFFF0000u && pics[0][1] == 0xFF00FF00u && pics[0][2] == 0xFFFF0000u);
        // The overlay keeps frame 0 underneath.
        CHECK(pics[1][0] == 0xFFFF0000u && pics[1][1] == 0xFF00FF00u);
    }
}

static void testConvert() {
    Bytes v = testVideo();
    hd::ConvertOptions opt;
    opt.quality = 101;
    std::string path = scratch("video.hdv").string(), err;
    CHECK(hd::convertVideo(v, path, opt, err));
    if (!err.empty()) std::fprintf(stderr, "convert: %s\n", err.c_str());
    CHECK(!fs::exists(path + ".part"));
    hd::Pack p;
    CHECK(p.open(path, hd::crc32(v.data(), v.size())));
    CHECK(p.header().frames == 2 && p.hdWidth() == 1440 && p.hdHeight() == 1080);
    const auto* f = p.frame(0);
    CHECK(f != nullptr);
    if (f) {
        uint32_t c = (*f)[0];   // top-left HD pixel: mostly red
        CHECK(((c >> 16) & 0xFF) > 200 && ((c >> 8) & 0xFF) < 80);
        uint32_t far = (*f)[size_t(540) * 1440 + 1300];   // nothing drawn there: black
        CHECK((far & 0xFFFFFF) == 0);
    }
    CHECK(!hd::convertVideo(Bytes{1, 2, 3}, scratch("bad.hdv").string(), opt, err));
}

int main() {
    for (auto* t : {testCrc, testWebpLossless, testPack, testResample, testComposite, testConvert}) t();
    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "megarace_hd_tests", ec);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
