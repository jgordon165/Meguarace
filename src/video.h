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
    // Blit an indexed image (row-major, `w*h` bytes). If `transparent` is
    // 0..255, that index is skipped (sprite colour key).
    void blit(int x, int y, int w, int h, const uint8_t* src, size_t len, int transparent);
    void text(int x, int y, const std::string& s, uint8_t idx);   // built-in 3x5 font

    void setColor(int i, uint8_t r, uint8_t g, uint8_t b);
    // 768-byte RGB palette. VGA DAC palettes are 6-bit (0..63); pass sixBit
    // to expand them to 8-bit.
    void setPalette(const uint8_t* rgb, size_t len, int first, bool sixBit);

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
    void convert();
};
