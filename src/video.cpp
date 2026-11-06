#include "video.h"

#include "hd/compose.h"

#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <cstring>

// Tiny 3x5 debug font, original to this project (not taken from the game).
struct Glyph { char c; const char* rows; };
static const Glyph kFont[] = {
    {'0',"111101101101111"},{'1',"010110010010111"},{'2',"111001111100111"},
    {'3',"111001111001111"},{'4',"101101111001001"},{'5',"111100111001111"},
    {'6',"111100111101111"},{'7',"111001001001001"},{'8',"111101111101111"},
    {'9',"111101111001111"},{'A',"010101111101101"},{'B',"110101110101110"},
    {'C',"011100100100011"},{'D',"110101101101110"},{'E',"111100110100111"},
    {'F',"111100110100100"},{'G',"011100101101011"},{'H',"101101111101101"},
    {'I',"111010010010111"},{'J',"001001001101010"},{'K',"101101110101101"},
    {'L',"100100100100111"},{'M',"101111111101101"},{'N',"110101101101101"},
    {'O',"010101101101010"},{'P',"110101110100100"},{'Q',"010101101110011"},
    {'R',"110101110101101"},{'S',"011100010001110"},{'T',"111010010010010"},
    {'U',"101101101101111"},{'V',"101101101101010"},{'W',"101101111111101"},
    {'X',"101101010101101"},{'Y',"101101010010010"},{'Z',"111001010100111"},
    {' ',"000000000000000"},{'.',"000000000000010"},{',',"000000000010100"},
    {':',"000010000010000"},{'-',"000000111000000"},{'_',"000000000000111"},
    {'/',"001001010100100"},{'(',"001010010010001"},{')',"100010010010100"},
    {'!',"010010010000010"},{'?',"111001010000010"},{'\'',"010010000000000"},
    {'=',"000111000111000"},{'+',"000010111010000"},{'>',"100010001010100"},
    {'<',"001010100010001"},{'%',"101001010100101"},{'#',"101111101111101"},
    {'*',"000101010101000"},{'\\',"100100010001001"},{'"',"101101000000000"},
    {'&',"010101010101011"},{'$',"011110010011110"},{'@',"010101111100011"},{'[',"110100100100110"},{']',"011001001001011"},
};

static const char* glyphFor(char c) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& g : kFont) if (g.c == c) return g.rows;
    return "111001010000010";  // '?'
}

bool Video::init(int width, int height, int scale, bool hidden) {
    w_ = width; h_ = height;
    fb_.assign(static_cast<size_t>(w_) * h_, 0);
    src_.assign(fb_.size(), 0);
    rgba_.assign(fb_.size(), 0);
    for (int i = 0; i < 256; ++i) setColor(i, i, i, i);   // grey ramp until a palette loads

    Uint32 flags = SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    window_ = SDL_CreateWindow("MegaRace (port)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               w_ * scale, h_ * scale, flags);
    if (!window_) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) { std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
    // The original runs at 320x200 on a 4:3 monitor (non-square pixels).
    SDL_RenderSetLogicalSize(renderer_, w_, w_ * 3 / 4);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, w_, h_);
    return texture_ != nullptr;
}

void Video::shutdown() {
    if (hdTexture_) SDL_DestroyTexture(hdTexture_);
    hdTexture_ = nullptr;
    if (texture_)  SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    texture_ = nullptr; renderer_ = nullptr; window_ = nullptr;
}

void Video::clear(uint8_t idx) {
    std::memset(fb_.data(), idx, fb_.size());
    std::memset(src_.data(), 0, src_.size());
}

void Video::pset(int x, int y, uint8_t idx) {
    if (x >= 0 && y >= 0 && x < w_ && y < h_) {
        size_t i = static_cast<size_t>(y) * w_ + x;
        fb_[i] = idx;
        src_[i] = 0;
    }
}

uint8_t Video::pget(int x, int y) const {
    if (x >= 0 && y >= 0 && x < w_ && y < h_) return fb_[static_cast<size_t>(y) * w_ + x];
    return 0;
}

void Video::fill(int x, int y, int w, int h, uint8_t idx) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) pset(xx, yy, idx);
}

void Video::line(int x0, int y0, int x1, int y1, uint8_t idx) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 4096; ++guard) {
        pset(x0, y0, idx);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Video::blit(int x, int y, int w, int h, const uint8_t* src, size_t len, int transparent) {
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            size_t i = static_cast<size_t>(yy) * w + xx;
            if (i >= len) return;
            if (src[i] == transparent) continue;
            pset(x + xx, y + yy, src[i]);
        }
    }
}

void Video::text(int x, int y, const std::string& s, uint8_t idx) {
    int cx = x;
    for (char c : s) {
        if (c == '\n') { cx = x; y += 7; continue; }
        const char* g = glyphFor(c);
        for (int r = 0; r < 5; ++r)
            for (int col = 0; col < 3; ++col)
                if (g[r * 3 + col] == '1') pset(cx + col, y + r, idx);
        cx += 4;
    }
}

void Video::setColor(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i > 255) return;
    pal_[i] = 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

void Video::setPalette(const uint8_t* rgb, size_t len, int first, bool sixBit) {
    for (size_t n = 0; n + 2 < len && first + int(n / 3) < 256; n += 3) {
        uint8_t r = rgb[n], g = rgb[n + 1], b = rgb[n + 2];
        if (sixBit) {   // 0..63 -> 0..255
            r = uint8_t((r & 63) << 2 | (r & 63) >> 4);
            g = uint8_t((g & 63) << 2 | (g & 63) >> 4);
            b = uint8_t((b & 63) << 2 | (b & 63) >> 4);
        }
        setColor(first + int(n / 3), r, g, b);
    }
}

void Video::applyPaletteBlocks(const uint8_t* p, size_t n, int shift) {
    size_t i = 0;
    while (i + 2 <= n) {
        int start = p[i], count = p[i + 1];
        i += 2;
        if (start == 0xFF && count == 0xFF) break;
        if (count == 0) count = 256;
        for (int k = 0; k < count && i + 3 <= n; ++k, i += 3) {
            auto x = [](uint8_t v) { v &= 63; return uint8_t(v << 2 | v >> 4); };
            // Colour 0 is "transparent" in sprites, but the videos draw black
            // with it, so sprite palettes must not change it.
            if (start + k == 0) continue;
            int at = start + k + (start >= 128 ? shift : 0);
            if (at > 255) continue;
            setColor(at, x(p[i]), x(p[i + 1]), x(p[i + 2]));
        }
    }
}

void Video::setClip(std::vector<std::vector<int>> rows, int yOffset) {
    clip_ = std::move(rows);
    clipY_ = yOffset;
    clipOn_ = true;
}

bool Video::visible(int x, int y) const {
    if (!clipOn_) return true;
    int r = y - clipY_;
    if (r < 0 || r >= int(clip_.size())) return false;
    const auto& s = clip_[size_t(r)];
    for (size_t k = 0; k + 1 < s.size(); k += 2)
        if (x >= s[k] && x < s[k + 1]) return true;
    return false;
}

void Video::sprite(const uint8_t* px, int w, int h, int x, int y, double scale, bool flip) {
    if (w <= 0 || h <= 0 || scale <= 0) return;
    int dw = std::max(1, int(w * scale + 0.5)), dh = std::max(1, int(h * scale + 0.5));
    for (int dy = 0; dy < dh; ++dy) {
        int Y = y + dy;
        if (Y < 0 || Y >= h_) continue;
        int sy = dy * h / dh;
        for (int dx = 0; dx < dw; ++dx) {
            int X = x + dx;
            if (X < 0 || X >= w_) continue;
            int sx = dx * w / dw;
            if (flip) sx = w - 1 - sx;
            uint8_t c = px[size_t(sy) * size_t(w) + size_t(sx)];
            if (c && visible(X, Y)) {
                fb_[size_t(Y) * w_ + X] = c;
                src_[size_t(Y) * w_ + X] = 0;
            }
        }
    }
}

bool Video::enableHd(int k) {
    if (k < 2 || k > 8 || !renderer_) return false;
    if (hdK_ == k) return true;
    if (hdTexture_) SDL_DestroyTexture(hdTexture_);
    hdTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                   w_ * k, h_ * k);
    if (!hdTexture_) { std::fprintf(stderr, "HD texture: %s\n", SDL_GetError()); hdK_ = 0; return false; }
    // The HD screen is shown smaller or larger than its size depending on the
    // window; filter it rather than dropping pixels.
    SDL_SetTextureScaleMode(hdTexture_, SDL_ScaleModeLinear);
    hdK_ = k;
    hd_.assign(size_t(w_) * k * size_t(h_) * k, 0xFF000000u);
    hdOut_.assign(hd_.size(), 0xFF000000u);
    return true;
}

void Video::blitHd(int ox, int oy, int fx, int fy, int w, int h, const uint8_t* src, size_t len,
                   int transparent, const uint32_t* hd, int hdW, int hdH) {
    const int k = hdK_;
    const size_t W = size_t(w_) * size_t(k);
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            size_t i = static_cast<size_t>(yy) * w + xx;
            if (i >= len) return;
            if (src[i] == transparent) continue;
            int X = ox + fx + xx, Y = oy + fy + yy;            // on screen
            if (X < 0 || Y < 0 || X >= w_ || Y >= h_) continue;
            int cx = (fx + xx) * k, cy = (fy + yy) * k;        // in the HD picture
            size_t p = size_t(Y) * w_ + X;
            fb_[p] = src[i];
            if (cx < 0 || cy < 0 || cx + k > hdW || cy + k > hdH) { src_[p] = 0; continue; }
            src_[p] = 1;
            for (int r = 0; r < k; ++r)
                std::memcpy(&hd_[(size_t(Y) * k + r) * W + size_t(X) * k],
                            &hd[(size_t(cy) + r) * size_t(hdW) + size_t(cx)], size_t(k) * 4);
        }
    }
}

bool Video::paletteMatches(const uint8_t* rgb, const uint8_t* px, size_t n, int transparent) const {
    bool used[256] = {};
    for (size_t i = 0; i < n; ++i) used[px[i]] = true;
    for (int c = 0; c < 256; ++c) {
        if (!used[c] || c == transparent) continue;
        uint32_t want = 0xFF000000u | uint32_t(rgb[c * 3]) << 16 | uint32_t(rgb[c * 3 + 1]) << 8 | rgb[c * 3 + 2];
        if (pal_[size_t(c)] != want) return false;
    }
    return true;
}

void Video::convert() {
    for (size_t i = 0; i < fb_.size(); ++i) rgba_[i] = pal_[fb_[i]];
}

void Video::present() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    // HD only when something on screen came from an HD video; menus and
    // screens without one go through the plain path.
    if (hdOn() && std::find(src_.begin(), src_.end(), uint8_t(1)) != src_.end()) {
        hd::compose(fb_.data(), src_.data(), pal_.data(), hd_.data(), w_, h_, hdK_, hdOut_.data());
        SDL_UpdateTexture(hdTexture_, nullptr, hdOut_.data(), w_ * hdK_ * 4);
        SDL_RenderCopy(renderer_, hdTexture_, nullptr, nullptr);
    } else {
        convert();
        SDL_UpdateTexture(texture_, nullptr, rgba_.data(), w_ * 4);
        SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    }
    SDL_RenderPresent(renderer_);
}

bool Video::saveBmp(const std::string& path, bool asShown) {
    uint32_t* px = rgba_.data();
    int w = w_, h = h_;
    if (asShown && hdOn() && std::find(src_.begin(), src_.end(), uint8_t(1)) != src_.end()) {
        hd::compose(fb_.data(), src_.data(), pal_.data(), hd_.data(), w_, h_, hdK_, hdOut_.data());
        px = hdOut_.data();
        w = w_ * hdK_;
        h = h_ * hdK_;
    } else {
        convert();
    }
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}
