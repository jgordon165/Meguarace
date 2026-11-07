// HD packs as the game reads them: format, WebP, the HD folder setting and
// the video-name check. Hand-built inputs only (no game data in the repository).

#include "hd/hdconfig.h"
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
    fs::path p = fs::temp_directory_path() / "megarace_hdpack_tests";
    fs::create_directories(p);
    return p / name;
}

static void testConfig() {
    // Only plain archive names become pack names.
    CHECK(hd::safeVideoStem("B01.HNM") == "B01" && hd::safeVideoStem("ns11.hnm") == "NS11");
    CHECK(hd::safeVideoStem("VWBT2.HNM") == "VWBT2" && hd::safeVideoStem("A_B.HNM") == "A_B");
    for (const char* bad : {"../B01.HNM", "..\\B01.HNM", "C:B01.HNM", "B01.TXT", ".HNM", "B01", "TOOLONGNAME1.HNM",
                            "B 1.HNM", "/etc/x.HNM"})
        CHECK(hd::safeVideoStem(bad).empty());
    // The folder setting: default, set, cleared.
    fs::path user = scratch("user");
    fs::create_directories(user);
    CHECK(hd::configuredFolder(user.string()).empty());
    CHECK(hd::packFolder(user.string()) == (user / "hd").string());
    CHECK(hd::setConfiguredFolder(user.string(), "D:\\Games\\MegaRace HD"));
    CHECK(hd::configuredFolder(user.string()) == "D:\\Games\\MegaRace HD");
    CHECK(hd::packFolder(user.string()) == "D:\\Games\\MegaRace HD");
    CHECK(hd::setConfiguredFolder(user.string(), ""));
    CHECK(hd::configuredFolder(user.string()).empty());
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

int main() {
    for (auto* t : {testConfig, testCrc, testWebpLossless, testPack}) t();
    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "megarace_hdpack_tests", ec);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
