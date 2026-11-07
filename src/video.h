#pragma once
// 8-bit indexed framebuffer, the same model the original VGA (mode 13h,
// 320x200x256) game draws into. Lua writes palette indices; the engine
// converts through the palette and scales to the window on present().

#include <SDL.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class Video {
public:
    bool init(int width, int height, int scale, bool hidden);
    void shutdown();

    int width()  const { return w_; }
    int height() const { return h_; }

    void clear(uint8_t idx);
    void pset(int x, int y, uint8_t idx);
    uint8_t pget(int x, int y) const;
    void fill(int x, int y, int w, int h, uint8_t idx);
    void line(int x0, int y0, int x1, int y1, uint8_t idx);
    // Blit an indexed image (row-major, `w*h` bytes). If `transparent` is
    // 0..255, that index is skipped (sprite colour key).
    void blit(int x, int y, int w, int h, const uint8_t* src, size_t len, int transparent);
    void text(int x, int y, const std::string& s, uint8_t idx);   // built-in 3x5 font

    void setColor(int i, uint8_t r, uint8_t g, uint8_t b);
    // 768-byte RGB palette. VGA DAC palettes are 6-bit (0..63); pass sixBit
    // to expand them to 8-bit.
    void setPalette(const uint8_t* rgb, size_t len, int first, bool sixBit);

    // Cryo palette ranges: { u8 start, u8 count (0=256), rgb[count] 6-bit } until FF FF.
    // Colour 0 is left alone (transparent in sprites, black in the videos).
    // `shift` moves ranges starting at 128+ up (rival cars use the player
    // car's colours moved from 128..191 to 192..255).
    void applyPaletteBlocks(const uint8_t* p, size_t n, int shift = 0);

    // Sprite drawing: index 0 transparent, nearest-neighbour scaling, optional
    // horizontal mirror, and clipping to the current span mask (if any).
    void sprite(const uint8_t* px, int w, int h, int x, int y, double scale, bool flip);
    // Span mask: rows[r] = {x0,x1, x0,x1, ...} drawable spans for screen row
    // r + yOffset. Rows with no entry are fully hidden while the mask is set.
    void setClip(std::vector<std::vector<int>> rows, int yOffset);
    void clearClip() { clipOn_ = false; clip_.clear(); }

    // ---- Display modes and the HD layer ---------------------------------
    // The game always draws in the original 320x200 coordinates: positions,
    // masks, hit tests and every layout table from RACEPRG.EXE stay as they
    // are. A display mode only decides how that screen is shown:
    //   native     320x200 at 4:3, scaled by --scale (the original look)
    //   800x600, 1024x768, 1920x1080
    //              a 4:3 picture of 800x600, 1024x768 or 1440x1080 (pillarboxed
    //              on 1920x1080), with HD videos when the player made packs
    // In those modes the screen is two layers: the HD video pictures (made
    // once at 1440x1080 and scaled down by the GPU for smaller modes), and the
    // game's own drawing (cars, dashboard, text) from the 320x200 framebuffer,
    // scaled up sharp. Each original pixel records which layer it shows.
    struct Mode { const char* name; int winW, winH, picW, picH; bool hd; };
    static const std::vector<Mode>& modes();
    // HD packs found: their picture size. HD modes are offered only then.
    bool enableHd(int picW, int picH);
    void disableHd();          // no packs any more: back to native if in an HD mode
    bool hdAvailable() const { return picW_ > 0; }
    int  hdPictureW() const { return picW_; }
    int  hdPictureH() const { return picH_; }
    bool setMode(const std::string& name);          // false: unknown or not available
    const Mode& mode() const { return modes()[size_t(mode_)]; }
    bool modeAvailable(const Mode& m) const { return !m.hd || hdAvailable(); }
    // The best mode that fits the desktop (HD modes only when available).
    std::string bestMode() const;
    // HD pictures are shown: an HD mode, packs found, and not switched off (F10).
    bool hdOn() const { return hdAvailable() && hdOn_ && mode().hd; }
    void setHdOn(bool on) { hdOn_ = on; }
    // blit() of a video frame drawn with its top-left at (ox, oy) on screen,
    // its pixels at (fx, fy) in the video, also taking that part of `pic`, the
    // video's whole screen as an HD picture (picW x picH), for the HD layer.
    void blitHd(int ox, int oy, int fx, int fy, int w, int h, const uint8_t* src, size_t len,
                int transparent, const uint32_t* pic, int picW, int picH);
    // True when, for every colour index `px` uses (except `transparent`),
    // the current palette is `rgb` (768 bytes, 8-bit). HD pictures carry the
    // video's own colours, so they are only used while the screen shows them.
    bool paletteMatches(const uint8_t* rgb, const uint8_t* px, size_t n, int transparent) const;

    void present();
    // asShown: the window as shown (in display modes); else the 320x200 screen.
    bool saveBmp(const std::string& path, bool asShown = false);
private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_  = nullptr;
    int w_ = 320, h_ = 200;
    std::vector<uint8_t> fb_;
    std::array<uint32_t, 256> pal_{};
    std::vector<uint32_t> rgba_;
    std::vector<std::vector<int>> clip_;
    int clipY_ = 0;
    bool clipOn_ = false;
    bool visible(int x, int y) const;
    void convert();

    int nativeScale_ = 3;
    bool hidden_ = false;
    int mode_ = 0;                   // index in modes()
    int picW_ = 0, picH_ = 0;        // HD picture size, 0 = no packs
    bool hdOn_ = true;
    bool hdDirty_ = false;
    std::vector<uint8_t>  src_;      // per original pixel: 1 = shows the HD layer
    std::vector<uint32_t> hd_;       // HD layer, picW_ x picH_ (the whole screen)
    std::vector<uint32_t> layer_;    // game layer, 320x200, transparent over HD
    SDL_Texture* hdTexture_ = nullptr;      // HD layer
    SDL_Texture* gameTexture_ = nullptr;    // game layer, 320x200
    SDL_Texture* sharpTexture_ = nullptr;   // game layer at a whole-number scale (render target)
    void render(bool hdShown);
    bool hdShown() const;
    void destroyModeTextures();
};
