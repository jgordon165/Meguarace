#pragma once
// HD sprite packs (.hds): upscaled versions of the menu sprite sets (PANEL2,
// KONTROL, CONTROL4, the menu fonts, ...), made by hd-conversion-utility from
// the player's own MEGARACE.DAT, next to the video packs. Like those, they are
// still the game's content: never committed, built in CI or shipped.
//
// Every image is made at the size it covers on the 1440x1080 picture (the
// largest display mode's; the GPU scales it down for smaller ones). Two kinds:
//   colour   sets whose palette defines every colour they use (PANEL2,
//            KONTROL, CONTROL2): full-colour pictures with alpha, baked
//            with the set's own palette (stored, so the engine can check the
//            screen still shows those colours, as for videos).
//   indexed  sets drawn in the colours of the screen they're on (CONTROL4,
//            KONTROL6, the fonts): per HD pixel, the palette index and an
//            alpha. The engine colours them from the palette as it draws, so
//            they are always right. Their edges are upscaled shape by
//            shape, smooth, and the flat colours stay flat.
//
// File layout (little-endian):
//   char[4] "MRHS"
//   u16     version (1)
//   u8      kind: 0 colour, 1 indexed
//   u8      the upscaler that made it (hd::kUpscaler*; colour packs)
//   u16     picture width, height (1440, 1080) and canvas width, height (320, 200)
//   u32     image count (same as the set's)
//   u32     CRC-32 of the sprite set as stored in the archive
//   u8[768] the palette the colour pictures were made with (8-bit RGB; zeros for indexed)
//   count x { u16 w, h (original), u16 hdW, hdH, u32 offset, u32 size }   size 0: no image
//   WebP images with alpha: colour as it is; indexed with the index in red.

#include <cstdint>
#include <string>
#include <vector>

namespace hd {

constexpr uint16_t kSpritePackVersion = 1;

// The HD size of a w x h original image on a picW x picH picture of a cw x ch canvas.
inline int hdSpan(int n, int pic, int canvas) { return int((int64_t(n) * pic + canvas / 2) / canvas); }

struct SpriteImage {
    int w = 0, h = 0, hdW = 0, hdH = 0;
    std::vector<uint32_t> px;   // hdW x hdH, 0xAARRGGBB (indexed: index in RR)
};

struct SpritePackData {
    int kind = 0;                                // 0 colour, 1 indexed
    int pictureW = 1440, pictureH = 1080, width = 320, height = 200;
    uint32_t sourceCrc = 0;
    uint32_t upscaler = 0;                       // hd::kUpscaler*
    bool sharpened = false;                      // an unsharp mask after the upscale
    bool grained = false;                        // the original pixels mixed back in (menus)
    std::vector<uint8_t> palette = std::vector<uint8_t>(768, 0);
    std::vector<SpriteImage> images;             // one per set entry; px empty = none
};

std::vector<uint8_t> encodeWebpRgba(const uint32_t* argb, int w, int h, int quality);
bool decodeWebpRgba(const uint8_t* d, size_t n, int w, int h, std::vector<uint32_t>& argb);

// quality > 100: lossless (indexed packs are always lossless).
bool writeSpritePack(const std::string& path, const SpritePackData& p, int quality, std::string& err);
// Fails (err) if it's not a pack, is damaged, or was made from other bytes.
bool readSpritePack(const std::string& path, uint32_t sourceCrc, SpritePackData& p, std::string& err);

}  // namespace hd
