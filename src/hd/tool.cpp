// megarace-hd: makes HD video packs from the player's own MegaRace CD.
//
//   megarace-hd --data <your MegaRace CD or folder> [options]
//
// The packs are written to your user folder (the engine looks there by
// default) or to --out. They are made from the game's videos, so they are
// still the game's content: keep them for yourself, don't share them.

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "formats/cryo.h"
#include "hd/convert.h"
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
    "  --out <folder>        where packs go (default: your Meguarace user folder /hd)\n"
    "  --scale N             2, 3 or 4 (default 2)\n"
    "  --quality Q           WebP quality 1..100, or 'lossless' (default 90)\n"
    "  --upscaler U          'realesrgan' or 'builtin' (default: realesrgan if it is\n"
    "                        next to this program, else builtin)\n"
    "  --realesrgan <path>   realesrgan-ncnn-vulkan program to use\n"
    "  --model NAME          Real-ESRGAN model (default realesr-animevideov3)\n"
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

static std::string defaultOut() {
    char* p = SDL_GetPrefPath("Meguarace", "Meguarace");
    std::string r = p ? (fs::path(p) / "hd").string() : "hd";
    SDL_free(p);
    return r;
}

int main(int argc, char** argv) {
    std::string data, out, only, upscaler, realesrgan;
    hd::ConvertOptions opt;
    bool force = false, list = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if      (a == "--data")       data = next();
        else if (a == "--out")        out = next();
        else if (a == "--scale")      opt.scale = std::atoi(next().c_str());
        else if (a == "--quality")    { std::string q = next(); opt.quality = q == "lossless" ? 101 : std::atoi(q.c_str()); }
        else if (a == "--upscaler")   upscaler = next();
        else if (a == "--realesrgan") realesrgan = next();
        else if (a == "--model")      opt.model = next();
        else if (a == "--only")       only = next();
        else if (a == "--jobs")       opt.jobs = std::atoi(next().c_str());
        else if (a == "--force")      force = true;
        else if (a == "--list")       list = true;
        else { std::fputs(kUsage, a == "--help" || a == "-h" ? stdout : stderr); return a == "--help" || a == "-h" ? 0 : 2; }
    }
    if (data.empty()) { std::fputs(kUsage, stderr); return 2; }
    if (opt.scale < 2 || opt.scale > 4) { std::fprintf(stderr, "--scale must be 2, 3 or 4\n"); return 2; }
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
    struct Video { std::string name; bool inDat; };
    std::vector<Video> videos;
    for (const auto& e : dat.entries())
        if (endsWithHnm(e.name)) videos.push_back({upper(e.name), true});
    for (const auto& e : vfs.list(""))
        if (!e.isDir && endsWithHnm(e.name) &&
            std::none_of(videos.begin(), videos.end(), [&](const Video& v) { return v.name == upper(e.name); }))
            videos.push_back({upper(e.name), false});
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
    if (realesrgan.empty()) {
        fs::path bundled = fs::path(exeDir()) / "realesrgan" / esrganName;
        if (fs::exists(bundled)) realesrgan = bundled.string();
    }
    if (upscaler.empty()) upscaler = realesrgan.empty() ? "builtin" : "realesrgan";
    if (upscaler == "realesrgan") {
        if (realesrgan.empty()) { std::fprintf(stderr, "Real-ESRGAN not found; pass --realesrgan <path>\n"); return 2; }
        opt.upscaler = hd::ConvertOptions::Upscaler::RealEsrgan;
        opt.realesrgan = realesrgan;
    } else if (upscaler != "builtin") {
        std::fprintf(stderr, "--upscaler must be realesrgan or builtin\n");
        return 2;
    }

    if (out.empty()) out = defaultOut();
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

    std::printf("%zu videos -> %s (x%d, %s)\n", videos.size(), out.c_str(), opt.scale,
                opt.upscaler == hd::ConvertOptions::Upscaler::RealEsrgan ? "Real-ESRGAN" : "built-in upscaler");
    int failed = 0, skipped = 0, n = 0;
    for (const auto& v : videos) {
        ++n;
        auto d = v.inDat ? dat.read(v.name) : vfs.read(v.name);
        if (!d) { std::fprintf(stderr, "%s: unreadable\n", v.name.c_str()); ++failed; continue; }
        fs::path packPath = fs::path(out) / (v.name.substr(0, v.name.size() - 4) + ".hdv");
        if (!force) {
            hd::Pack existing;
            if (existing.open(packPath.string(), hd::crc32(d->data(), d->size())) &&
                existing.header().scale == opt.scale) { ++skipped; continue; }
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
