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

    void present();
    bool saveBmp(const std::string& path);

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
};
