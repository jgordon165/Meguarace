// hd-conversion-utility: resampling, compositing and converting a video, and
// the Real-ESRGAN model list. Hand-built inputs only (no game data in the repository).

#include "formats/cryo.h"
#include "hd/convert.h"
#include "hd/hdpack.h"
#include "hd/sprpack.h"

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
    fs::path p = fs::temp_directory_path() / "megarace_hdconvert_tests";
    fs::create_directories(p);
    return p / name;
}

static void testModels() {
    // Only known Real-ESRGAN model names reach its command line.
    CHECK(hd::knownModel("realesr-animevideov3") != nullptr);
    CHECK(hd::knownModel("x; rm -rf /") == nullptr && hd::knownModel("") == nullptr);
}

static void testSharp() {
    // Line doubled and interpolated: a flat picture stays flat, and away from
    // the seams every pixel is exactly the original's.
    std::vector<uint32_t> flat(size_t(320 * 200), 0xFF336699u);
    auto up = hd::resampleSharp(flat, 320, 200, 1440, 1080);
    CHECK(up.size() == 1440u * 1080u && up[size_t(540) * 1440 + 720] == 0xFF336699u);
    std::vector<uint32_t> two = {0xFF000000u, 0xFFFFFFFFu};   // 2x1: black, white
    auto r = hd::resampleSharp(two, 2, 1, 9, 1);               // 4.5x: 4 black, a seam, 4 white
    CHECK(r[0] == 0xFF000000u && r[3] == 0xFF000000u && r[5] == 0xFFFFFFFFu && r[8] == 0xFFFFFFFFu);
    CHECK((r[4] & 0xFF) > 100 && (r[4] & 0xFF) < 155);         // the seam pixel: half and half
    // No ringing: never outside the originals' range (Catmull-Rom overshoots).
    for (uint32_t v : r) CHECK((v & 0xFF) <= 255);
}

// A sprite set as stored (see test_formats.cpp): palette block, offsets, raw images.
static Bytes spriteSet(const Bytes& pal, const std::vector<Bytes>& images) {
    Bytes f;
    auto put16 = [&](Bytes& b, unsigned v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); };
    put16(f, unsigned(2 + pal.size()));
    f.insert(f.end(), pal.begin(), pal.end());
    size_t first = images.size() * 2, at = first;
    for (const auto& im : images) { put16(f, unsigned(at)); at += im.size(); }
    for (const auto& im : images) f.insert(f.end(), im.begin(), im.end());
    return f;
}

static void testSprites() {
    // 3x2 raw images (w, 0, h, 0xFF, pixels).
    Bytes a = {3, 0, 2, 0xFF, 0, 5, 5, 5, 6, 0};
    // Colour: its palette defines 5 and 6.
    Bytes own = spriteSet({5, 2, 63, 0, 0, 0, 63, 0, 0xFF, 0xFF}, {a});
    // Indexed: no palette; colours from the screen.
    Bytes other = spriteSet({0xFF, 0xFF}, {a});
    hd::ConvertOptions opt;
    std::string kind, err;
    std::string p1 = scratch("own.hds").string(), p2 = scratch("other.hds").string();
    CHECK(hd::convertSprites(own, p1, opt, kind, err) && kind == "colour");
    CHECK(hd::convertSprites(other, p2, opt, kind, err) && kind == "indexed");
    hd::SpritePackData c, i;
    CHECK(hd::readSpritePack(p1, hd::crc32(own.data(), own.size()), c, err));
    CHECK(hd::readSpritePack(p2, hd::crc32(other.data(), other.size()), i, err));
    CHECK(!hd::readSpritePack(p1, 1234, c, err));                         // other bytes: refused
    CHECK(c.kind == 0 && c.upscaler == hd::kUpscalerSharp && c.images.size() == 1);
    const auto& ci = c.images[0];
    CHECK(ci.w == 3 && ci.h == 2 && ci.hdW == hd::hdSpan(3, 1440, 320) && ci.hdH == hd::hdSpan(2, 1080, 200));
    // Colour pack: top-left is transparent (0), the middle of the top row red.
    CHECK((ci.px[0] >> 24) < 40);
    uint32_t mid = ci.px[size_t(ci.hdH / 4) * size_t(ci.hdW) + size_t(ci.hdW / 2)];
    CHECK((mid >> 24) > 200 && ((mid >> 16) & 0xFF) > 200 && ((mid >> 8) & 0xFF) < 40);
    CHECK(c.palette[5 * 3] == 255 && c.palette[6 * 3 + 1] == 255);
    // Indexed pack: the index in red, alpha for the shape.
    const auto& ii = i.images[0];
    uint32_t imid = ii.px[size_t(ii.hdH / 4) * size_t(ii.hdW) + size_t(ii.hdW / 2)];
    CHECK(i.kind == 1 && ((imid >> 16) & 0xFF) == 5 && (imid >> 24) > 200);
    uint32_t ibot = ii.px[size_t(ii.hdH * 3 / 4) * size_t(ii.hdW) + size_t(ii.hdW / 2)];   // bottom-middle: colour 6
    CHECK(((ibot >> 16) & 0xFF) == 6);
    CHECK(!c.sharpened);
    // Dashboards: a strip (two halves upscaled as one) and sharpened, which
    // the pack records.
    CHECK(hd::spriteStrip("TJO_2") && !hd::spriteStrip("TJO") && !hd::spriteStrip("PANEL2"));
    CHECK(hd::dashboardSprite("TJO_2") && hd::dashboardSprite("TMA") && !hd::dashboardSprite("ZONES_") &&
          !hd::dashboardSprite("PANEL2") && !hd::dashboardSprite("FONTOPT"));
    Bytes b = {3, 0, 2, 0xFF, 5, 5, 6, 6, 6, 5};
    Bytes strip = spriteSet({5, 2, 63, 0, 0, 0, 63, 0, 0xFF, 0xFF}, {b, b});
    hd::ConvertOptions so;
    so.strip = true; so.sharpen = 2.0f;
    std::string p3 = scratch("strip.hds").string();
    CHECK(hd::convertSprites(strip, p3, so, kind, err) && kind == "colour");
    hd::SpritePackData sp;
    CHECK(hd::readSpritePack(p3, hd::crc32(strip.data(), strip.size()), sp, err));
    CHECK(sp.sharpened && sp.images.size() == 2 && sp.images[1].hdW == sp.images[0].hdW);
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
    for (auto* t : {testModels, testSharp, testSprites, testResample, testComposite, testConvert}) t();
    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "megarace_hdconvert_tests", ec);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
