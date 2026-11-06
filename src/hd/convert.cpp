#include "hd/convert.h"

#include "formats/cryo.h"
#include "hd/hdpack.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <thread>

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

// Catmull-Rom (a = -0.5) cubic weights for a sample t in [0,1) past tap 1.
static std::array<float, 4> cubic(float t) {
    float t2 = t * t, t3 = t2 * t;
    return {-0.5f * t3 + t2 - 0.5f * t,
             1.5f * t3 - 2.5f * t2 + 1.0f,
            -1.5f * t3 + 2.0f * t2 + 0.5f * t,
             0.5f * t3 - 0.5f * t2};
}

// Separable integer-factor bicubic, edges clamped. Each output pixel's
// centre maps back to (x + 0.5) / k - 0.5 in the source, so there are only
// k distinct weight sets per axis.
std::vector<uint32_t> upscaleBuiltin(const std::vector<uint32_t>& argb, int w, int h, int k) {
    std::vector<std::array<float, 4>> wt(static_cast<size_t>(k));
    std::vector<int> base(static_cast<size_t>(k));
    for (int p = 0; p < k; ++p) {
        float s = (float(p) + 0.5f) / float(k) - 0.5f;
        int i = int(std::floor(s));
        base[size_t(p)] = i - 1;
        wt[size_t(p)] = cubic(s - float(i));
    }
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    const int W = w * k, H = h * k;
    // Horizontal pass into float RGB, then vertical pass to bytes.
    std::vector<float> tmp(size_t(W) * size_t(h) * 3);
    for (int y = 0; y < h; ++y)
        for (int X = 0; X < W; ++X) {
            int x0 = X / k, p = X % k;
            float acc[3] = {0, 0, 0};
            for (int t = 0; t < 4; ++t) {
                uint32_t c = argb[size_t(y) * size_t(w) + size_t(clampi(x0 + base[size_t(p)] + t, 0, w - 1))];
                float f = wt[size_t(p)][size_t(t)];
                acc[0] += f * float((c >> 16) & 0xFF); acc[1] += f * float((c >> 8) & 0xFF); acc[2] += f * float(c & 0xFF);
            }
            float* o = &tmp[(size_t(y) * size_t(W) + size_t(X)) * 3];
            o[0] = acc[0]; o[1] = acc[1]; o[2] = acc[2];
        }
    std::vector<uint32_t> out(size_t(W) * size_t(H));
    auto byte = [](float v) { return uint32_t(v <= 0 ? 0 : v >= 255 ? 255 : int(v + 0.5f)); };
    for (int Y = 0; Y < H; ++Y) {
        int y0 = Y / k, p = Y % k;
        for (int X = 0; X < W; ++X) {
            float acc[3] = {0, 0, 0};
            for (int t = 0; t < 4; ++t) {
                const float* c = &tmp[(size_t(clampi(y0 + base[size_t(p)] + t, 0, h - 1)) * size_t(W) + size_t(X)) * 3];
                float f = wt[size_t(p)][size_t(t)];
                acc[0] += f * c[0]; acc[1] += f * c[1]; acc[2] += f * c[2];
            }
            out[size_t(Y) * size_t(W) + size_t(X)] = 0xFF000000u | byte(acc[0]) << 16 | byte(acc[1]) << 8 | byte(acc[2]);
        }
    }
    return out;
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

static std::string quote(const std::string& s) { return "\"" + s + "\""; }

// Runs Real-ESRGAN over `pics` (in place). Each must come back k times larger.
static bool upscaleRealEsrgan(std::vector<std::vector<uint32_t>>& pics, const ConvertOptions& opt,
                              std::string& err) {
    fs::path base = opt.tempDir.empty() ? fs::temp_directory_path() / "megarace-hd" : fs::path(opt.tempDir);
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
        if (!stbi_write_png((in / name).string().c_str(), kW, kH, 3, rgb.data(), kW * 3)) {
            err = "cannot write scratch frame";
            return false;
        }
    }
    std::string cmd = quote(opt.realesrgan) + " -i " + quote(in.string()) + " -o " + quote(out.string()) +
                      " -n " + opt.model + " -s " + std::to_string(opt.scale) + " -f png";
    // Its models ship in a "models" folder beside it; it otherwise looks in
    // the current folder.
    fs::path models = fs::path(opt.realesrgan).parent_path() / "models";
    if (fs::is_directory(models, ec)) cmd += " -m " + quote(models.string());
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";   // cmd.exe strips one pair of outer quotes
#else
    cmd += " >/dev/null 2>&1";
#endif
    if (std::system(cmd.c_str()) != 0) { err = "Real-ESRGAN failed: " + cmd; return false; }

    const int W = kW * opt.scale, H = kH * opt.scale;
    for (size_t i = 0; i < pics.size(); ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "%06zu.png", i);
        int w = 0, h = 0, n = 0;
        uint8_t* px = stbi_load((out / name).string().c_str(), &w, &h, &n, 3);
        if (!px) { err = "Real-ESRGAN wrote no " + std::string(name); return false; }
        if (w != W || h != H) {
            stbi_image_free(px);
            err = "Real-ESRGAN output is " + std::to_string(w) + "x" + std::to_string(h) +
                  ", expected " + std::to_string(W) + "x" + std::to_string(H) +
                  " (does model " + opt.model + " support --scale " + std::to_string(opt.scale) + "?)";
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
    h.scale = opt.scale;
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
        if (opt.upscaler == ConvertOptions::Upscaler::RealEsrgan) {
            if (!pics.empty() && !upscaleRealEsrgan(pics, opt, err)) return false;
        } else {
            parallelFor(int(pics.size()), opt.jobs,
                        [&](int i) { pics[size_t(i)] = upscaleBuiltin(pics[size_t(i)], kW, kH, opt.scale); });
        }
        std::vector<std::vector<uint8_t>> webp(pics.size());
        parallelFor(int(pics.size()), opt.jobs, [&](int i) {
            webp[size_t(i)] = encodeWebp(pics[size_t(i)].data(), kW * opt.scale, kH * opt.scale, opt.quality);
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

}  // namespace hd
