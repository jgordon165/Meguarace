#include "hd/convert.h"

#include "formats/cryo.h"
#include "hd/hdpack.h"
#include "hd/sprpack.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace hd {

static constexpr int kW = 320, kH = 200;

bool compositeFrames(const std::vector<uint8_t>& hnm,
                     const std::function<bool(int, const std::vector<uint32_t>*)>& sink,
                     std::string& err) {
    HnmVideo v;
    if (!v.load(hnm)) { err = "not an HNM video"; return false; }
    std::vector<uint8_t> screen(size_t(kW) * kH, 0);
    std::vector<uint32_t> rgb(screen.size());
    HnmVideo::Frame f;
    for (int i = 0; i < v.frameCount(); ++i) {
        try {
            if (!v.decode(i, f)) { err = "frame " + std::to_string(i) + " unreadable"; return false; }
        } catch (const std::exception& ex) {
            err = "frame " + std::to_string(i) + ": " + ex.what();
            return false;
        }
        bool changed = f.paletteChanged || i == 0;
        if (f.hasVideo) {
            changed = true;
            for (int y = 0; y < f.h; ++y) {
                int Y = f.y + y;
                if (Y < 0 || Y >= kH) continue;
                for (int x = 0; x < f.w; ++x) {
                    int X = f.x + x;
                    if (X < 0 || X >= kW) continue;
                    uint8_t c = f.pixels[size_t(y) * size_t(f.w) + size_t(x)];
                    if (f.overlay && c == 0) continue;
                    screen[size_t(Y) * kW + size_t(X)] = c;
                }
            }
        }
        if (!changed) {
            if (!sink(i, nullptr)) return false;
            continue;
        }
        const auto& p = v.palette();
        for (size_t k = 0; k < screen.size(); ++k) {
            const uint8_t* c = &p[size_t(screen[k]) * 3];
            rgb[k] = 0xFF000000u | uint32_t(c[0]) << 16 | uint32_t(c[1]) << 8 | c[2];
        }
        if (!sink(i, &rgb)) return false;
    }
    return true;
}

static std::vector<uint8_t> argbToRgb(const std::vector<uint32_t>& a) {
    std::vector<uint8_t> r(a.size() * 3);
    for (size_t i = 0; i < a.size(); ++i) {
        r[i * 3] = uint8_t(a[i] >> 16); r[i * 3 + 1] = uint8_t(a[i] >> 8); r[i * 3 + 2] = uint8_t(a[i]);
    }
    return r;
}

static std::vector<uint32_t> rgbToArgb(const uint8_t* r, size_t n) {
    std::vector<uint32_t> a(n);
    for (size_t i = 0; i < n; ++i)
        a[i] = 0xFF000000u | uint32_t(r[i * 3]) << 16 | uint32_t(r[i * 3 + 1]) << 8 | r[i * 3 + 2];
    return a;
}

// Catmull-Rom (a = -0.5) kernel.
static float catmullRom(float x) {
    x = std::fabs(x);
    if (x < 1) return 1.5f * x * x * x - 2.5f * x * x + 1.0f;
    if (x < 2) return -0.5f * x * x * x + 2.5f * x * x - 4.0f * x + 2.0f;
    return 0;
}

// Per output index: first source tap and normalized weights. Pixel centres
// map as (i + 0.5) * n / N - 0.5; when shrinking, the kernel is widened by
// the ratio so every source pixel contributes (no aliasing).
struct Taps { std::vector<int> first; std::vector<std::vector<float>> w; };
static Taps taps(int n, int N) {
    Taps t;
    const float ratio = float(n) / float(N);
    const float support = 2.0f * std::max(1.0f, ratio);
    const float stretch = std::max(1.0f, ratio);
    t.first.resize(size_t(N));
    t.w.resize(size_t(N));
    for (int i = 0; i < N; ++i) {
        float c = (float(i) + 0.5f) * ratio - 0.5f;
        int lo = int(std::floor(c - support)) + 1, hi = int(std::floor(c + support));
        t.first[size_t(i)] = lo;
        float sum = 0;
        for (int k = lo; k <= hi; ++k) {
            float wgt = catmullRom((float(k) - c) / stretch);
            t.w[size_t(i)].push_back(wgt);
            sum += wgt;
        }
        for (auto& wgt : t.w[size_t(i)]) wgt /= sum;
    }
    return t;
}

std::vector<uint32_t> resample(const std::vector<uint32_t>& argb, int w, int h, int W, int H) {
    const Taps tx = taps(w, W), ty = taps(h, H);
    auto clampi = [](int v, int hi) { return v < 0 ? 0 : v > hi ? hi : v; };
    // Horizontal pass into float RGB, then vertical pass to bytes.
    std::vector<float> tmp(size_t(W) * size_t(h) * 3);
    for (int y = 0; y < h; ++y)
        for (int X = 0; X < W; ++X) {
            float acc[3] = {0, 0, 0};
            const auto& wt = tx.w[size_t(X)];
            for (size_t t = 0; t < wt.size(); ++t) {
                uint32_t c = argb[size_t(y) * size_t(w) + size_t(clampi(tx.first[size_t(X)] + int(t), w - 1))];
                acc[0] += wt[t] * float((c >> 16) & 0xFF);
                acc[1] += wt[t] * float((c >> 8) & 0xFF);
                acc[2] += wt[t] * float(c & 0xFF);
            }
            float* o = &tmp[(size_t(y) * size_t(W) + size_t(X)) * 3];
            o[0] = acc[0]; o[1] = acc[1]; o[2] = acc[2];
        }
    std::vector<uint32_t> out(size_t(W) * size_t(H));
    auto byte = [](float v) { return uint32_t(v <= 0 ? 0 : v >= 255 ? 255 : int(v + 0.5f)); };
    for (int Y = 0; Y < H; ++Y) {
        const auto& wt = ty.w[size_t(Y)];
        for (int X = 0; X < W; ++X) {
            float acc[3] = {0, 0, 0};
            for (size_t t = 0; t < wt.size(); ++t) {
                const float* c = &tmp[(size_t(clampi(ty.first[size_t(Y)] + int(t), h - 1)) * size_t(W) + size_t(X)) * 3];
                acc[0] += wt[t] * c[0]; acc[1] += wt[t] * c[1]; acc[2] += wt[t] * c[2];
            }
            out[size_t(Y) * size_t(W) + size_t(X)] = 0xFF000000u | byte(acc[0]) << 16 | byte(acc[1]) << 8 | byte(acc[2]);
        }
    }
    return out;
}

// Area coverage: output pixel X spans [X*n/N, (X+1)*n/N) in the source;
// each source pixel weighs by how much of that span it covers. Enlarging,
// that is the source pixel repeated with only the seams blended.
static Taps coverage(int n, int N) {
    Taps t;
    t.first.resize(size_t(N));
    t.w.resize(size_t(N));
    for (int i = 0; i < N; ++i) {
        double a = double(i) * n / N, b = double(i + 1) * n / N;
        int lo = int(std::floor(a)), hi = std::min(n - 1, int(std::ceil(b)) - 1);
        t.first[size_t(i)] = lo;
        for (int k = lo; k <= hi; ++k)
            t.w[size_t(i)].push_back(float((std::min(b, double(k + 1)) - std::max(a, double(k))) / (b - a)));
    }
    return t;
}

static std::vector<uint32_t> separable(const std::vector<uint32_t>& argb, int w, int h, int W, int H,
                                       const Taps& tx, const Taps& ty) {
    auto clampi = [](int v, int hi) { return v < 0 ? 0 : v > hi ? hi : v; };
    std::vector<float> tmp(size_t(W) * size_t(h) * 3);
    for (int y = 0; y < h; ++y)
        for (int X = 0; X < W; ++X) {
            float acc[3] = {0, 0, 0};
            const auto& wt = tx.w[size_t(X)];
            for (size_t t = 0; t < wt.size(); ++t) {
                uint32_t c = argb[size_t(y) * size_t(w) + size_t(clampi(tx.first[size_t(X)] + int(t), w - 1))];
                acc[0] += wt[t] * float((c >> 16) & 0xFF);
                acc[1] += wt[t] * float((c >> 8) & 0xFF);
                acc[2] += wt[t] * float(c & 0xFF);
            }
            float* o = &tmp[(size_t(y) * size_t(W) + size_t(X)) * 3];
            o[0] = acc[0]; o[1] = acc[1]; o[2] = acc[2];
        }
    std::vector<uint32_t> out(size_t(W) * size_t(H));
    auto byte = [](float v) { return uint32_t(v <= 0 ? 0 : v >= 255 ? 255 : int(v + 0.5f)); };
    for (int Y = 0; Y < H; ++Y) {
        const auto& wt = ty.w[size_t(Y)];
        for (int X = 0; X < W; ++X) {
            float acc[3] = {0, 0, 0};
            for (size_t t = 0; t < wt.size(); ++t) {
                const float* c = &tmp[(size_t(clampi(ty.first[size_t(Y)] + int(t), h - 1)) * size_t(W) + size_t(X)) * 3];
                acc[0] += wt[t] * c[0]; acc[1] += wt[t] * c[1]; acc[2] += wt[t] * c[2];
            }
            out[size_t(Y) * size_t(W) + size_t(X)] = 0xFF000000u | byte(acc[0]) << 16 | byte(acc[1]) << 8 | byte(acc[2]);
        }
    }
    return out;
}

std::vector<uint32_t> resampleSharp(const std::vector<uint32_t>& argb, int w, int h, int W, int H) {
    return separable(argb, w, h, W, H, coverage(w, W), coverage(h, H));
}

std::vector<uint32_t> upscale(const ConvertOptions& opt, const std::vector<uint32_t>& argb, int w, int h, int W, int H) {
    return opt.upscaler == ConvertOptions::Upscaler::Smooth ? resample(argb, w, h, W, H)
                                                            : resampleSharp(argb, w, h, W, H);
}

template <class F>
static void parallelFor(int n, int jobs, F&& fn) {
    if (jobs <= 0) jobs = int(std::max(1u, std::thread::hardware_concurrency()));
    jobs = std::min(jobs, std::max(n, 1));
    std::atomic<int> next{0};
    auto work = [&] { for (int i; (i = next++) < n;) fn(i); };
    std::vector<std::thread> pool;
    for (int t = 1; t < jobs; ++t) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
}

const char* const kModels[] = {"realesr-animevideov3", "realesrgan-x4plus", "realesrgan-x4plus-anime", nullptr};

const char* knownModel(const std::string& name) {
    for (int i = 0; kModels[i]; ++i)
        if (name == kModels[i]) return kModels[i];
    return nullptr;
}

// Runs a program with these arguments, directly: no shell, so nothing in
// them is interpreted. Its output is discarded. Returns its exit code, or -1
// if it couldn't be started.
static int runProgram(const fs::path& exe, const std::vector<std::string>& args) {
#ifdef _WIN32
    // Build the command line the way CommandLineToArgvW splits it.
    auto quoteArg = [](const std::wstring& a) {
        std::wstring q = L"\"";
        size_t slashes = 0;
        for (wchar_t c : a) {
            if (c == L'\\') { ++slashes; continue; }
            if (c == L'"') q.append(slashes * 2 + 1, L'\\');
            else q.append(slashes, L'\\');
            slashes = 0;
            q += c;
        }
        q.append(slashes * 2, L'\\');
        return q + L"\"";
    };
    auto wide = [](const std::string& s) {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(size_t(n > 0 ? n - 1 : 0), L'\0');
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    };
    std::wstring app = exe.wstring();
    std::wstring line = quoteArg(app);
    for (const auto& a : args) line += L" " + quoteArg(wide(a));
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = si.hStdError = nul;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(app.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             nullptr, &si, &pi);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return int(code);
#else
    std::string path = exe.string();
    std::vector<char*> argv;
    argv.push_back(path.data());
    std::vector<std::string> copy = args;
    for (auto& a : copy) argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, path.c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// Real-ESRGAN's video model, run at 4x: 320x200 -> 1280x800.
static constexpr int kEsrganScale = 4;

// Runs Real-ESRGAN over `pics` (in place), each sizes[i] = {w, h}; each
// comes back 4x larger. Video frames are all 320x200; sprites vary.
static bool upscaleRealEsrgan(std::vector<std::vector<uint32_t>>& pics, const std::vector<std::pair<int, int>>& sizes,
                              const ConvertOptions& opt, std::string& err) {
    fs::path base = opt.tempDir.empty() ? fs::temp_directory_path() / "hd-conversion-utility" : fs::path(opt.tempDir);
    fs::path in = base / "in", out = base / "out";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(in, ec);
    fs::create_directories(out, ec);
    if (ec) { err = "cannot make scratch folder " + base.string(); return false; }

    for (size_t i = 0; i < pics.size(); ++i) {
        auto rgb = argbToRgb(pics[i]);
        char name[32];
        std::snprintf(name, sizeof name, "%06zu.png", i);
        const int pw = sizes[i].first, ph = sizes[i].second;
        if (!stbi_write_png((in / name).string().c_str(), pw, ph, 3, rgb.data(), pw * 3)) {
            err = "cannot write scratch frame";
            return false;
        }
    }
    const char* model = knownModel(opt.model);
    if (!model) { err = "unknown Real-ESRGAN model " + opt.model; return false; }
    std::vector<std::string> args = {"-i", in.string(), "-o", out.string(), "-n", model,
                                     "-s", std::to_string(kEsrganScale), "-f", "png"};
    // Its models ship in a "models" folder beside it; it otherwise looks in
    // the current folder.
    fs::path models = fs::path(opt.realesrgan).parent_path() / "models";
    if (fs::is_directory(models, ec)) { args.push_back("-m"); args.push_back(models.string()); }
    int rc = runProgram(opt.realesrgan, args);
    if (rc != 0) {
        err = rc < 0 ? "Real-ESRGAN could not be started (" + opt.realesrgan + ")"
                     : "Real-ESRGAN failed (exit code " + std::to_string(rc) + ")";
        return false;
    }

    for (size_t i = 0; i < pics.size(); ++i) {
        const int W = sizes[i].first * kEsrganScale, H = sizes[i].second * kEsrganScale;
        char name[32];
        std::snprintf(name, sizeof name, "%06zu.png", i);
        int w = 0, h = 0, n = 0;
        uint8_t* px = stbi_load((out / name).string().c_str(), &w, &h, &n, 3);
        if (!px) { err = "Real-ESRGAN wrote no " + std::string(name); return false; }
        if (w != W || h != H) {
            stbi_image_free(px);
            err = "Real-ESRGAN output is " + std::to_string(w) + "x" + std::to_string(h) +
                  ", expected " + std::to_string(W) + "x" + std::to_string(H) +
                  " (does model " + opt.model + " do 4x?)";
            return false;
        }
        pics[i] = rgbToArgb(px, size_t(w) * size_t(h));
        stbi_image_free(px);
    }
    fs::remove_all(base, ec);
    return true;
}

bool convertVideo(const std::vector<uint8_t>& hnm, const std::string& outPath,
                  const ConvertOptions& opt, std::string& err) {
    HnmVideo probe;
    if (!probe.load(hnm)) { err = "not an HNM video"; return false; }
    const int total = probe.frameCount();

    PackHeader h;
    h.pictureW = opt.pictureW;
    h.pictureH = opt.pictureH;
    h.upscaler = opt.upscaler == ConvertOptions::Upscaler::RealEsrgan ? kUpscalerRealEsrgan
                     : opt.upscaler == ConvertOptions::Upscaler::Smooth ? kUpscalerSmooth : kUpscalerSharp;
    h.frames = uint32_t(total);
    h.sourceCrc = crc32(hnm.data(), hnm.size());
    const std::string part = outPath + ".part";
    PackWriter w;
    if (!w.open(part, h)) { err = w.error(); return false; }

    // Frames are gathered in batches: upscaled, encoded in parallel, written in order.
    std::vector<std::vector<uint32_t>> pics;       // pictures in this batch
    std::vector<int> picOf;                        // entry -> index in pics, -1 repeat
    int done = 0;

    auto flush = [&]() -> bool {
        // Real-ESRGAN upscales the batch 4x on the GPU; the rest (resize to
        // the picture size, encode) runs on every CPU core.
        int srcW = kW, srcH = kH;
        if (opt.upscaler == ConvertOptions::Upscaler::RealEsrgan && !pics.empty()) {
            if (!upscaleRealEsrgan(pics, std::vector<std::pair<int, int>>(pics.size(), {kW, kH}), opt, err))
                return false;
            srcW = kW * kEsrganScale;
            srcH = kH * kEsrganScale;
        }
        std::vector<std::vector<uint8_t>> webp(pics.size());
        parallelFor(int(pics.size()), opt.jobs, [&](int i) {
            auto& p = pics[size_t(i)];
            // Real-ESRGAN's 1280x800 to the picture size: a small smooth step.
            // Built-in: the chosen upscaler straight from 320x200.
            if (srcW != kW) p = resample(p, srcW, srcH, opt.pictureW, opt.pictureH);
            else p = upscale(opt, p, srcW, srcH, opt.pictureW, opt.pictureH);
            webp[size_t(i)] = encodeWebp(p.data(), opt.pictureW, opt.pictureH, opt.quality);
            p = {};
        });
        for (int p : picOf) {
            bool ok = p < 0 ? w.addRepeat() : w.addFrame(webp[size_t(p)]);
            if (!ok) { err = w.error(); return false; }
        }
        done += int(picOf.size());
        if (opt.progress) opt.progress(done, total);
        pics.clear();
        picOf.clear();
        return true;
    };

    bool ok = compositeFrames(hnm, [&](int, const std::vector<uint32_t>* pic) {
        if (pic) { picOf.push_back(int(pics.size())); pics.push_back(*pic); }
        else picOf.push_back(-1);
        if (int(pics.size()) >= std::max(1, opt.batch)) return flush();
        return true;
    }, err);
    if (ok && !picOf.empty()) ok = flush();
    if (ok && !w.finish()) { err = w.error(); ok = false; }

    std::error_code ec;
    if (!ok) { fs::remove(part, ec); return false; }
    fs::remove(outPath, ec);   // rename doesn't replace on Windows
    fs::rename(part, outPath, ec);
    if (ec) { err = "cannot rename " + part + ": " + ec.message(); return false; }
    return true;
}

// ---- menu sprites ------------------------------------------------------------

const char* const kMenuSprites[] = {"PANEL2", "KONTROL", "KONTROL6", "CONTROL2", "CONTROL4",
                                    "FONTOPT", "FONTOPT2", "FONTOPT3", "FONTHALL", nullptr};

// The set's own palette (8-bit), as Video::applyPaletteBlocks applies it at
// shift 0, and which indices it defines. Index 0 stays black (transparent).
static void ownPalette(const std::vector<uint8_t>& p, std::vector<uint8_t>& pal, bool defined[256]) {
    pal.assign(768, 0);
    std::fill(defined, defined + 256, false);
    size_t i = 0;
    while (i + 2 <= p.size()) {
        int start = p[i], count = p[i + 1];
        i += 2;
        if (start == 0xFF && count == 0xFF) break;
        if (count == 0) count = 256;
        for (int k = 0; k < count && i + 3 <= p.size(); ++k, i += 3) {
            int at = start + k;
            if (at == 0 || at > 255) continue;
            for (int c = 0; c < 3; ++c) {
                uint8_t v = p[i + size_t(c)] & 63;
                pal[size_t(at) * 3 + size_t(c)] = uint8_t(v << 2 | v >> 4);
            }
            defined[at] = true;
        }
    }
}

static std::vector<uint32_t> grey(const std::vector<float>& v) {
    std::vector<uint32_t> g(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        uint32_t b = uint32_t(std::lround(std::clamp(v[i], 0.0f, 1.0f) * 255));
        g[i] = 0xFF000000u | b << 16 | b << 8 | b;
    }
    return g;
}

// Pads colours outward into transparent pixels (several rings), so filtering
// at an edge blends toward the sprite's own colours instead of black.
static void dilate(std::vector<uint32_t>& c, std::vector<bool> opaque, int w, int h) {
    for (int ring = 0; ring < 6; ++ring) {
        auto next = opaque;
        auto out = c;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                size_t i = size_t(y) * size_t(w) + size_t(x);
                if (opaque[i]) continue;
                int n = 0, r = 0, g = 0, b = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int X = x + dx, Y = y + dy;
                        if ((dx || dy) && X >= 0 && Y >= 0 && X < w && Y < h && opaque[size_t(Y) * size_t(w) + size_t(X)]) {
                            uint32_t v = c[size_t(Y) * size_t(w) + size_t(X)];
                            r += int((v >> 16) & 0xFF); g += int((v >> 8) & 0xFF); b += int(v & 0xFF); ++n;
                        }
                    }
                if (n) { out[i] = 0xFF000000u | uint32_t(r / n) << 16 | uint32_t(g / n) << 8 | uint32_t(b / n); next[i] = true; }
            }
        c.swap(out);
        opaque.swap(next);
    }
}

bool convertSprites(const std::vector<uint8_t>& stored, const std::string& outPath,
                    const ConvertOptions& opt, std::string& kind, std::string& err) {
    SpriteSet set;
    try {
        std::vector<uint8_t> raw = isHsq(stored.data(), stored.size()) ? unHsq(stored.data(), stored.size()) : stored;
        if (!set.load(raw)) { err = "not a sprite set"; return false; }
    } catch (const std::exception& ex) { err = ex.what(); return false; }

    SpritePackData pack;
    pack.pictureW = opt.pictureW;
    pack.pictureH = opt.pictureH;
    pack.sourceCrc = crc32(stored.data(), stored.size());
    pack.upscaler = opt.upscaler == ConvertOptions::Upscaler::RealEsrgan ? kUpscalerRealEsrgan
                     : opt.upscaler == ConvertOptions::Upscaler::Smooth ? kUpscalerSmooth : kUpscalerSharp;
    bool defined[256];
    ownPalette(set.paletteBlock(), pack.palette, defined);
    // Colour if the set's palette covers every colour it uses.
    bool own = true;
    for (size_t i = 0; i < set.count() && own; ++i)
        if (const auto* im = set.image(i))
            for (uint8_t c : im->pixels) if (c && !defined[c]) { own = false; break; }
    pack.kind = own ? 0 : 1;
    kind = own ? "colour" : "indexed";

    pack.images.resize(set.count());
    std::vector<size_t> forEsrgan;
    std::vector<std::vector<uint32_t>> colours(set.count());
    for (size_t i = 0; i < set.count(); ++i) {
        const auto* im = set.image(i);
        if (!im) continue;
        auto& out = pack.images[i];
        out.w = im->w; out.h = im->h;
        out.hdW = std::max(1, hdSpan(im->w, opt.pictureW, kW));
        out.hdH = std::max(1, hdSpan(im->h, opt.pictureH, kH));
        const size_t n = im->pixels.size();
        std::vector<float> solid(n);
        for (size_t k = 0; k < n; ++k) solid[k] = im->pixels[k] ? 1.0f : 0.0f;
        // Alpha: the sprite's shape, upscaled like its colours (smooth for
        // indexed sets: their shapes are traced smooth).
        auto alpha = (own && opt.upscaler != ConvertOptions::Upscaler::Smooth)
                         ? resampleSharp(grey(solid), im->w, im->h, out.hdW, out.hdH)
                         : resample(grey(solid), im->w, im->h, out.hdW, out.hdH);
        out.px.assign(size_t(out.hdW) * size_t(out.hdH), 0);
        if (own) {
            std::vector<uint32_t> c(n);
            std::vector<bool> opaque(n);
            for (size_t k = 0; k < n; ++k) {
                const uint8_t* rgb = &pack.palette[size_t(im->pixels[k]) * 3];
                c[k] = 0xFF000000u | uint32_t(rgb[0]) << 16 | uint32_t(rgb[1]) << 8 | rgb[2];
                opaque[k] = im->pixels[k] != 0;
            }
            dilate(c, opaque, im->w, im->h);
            colours[i] = std::move(c);
            for (size_t k = 0; k < out.px.size(); ++k) out.px[k] = (alpha[k] & 0xFF) << 24;   // alpha now, colour below
            if (opt.upscaler == ConvertOptions::Upscaler::RealEsrgan) forEsrgan.push_back(i);
            else {
                auto up = upscale(opt, colours[i], im->w, im->h, out.hdW, out.hdH);
                for (size_t k = 0; k < out.px.size(); ++k) out.px[k] |= up[k] & 0xFFFFFF;
            }
        } else {
            // Indexed: each colour's region upscaled smooth; every HD pixel
            // takes the colour whose region covers it most.
            std::vector<int> used;
            bool seen[256] = {};
            for (uint8_t c : im->pixels) if (c && !seen[c]) { seen[c] = true; used.push_back(c); }
            std::vector<uint8_t> best(out.px.size(), 0), bestW(out.px.size(), 0);
            for (int c : used) {
                std::vector<float> m(n);
                for (size_t k = 0; k < n; ++k) m[k] = im->pixels[k] == c ? 1.0f : 0.0f;
                auto up = resample(grey(m), im->w, im->h, out.hdW, out.hdH);
                for (size_t k = 0; k < up.size(); ++k) {
                    uint8_t v = uint8_t(up[k] & 0xFF);
                    if (v > bestW[k]) { bestW[k] = v; best[k] = uint8_t(c); }
                }
            }
            for (int y = 0; y < out.hdH; ++y)
                for (int x = 0; x < out.hdW; ++x) {
                    size_t k = size_t(y) * size_t(out.hdW) + size_t(x);
                    uint8_t idx = best[k];
                    if (!idx) {   // no region reaches here: the nearest original pixel
                        int sx = std::min(im->w - 1, x * im->w / out.hdW), sy = std::min(im->h - 1, y * im->h / out.hdH);
                        idx = im->pixels[size_t(sy) * size_t(im->w) + size_t(sx)];
                    }
                    out.px[k] = (alpha[k] & 0xFF) << 24 | uint32_t(idx) << 16;
                }
        }
    }
    if (!forEsrgan.empty()) {
        std::vector<std::vector<uint32_t>> pics;
        std::vector<std::pair<int, int>> sizes;
        for (size_t i : forEsrgan) { pics.push_back(colours[i]); sizes.emplace_back(pack.images[i].w, pack.images[i].h); }
        if (!upscaleRealEsrgan(pics, sizes, opt, err)) return false;
        for (size_t j = 0; j < forEsrgan.size(); ++j) {
            auto& out = pack.images[forEsrgan[j]];
            auto up = resample(pics[j], out.w * kEsrganScale, out.h * kEsrganScale, out.hdW, out.hdH);
            for (size_t k = 0; k < out.px.size(); ++k) out.px[k] |= up[k] & 0xFFFFFF;
        }
    }
    return writeSpritePack(outPath, pack, opt.quality, err);
}

}  // namespace hd
