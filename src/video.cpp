#include "video.h"

#include <cctype>
#include <cstdlib>
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
    if (texture_)  SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    texture_ = nullptr; renderer_ = nullptr; window_ = nullptr;
}

void Video::clear(uint8_t idx) { std::memset(fb_.data(), idx, fb_.size()); }

void Video::pset(int x, int y, uint8_t idx) {
    if (x >= 0 && y >= 0 && x < w_ && y < h_) fb_[static_cast<size_t>(y) * w_ + x] = idx;
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

void Video::convert() {
    for (size_t i = 0; i < fb_.size(); ++i) rgba_[i] = pal_[fb_[i]];
}

void Video::present() {
    convert();
    SDL_UpdateTexture(texture_, nullptr, rgba_.data(), w_ * 4);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

bool Video::saveBmp(const std::string& path) {
    convert();
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(rgba_.data(), w_, h_, 32, w_ * 4,
                                                        SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}
