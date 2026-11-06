#pragma once
// HD video packs (.hdv): upscaled versions of MegaRace's HNM videos, made by
// megarace-hd on the player's machine from the player's own MEGARACE.DAT.
//
// A pack is derived from the original game's videos, so it is still the
// game's content: it is never committed, never built in CI and never shipped.
// The engine only reads packs the player generated.
//
// File layout (little-endian):
//   char[4] "MRHD"
//   u16     version (1)
//   u16     scale K (HD pixels per original pixel, each axis)
//   u16     canvas width, height in original pixels (320, 200)
//   u32     frame count (same as the HNM's)
//   u32     CRC-32 of the source .HNM file (a pack only loads over the exact
//           video it was made from)
//   u32     flags (reserved, 0)
//   frame count x { u32 offset, u32 size }   size 0: same picture as the
//                                            previous frame (no video chunk)
//   WebP images, each (width*K) x (height*K): the whole composited screen
//   after that frame, as the original shows it with the video's palette.

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace hd {

constexpr uint16_t kPackVersion = 1;
constexpr int kMaxScale = 8;

uint32_t crc32(const uint8_t* d, size_t n);

// Encode / decode one RGBA picture (0xAARRGGBB pixels, row-major).
// quality 0..100; quality > 100 asks for lossless.
std::vector<uint8_t> encodeWebp(const uint32_t* argb, int w, int h, int quality);
bool decodeWebp(const uint8_t* d, size_t n, int w, int h, std::vector<uint32_t>& argb);

struct PackHeader {
    int scale = 0, width = 320, height = 200;
    uint32_t frames = 0;
    uint32_t sourceCrc = 0;
};

class PackWriter {
public:
    // Frames must be added in order, 0..frames-1.
    bool open(const std::string& path, const PackHeader& h);
    // A WebP picture of (width*K) x (height*K), from encodeWebp.
    bool addFrame(const std::vector<uint8_t>& webp);
    bool addRepeat();   // no new picture for this frame
    bool finish();      // writes the index; false if the frame count is off
    const std::string& error() const { return err_; }

private:
    std::ofstream out_;
    std::string path_, err_;
    PackHeader h_;
    std::vector<std::pair<uint32_t, uint32_t>> index_;
    uint64_t pos_ = 0;
};

class Pack {
public:
    // Fails (with error()) if the file is not a pack, is damaged, or was made
    // from a different video than `sourceCrc`.
    bool open(const std::string& path, uint32_t sourceCrc);
    const PackHeader& header() const { return h_; }
    int hdWidth()  const { return h_.width * h_.scale; }
    int hdHeight() const { return h_.height * h_.scale; }
    // Picture shown after HNM frame i, or nullptr (out of range, unreadable,
    // or nothing drawn yet). Valid until the next call.
    const std::vector<uint32_t>* frame(int i);
    const std::string& error() const { return err_; }
    // Reads only the header (no source check): what scale a pack folder holds.
    static bool peek(const std::string& path, PackHeader& h);

private:
    std::ifstream in_;
    PackHeader h_;
    std::vector<std::pair<uint32_t, uint32_t>> index_;
    std::vector<int> picture_;      // frame -> index of the frame holding its picture, -1 none
    std::vector<uint32_t> cache_;
    int cached_ = -1;
    std::string err_;
};

}  // namespace hd
