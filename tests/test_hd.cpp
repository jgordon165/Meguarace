// HD mode tests: pack format, compositing and conversion, on hand-built
// inputs only (no game data in the repository, as in test_formats.cpp).

#include "formats/cryo.h"
#include "hd/convert.h"
#include "hd/compose.h"
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
    const int K = 2, W = 4, H = 2;   // tiny canvas: 8x4 HD pixels
    std::vector<uint32_t> a(size_t(W * K * H * K), 0xFF112233u), b(a.size(), 0xFFAABBCCu);
    hd::PackHeader h;
    h.scale = K; h.width = W; h.height = H; h.frames = 3; h.sourceCrc = 0x1234;
    std::string path = scratch("pack.hdv").string();
    hd::PackWriter w;
    CHECK(w.open(path, h));
    CHECK(w.addFrame(hd::encodeWebp(a.data(), W * K, H * K, 101)));
    CHECK(w.addRepeat());
    CHECK(w.addFrame(hd::encodeWebp(b.data(), W * K, H * K, 101)));
    CHECK(!w.addRepeat());   // one too many
    CHECK(w.finish());

    hd::Pack p;
    CHECK(!p.open(path, 0x9999));          // made from a different video
    hd::Pack q;
    CHECK(q.open(path, 0x1234));
    CHECK(q.header().scale == K && q.header().frames == 3 && q.hdWidth() == 8);
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

static void testCompose() {
    // 2x1 screen at K=2: pixel 0 indexed (palette 7), pixel 1 from HD.
    const uint8_t fb[2] = {7, 0};
    const uint8_t src[2] = {0, 1};
    uint32_t pal[256] = {};
    pal[7] = 0xFF070707u;
    std::vector<uint32_t> hdp(8), out(8);
    for (size_t i = 0; i < 8; ++i) hdp[i] = 0xFF000000u | uint32_t(i);
    hd::compose(fb, src, pal, hdp.data(), 2, 1, 2, out.data());
    // Rows of 4 HD pixels: [pal pal hd2 hd3] [pal pal hd6 hd7]
    CHECK(out[0] == pal[7] && out[1] == pal[7] && out[4] == pal[7] && out[5] == pal[7]);
    CHECK(out[2] == hdp[2] && out[3] == hdp[3] && out[6] == hdp[6] && out[7] == hdp[7]);
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
    opt.scale = 2;
    opt.quality = 101;
    std::string path = scratch("video.hdv").string(), err;
    CHECK(hd::convertVideo(v, path, opt, err));
    if (!err.empty()) std::fprintf(stderr, "convert: %s\n", err.c_str());
    CHECK(!fs::exists(path + ".part"));
    hd::Pack p;
    CHECK(p.open(path, hd::crc32(v.data(), v.size())));
    CHECK(p.header().frames == 2 && p.hdWidth() == 640 && p.hdHeight() == 400);
    const auto* f = p.frame(0);
    CHECK(f != nullptr);
    if (f) {
        uint32_t c = (*f)[0];   // top-left HD pixel: mostly red
        CHECK(((c >> 16) & 0xFF) > 200 && ((c >> 8) & 0xFF) < 80);
        uint32_t far = (*f)[size_t(100) * 640 + 300];   // nothing drawn there: black
        CHECK((far & 0xFFFFFF) == 0);
    }
    CHECK(!hd::convertVideo(Bytes{1, 2, 3}, scratch("bad.hdv").string(), opt, err));
}

int main() {
    for (auto* t : {testCrc, testWebpLossless, testPack, testCompose, testComposite, testConvert}) t();
    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "megarace_hd_tests", ec);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
