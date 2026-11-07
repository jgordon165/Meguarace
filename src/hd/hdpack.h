#pragma once
// HD video packs (.hdv): upscaled versions of MegaRace's HNM videos, made by
// megarace-hd on the player's machine from the player's own MEGARACE.DAT.
//
// A pack is derived from the original game's videos, so it is still the
// game's content: it is never committed, never built in CI and never shipped.
// The engine only reads packs the player generated.
//
// Packs are made once, at the largest display mode (1920x1080, whose 4:3
// picture is 1440x1080); the engine scales them down for smaller modes.
//
// File layout (little-endian):
//   char[4] "MRHD"
//   u16     version (2)
//   u16     picture width, height in pixels (1440, 1080)
//   u16     canvas width, height in original pixels (320, 200). The picture
//           shows the canvas at 4:3, as the original's monitor did, so its
//           pixels are square and the original's are not.
//   u32     frame count (same as the HNM's)
//   u32     CRC-32 of the source .HNM file (a pack only loads over the exact
//           video it was made from)
//   u32     flags (reserved, 0)
//   frame count x { u32 offset, u32 size }   size 0: same picture as the
//                                            previous frame (no video chunk)
//   WebP images, each picture width x height: the whole composited screen
//   after that frame, as the original shows it with the video's palette.

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace hd {

constexpr uint16_t kPackVersion = 2;
// The master size every pack is made at: the 4:3 picture of a 1920x1080 display.
constexpr int kPictureW = 1440, kPictureH = 1080;
constexpr int kMaxPicture = 4096;

uint32_t crc32(const uint8_t* d, size_t n);

// Encode / decode one RGBA picture (0xAARRGGBB pixels, row-major).
// quality 0..100; quality > 100 asks for lossless.
std::vector<uint8_t> encodeWebp(const uint32_t* argb, int w, int h, int quality);
bool decodeWebp(const uint8_t* d, size_t n, int w, int h, std::vector<uint32_t>& argb);

struct PackHeader {
    int pictureW = kPictureW, pictureH = kPictureH;
    int width = 320, height = 200;              // canvas, in original pixels
    uint32_t frames = 0;
    uint32_t sourceCrc = 0;
};

class PackWriter {
public:
    // Frames must be added in order, 0..frames-1.
    bool open(const std::string& path, const PackHeader& h);
    // A WebP picture of pictureW x pictureH, from encodeWebp.
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
    int hdWidth()  const { return h_.pictureW; }
    int hdHeight() const { return h_.pictureH; }
    // Picture shown after HNM frame i, or nullptr (out of range, unreadable,
    // or nothing drawn yet). Valid until the next call.
    const std::vector<uint32_t>* frame(int i);
    const std::string& error() const { return err_; }
    // Reads only the header (no source check): what size a pack folder holds.
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
