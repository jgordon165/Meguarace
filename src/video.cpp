#include "video.h"
#include "icon_data.h"


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

    nativeScale_ = scale;
    hidden_ = hidden;
    layer_.assign(fb_.size(), 0);
    Uint32 flags = SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    window_ = SDL_CreateWindow("MegaRace (port)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               w_ * scale, h_ * scale, flags);
    if (!window_) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }

    // The port's own icon (res/meguarace.ico, tools/make_icon.py) for the
    // title bar, taskbar and dock.
    if (SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom(
            const_cast<unsigned char*>(kIconRGBA), kIconSize, kIconSize, 32, kIconSize * 4,
            SDL_PIXELFORMAT_RGBA32)) {
        SDL_SetWindowIcon(window_, icon);
        SDL_FreeSurface(icon);
    }

    // Render targets let display modes scale the game layer sharply
    // (whole-number nearest, then linear); without them it is nearest only.
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_PRESENTVSYNC);
  
    if (!renderer_) { std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return false; }
    // The original runs at 320x200 on a 4:3 monitor (non-square pixels).
    SDL_RenderSetLogicalSize(renderer_, w_, w_ * 3 / 4);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, w_, h_);
    return texture_ != nullptr;
}

void Video::shutdown() {
    destroyModeTextures();
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

// ---- display modes ---------------------------------------------------------

const std::vector<Video::Mode>& Video::modes() {
    // Window size, then the 4:3 picture inside it. 1080p is 16:9, so its
    // picture (1440x1080, the size HD packs are made at) is pillarboxed.
    static const std::vector<Mode> m = {
        {"native", 0, 0, 320, 240, false},
        {"800x600", 800, 600, 800, 600, true},
        {"1024x768", 1024, 768, 1024, 768, true},
        {"1920x1080", 1920, 1080, 1440, 1080, true},
    };
    return m;
}

bool Video::enableHd(int picW, int picH) {
    if (picW < 1 || picH < 1 || !renderer_) return false;
    if (hdTexture_) SDL_DestroyTexture(hdTexture_);
    hdTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, picW, picH);
    if (!hdTexture_) { std::fprintf(stderr, "HD texture: %s\n", SDL_GetError()); picW_ = picH_ = 0; return false; }
    // Smaller modes show the picture smaller than it is: filter it.
    SDL_SetTextureScaleMode(hdTexture_, SDL_ScaleModeLinear);
    picW_ = picW;
    picH_ = picH;
    hd_.assign(size_t(picW) * size_t(picH), 0xFF000000u);
    hdDirty_ = true;
    return true;
}

void Video::disableHd() {
    if (hdTexture_) SDL_DestroyTexture(hdTexture_);
    hdTexture_ = nullptr;
    picW_ = picH_ = 0;
    hd_.clear();
    std::fill(src_.begin(), src_.end(), uint8_t(0));
    if (mode().hd) setMode("native");
}

std::string Video::bestMode() const {
    SDL_DisplayMode dm{};
    int display = window_ ? SDL_GetWindowDisplayIndex(window_) : 0;
    bool known = SDL_GetDesktopDisplayMode(display < 0 ? 0 : display, &dm) == 0;
    std::string best = "native";
    for (const auto& m : modes())
        if (m.hd && modeAvailable(m) && (!known || (m.winW <= dm.w && m.winH <= dm.h))) best = m.name;
    return best;
}

void Video::destroyModeTextures() {
    if (gameTexture_) SDL_DestroyTexture(gameTexture_);
    if (sharpTexture_) SDL_DestroyTexture(sharpTexture_);
    gameTexture_ = sharpTexture_ = nullptr;
}

bool Video::setMode(const std::string& name) {
    const auto& all = modes();
    int i = 0;
    while (i < int(all.size()) && name != all[size_t(i)].name) ++i;
    if (i == int(all.size()) || !modeAvailable(all[size_t(i)]) || !renderer_) return false;
    const Mode& m = all[size_t(i)];
    destroyModeTextures();
    if (m.hd) {
        gameTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w_, h_);
        if (!gameTexture_) { std::fprintf(stderr, "game layer: %s\n", SDL_GetError()); return false; }
        SDL_SetTextureBlendMode(gameTexture_, SDL_BLENDMODE_BLEND);
        // Sharp scaling: nearest to whole multiples of 320x200 at least the
        // picture size, then linear down to it, so every original pixel
        // comes out the same size instead of alternating 4 and 5 wide.
        if (SDL_RenderTargetSupported(renderer_)) {
            int kx = (m.picW + w_ - 1) / w_, ky = (m.picH + h_ - 1) / h_;
            sharpTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                              w_ * kx, h_ * ky);
            if (sharpTexture_) {
                SDL_SetTextureScaleMode(sharpTexture_, SDL_ScaleModeLinear);
                SDL_SetTextureBlendMode(sharpTexture_, SDL_BLENDMODE_BLEND);
            }
        }
    }
    mode_ = i;
    // Window: the mode's size, or the whole screen when it doesn't fit in a
    // window (1080p on a 1080p monitor). Native keeps the --scale size.
    if (!hidden_) {
        SDL_DisplayMode dm{};
        int display = SDL_GetWindowDisplayIndex(window_);
        bool fill = m.hd && SDL_GetDesktopDisplayMode(display < 0 ? 0 : display, &dm) == 0 &&
                    (m.winW >= dm.w || m.winH >= dm.h);
        SDL_SetWindowFullscreen(window_, fill ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        if (!fill) {
            SDL_SetWindowSize(window_, m.hd ? m.winW : w_ * nativeScale_, m.hd ? m.winH : h_ * nativeScale_);
            SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    } else {
        SDL_SetWindowSize(window_, m.hd ? m.winW : w_ * nativeScale_, m.hd ? m.winH : h_ * nativeScale_);
    }
    SDL_RenderSetLogicalSize(renderer_, m.picW, m.picH);
    hdDirty_ = true;
    return true;
}

void Video::blitHd(int ox, int oy, int fx, int fy, int w, int h, const uint8_t* src, size_t len,
                   int transparent, const uint32_t* pic, int picW, int picH) {
    // Game layer: exactly blit(), marking the pixels drawn as showing HD.
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            size_t i = static_cast<size_t>(yy) * w + xx;
            if (i >= len) break;
            if (src[i] == transparent) continue;
            int X = ox + fx + xx, Y = oy + fy + yy;
            if (X < 0 || Y < 0 || X >= w_ || Y >= h_) continue;
            size_t p = size_t(Y) * w_ + X;
            fb_[p] = src[i];
            src_[p] = 1;
        }
    }
    if (picW != picW_ || picH != picH_) return;   // the engine refuses such packs; be safe
    // HD layer: the frame's rectangle of the picture, at its place on screen.
    // Picture and screen map the same way: x * picW / 320, y * picH / 200.
    auto mx = [&](int x) { return int(int64_t(x) * picW / w_); };
    auto my = [&](int y) { return int(int64_t(y) * picH / h_); };
    int x0 = std::max(fx, 0), x1 = std::min(fx + w, w_), y0 = std::max(fy, 0), y1 = std::min(fy + h, h_);
    if (x0 >= x1 || y0 >= y1) return;
    int sx0 = mx(x0), sx1 = mx(x1), sy0 = my(y0), sy1 = my(y1);
    int dx0 = mx(ox + x0), dy0 = my(oy + y0);
    for (int r = sy0; r < sy1; ++r) {
        int dy = dy0 + (r - sy0);
        if (dy < 0 || dy >= picH_) continue;
        int a = sx0, b = sx1, d = dx0;
        if (d < 0) { a -= d; d = 0; }
        if (d + (b - a) > picW_) b = a + (picW_ - d);
        if (a >= b) continue;
        std::memcpy(&hd_[size_t(dy) * size_t(picW_) + size_t(d)], &pic[size_t(r) * size_t(picW) + size_t(a)],
                    size_t(b - a) * 4);
    }
    hdDirty_ = true;
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

bool Video::hdShown() const {
    return hdOn() && std::find(src_.begin(), src_.end(), uint8_t(1)) != src_.end();
}

// Draws the frame into the renderer (without presenting it).
void Video::render(bool showHd) {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    if (!mode().hd) {
        convert();
        SDL_UpdateTexture(texture_, nullptr, rgba_.data(), w_ * 4);
        SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
        return;
    }
    if (showHd) {
        if (hdDirty_) SDL_UpdateTexture(hdTexture_, nullptr, hd_.data(), picW_ * 4);
        hdDirty_ = false;
        SDL_RenderCopy(renderer_, hdTexture_, nullptr, nullptr);
    }
    // Game layer: palette colours, transparent where the HD layer shows.
    // Transparent pixels keep the original colour (alpha 0): the linear
    // filter then blends sprite edges toward the video, not toward black.
    for (size_t i = 0; i < fb_.size(); ++i)
        layer_[i] = (showHd && src_[i]) ? (pal_[fb_[i]] & 0x00FFFFFFu) : pal_[fb_[i]];
    SDL_UpdateTexture(gameTexture_, nullptr, layer_.data(), w_ * 4);
    if (sharpTexture_ && SDL_SetRenderTarget(renderer_, sharpTexture_) == 0) {
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
        SDL_RenderClear(renderer_);
        SDL_SetTextureBlendMode(gameTexture_, SDL_BLENDMODE_NONE);   // copy alpha as it is
        SDL_RenderCopy(renderer_, gameTexture_, nullptr, nullptr);
        SDL_SetTextureBlendMode(gameTexture_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_RenderCopy(renderer_, sharpTexture_, nullptr, nullptr);
    } else {
        SDL_RenderCopy(renderer_, gameTexture_, nullptr, nullptr);
    }
}

void Video::present() {
    render(hdShown());
    SDL_RenderPresent(renderer_);
}

bool Video::saveBmp(const std::string& path, bool asShown) {
    SDL_Surface* s = nullptr;
    std::vector<uint32_t> px;
    if (asShown && mode().hd) {
        // The window as shown, read back from the renderer.
        render(hdShown());
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(renderer_, &ow, &oh);
        px.resize(size_t(ow) * size_t(oh));
        // With a logical size set, reads cover only the picture's viewport;
        // drop it for the read to get the whole window, borders included.
        SDL_RenderSetLogicalSize(renderer_, 0, 0);
        SDL_RenderSetScale(renderer_, 1.0f, 1.0f);
        SDL_RenderSetViewport(renderer_, nullptr);
        SDL_Rect all{0, 0, ow, oh};
        int rc = SDL_RenderReadPixels(renderer_, &all, SDL_PIXELFORMAT_ARGB8888, px.data(), ow * 4);
        SDL_RenderSetLogicalSize(renderer_, mode().picW, mode().picH);
        if (rc != 0) return false;
        s = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), ow, oh, 32, ow * 4, SDL_PIXELFORMAT_ARGB8888);
    } else {
        convert();
        s = SDL_CreateRGBSurfaceWithFormatFrom(rgba_.data(), w_, h_, 32, w_ * 4, SDL_PIXELFORMAT_ARGB8888);
    }
    if (!s) return false;
    bool ok = SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}
