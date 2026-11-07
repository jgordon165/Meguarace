// megarace-hd: makes HD video packs from the player's own MegaRace CD.
//
//   megarace-hd --data <your MegaRace CD or folder> [options]
//
// The packs go to the HD folder set in the game (Options > HD FOLDER; by
// default <your Meguarace user folder>/hd), where the game reads them. They
// are made from the game's videos, so they are still the game's content:
// keep them for yourself, don't share them.
//
// No option names a file or a program: the folder comes from the game's
// setting, pack names only from the archive's (checked) video names, and
// Real-ESRGAN only from the realesrgan/ folder next to this program.

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "formats/cryo.h"
#include "hd/convert.h"
#include "hd/hdconfig.h"
#include "hd/hdpack.h"
#include "vfs.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const char* kUsage =
    "megarace-hd --data <MegaRace CD or folder> [options]\n"
    "\n"
    "Makes upscaled copies of the game's videos for the HD mode, from your own copy\n"
    "of the game. Run it once; the game picks the packs up by itself.\n"
    "\n"
    "  The packs go to the HD folder set in the game (Options > HD FOLDER),\n"
    "  by default your Meguarace user folder /hd.\n"
    "  (pictures are 1440x1080, the 4:3 picture of 1080p; smaller display modes\n"
    "   are scaled down from them by the game)\n"
    "  --quality Q           WebP quality 1..100, or 'lossless' (default 90)\n"
    "  --upscaler U          'realesrgan' or 'builtin' (default: realesrgan if it is\n"
    "                        in the realesrgan folder next to this program, else builtin)\n"
    "  --model NAME          Real-ESRGAN model: realesr-animevideov3 (default),\n"
    "                        realesrgan-x4plus or realesrgan-x4plus-anime\n"
    "  --only PATTERN        only videos matching, e.g. 'N*.HNM' (case-insensitive)\n"
    "  --jobs N              worker threads (default: all cores)\n"
    "  --force               remake packs that are already up to date\n"
    "  --list                list the videos and exit\n";

static bool globMatch(const char* p, const char* s) {
    for (; *p; ++p, ++s) {
        if (*p == '*') {
            for (; *s; ++s) if (globMatch(p + 1, s)) return true;
            return globMatch(p + 1, s);
        }
        if (!*s) return false;
        if (*p != '?' && std::toupper(uint8_t(*p)) != std::toupper(uint8_t(*s))) return false;
    }
    return !*s;
}

static bool endsWithHnm(const std::string& n) {
    if (n.size() < 4) return false;
    std::string e = n.substr(n.size() - 4);
    for (auto& c : e) c = char(std::toupper(uint8_t(c)));
    return e == ".HNM";
}

static std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

static std::string exeDir() {
    char* b = SDL_GetBasePath();
    std::string r = b ? b : "";
    SDL_free(b);
    return r;
}

static std::string userDir() {
    char* p = SDL_GetPrefPath("Meguarace", "Meguarace");
    std::string r = p ? p : "";
    SDL_free(p);
    return r;
}

int main(int argc, char** argv) {
    std::string data, only, upscaler, realesrgan;
    hd::ConvertOptions opt;
    bool force = false, list = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if      (a == "--data")       data = next();
        else if (a == "--quality")    { std::string q = next(); opt.quality = q == "lossless" ? 101 : std::atoi(q.c_str()); }
        else if (a == "--upscaler")   upscaler = next();
        else if (a == "--model") {
            const char* m = hd::knownModel(next());
            if (!m) { std::fprintf(stderr, "--model: unknown model\n%s", kUsage); return 2; }
            opt.model = m;
        }
        else if (a == "--only")       only = next();
        else if (a == "--jobs")       opt.jobs = std::atoi(next().c_str());
        else if (a == "--force")      force = true;
        else if (a == "--list")       list = true;
        else { std::fputs(kUsage, a == "--help" || a == "-h" ? stdout : stderr); return a == "--help" || a == "-h" ? 0 : 2; }
    }
    if (data.empty()) { std::fputs(kUsage, stderr); return 2; }
    if (opt.quality < 1) { std::fprintf(stderr, "--quality must be 1..100 or lossless\n"); return 2; }

    Vfs vfs;
    if (!vfs.mount(data)) { std::fprintf(stderr, "no such folder: %s\n", data.c_str()); return 1; }
    CryoDat dat;
    if (!dat.open(vfs, "MEGARACE.DAT")) {
        std::fprintf(stderr, "MEGARACE.DAT not found in %s\n", data.c_str());
        return 1;
    }

    // Videos inside the archive, then any loose ones on the disc (the engine
    // looks in the same two places, in the same order).
    // Only names as the archive has them (B01.HNM) are taken: a pack's file
    // name is the video's, so anything else could point outside the folder.
    struct Video { std::string name; std::string stem; bool inDat; };
    std::vector<Video> videos;
    for (const auto& e : dat.entries()) {
        std::string stem = hd::safeVideoStem(e.name);
        if (!stem.empty()) videos.push_back({upper(e.name), stem, true});
        else if (endsWithHnm(e.name)) std::fprintf(stderr, "skipping a video with an odd name in the archive\n");
    }
    for (const auto& e : vfs.list("")) {
        std::string stem = hd::safeVideoStem(e.name);
        if (!e.isDir && !stem.empty() &&
            std::none_of(videos.begin(), videos.end(), [&](const Video& v) { return v.stem == stem; }))
            videos.push_back({upper(e.name), stem, false});
    }
    if (!only.empty())
        videos.erase(std::remove_if(videos.begin(), videos.end(),
                                    [&](const Video& v) { return !globMatch(only.c_str(), v.name.c_str()); }),
                     videos.end());
    if (videos.empty()) { std::fprintf(stderr, "no videos found\n"); return 1; }

    if (list) {
        for (const auto& v : videos) {
            auto d = v.inDat ? dat.read(v.name) : vfs.read(v.name);
            HnmVideo h;
            std::printf("%-14s %6d frames%s\n", v.name.c_str(), d && h.load(*d) ? h.frameCount() : -1,
                        v.inDat ? "" : "  (on disc)");
        }
        return 0;
    }

    // Upscaler: Real-ESRGAN when asked for or bundled next to this program.
#ifdef _WIN32
    const char* esrganName = "realesrgan-ncnn-vulkan.exe";
#else
    const char* esrganName = "realesrgan-ncnn-vulkan";
#endif
    {
        fs::path bundled = fs::path(exeDir()) / "realesrgan" / esrganName;
        if (fs::exists(bundled)) realesrgan = bundled.string();
    }
    if (upscaler.empty()) upscaler = realesrgan.empty() ? "builtin" : "realesrgan";
    if (upscaler == "realesrgan") {
        if (realesrgan.empty()) {
            std::fprintf(stderr, "Real-ESRGAN not found: put realesrgan-ncnn-vulkan and its models folder in\n"
                                 "%s\n", (fs::path(exeDir()) / "realesrgan").string().c_str());
            return 2;
        }
        opt.upscaler = hd::ConvertOptions::Upscaler::RealEsrgan;
        opt.realesrgan = realesrgan;
        opt.batch = 64;   // its 1280x800 output is 4 MB a frame; keep batches small
    } else if (upscaler != "builtin") {
        std::fprintf(stderr, "--upscaler must be realesrgan or builtin\n");
        return 2;
    }

    const std::string user = userDir();
    if (user.empty()) { std::fprintf(stderr, "no user folder: %s\n", SDL_GetError()); return 1; }
    const std::string out = hd::packFolder(user);
    std::error_code ec;
    fs::create_directories(out, ec);
    if (ec) { std::fprintf(stderr, "cannot make %s: %s\n", out.c_str(), ec.message().c_str()); return 1; }
    opt.tempDir = (fs::path(out) / ".work").string();
    {
        std::ofstream note(fs::path(out) / "README.txt");
        note << "MegaRace HD video packs, made by megarace-hd from your own copy of the game.\n"
                "They are derived from the game's videos and remain the game's content:\n"
                "keep them for your own use and do not share or upload them.\n";
    }

    std::printf("%zu videos -> %s (%dx%d, %s)\n", videos.size(), out.c_str(), opt.pictureW, opt.pictureH,
                opt.upscaler == hd::ConvertOptions::Upscaler::RealEsrgan ? "Real-ESRGAN" : "built-in upscaler");
    int failed = 0, skipped = 0, n = 0;
    for (const auto& v : videos) {
        ++n;
        auto d = v.inDat ? dat.read(v.name) : vfs.read(v.name);
        if (!d) { std::fprintf(stderr, "%s: unreadable\n", v.name.c_str()); ++failed; continue; }
        fs::path packPath = fs::path(out) / (v.stem + ".hdv");
        if (!force) {
            hd::Pack existing;
            if (existing.open(packPath.string(), hd::crc32(d->data(), d->size())) &&
                existing.header().pictureW == opt.pictureW && existing.header().pictureH == opt.pictureH) {
                ++skipped;
                continue;
            }
        }
        auto t0 = std::chrono::steady_clock::now();
        opt.progress = [&](int done, int total) {
            std::printf("\r[%d/%zu] %-12s %5d/%d frames", n, videos.size(), v.name.c_str(), done, total);
            std::fflush(stdout);
        };
        std::string err;
        if (!hd::convertVideo(*d, packPath.string(), opt, err)) {
            std::fprintf(stderr, "\n%s: %s\n", v.name.c_str(), err.c_str());
            ++failed;
            continue;
        }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %.1fs, %.1f MB\n", s, double(fs::file_size(packPath, ec)) / 1e6);
    }
    fs::remove_all(opt.tempDir, ec);
    std::printf("done: %zu made, %d up to date, %d failed\n", videos.size() - size_t(skipped + failed), skipped, failed);
    return failed ? 1 : 0;
}
