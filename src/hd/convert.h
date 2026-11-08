#pragma once
// HNM -> HD pack conversion (used by the hd-conversion-utility tool). Everything here
// runs on the player's machine, over the player's own game files.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hd {

struct ConvertOptions {
    // Picture size: the 4:3 picture of the largest display mode (1080p).
    // Smaller modes are scaled down from it by the engine.
    int pictureW = 1440, pictureH = 1080;
    int quality = 90;           // WebP quality; >100 = lossless
    // Sharp (built-in, the default without Real-ESRGAN): line doubling
    //   and interpolating. Each original pixel is repeated, so edges stay
    //   crisp, and only the seams between pixels are blended, by how much of
    //   each output pixel each original pixel covers. That keeps the original
    //   pixels even even though 320x200 -> 1440x1080 is 4.5 x 5.4. No detail
    //   is added; it's the original, cleanly enlarged.
    // Smooth (built-in): a Catmull-Rom resize; softer.
    // Real-ESRGAN: runs realesrgan-ncnn-vulkan (MIT, uses the GPU through
    //   Vulkan) at 4x over each batch of frames (1280x800), then resizes that
    //   to the picture size. The only one that adds detail.
    enum class Upscaler { Sharp, Smooth, RealEsrgan } upscaler = Upscaler::Sharp;
    std::string realesrgan;     // the bundled realesrgan-ncnn-vulkan(.exe) (see tool.cpp)
    std::string model = "realesr-animevideov3";   // one of kModels
    std::string tempDir;        // scratch folder for Real-ESRGAN batches
    int jobs = 0;               // worker threads for resize/encode; 0 = all cores
    int batch = 240;            // frames per upscaling batch (bounds memory and scratch space)
    // Sprite sets: the images side by side are one picture (the dashboards'
    // two halves), upscaled together so there's no seam where they meet.
    bool strip = false;
    std::function<void(int done, int total)> progress;
};

// Real-ESRGAN models hd-conversion-utility runs (all 4x). Any other name is refused,
// so nothing but a known name reaches the program's command line.
extern const char* const kModels[];
const char* knownModel(const std::string& name);   // the table's own string, or nullptr

// Composited screens of a video, as the original shows them: one 320x200
// ARGB picture per HNM frame that changes the picture, and `repeat` marking
// frames that don't. Calls `sink(frameIndex, picture or nullptr)` in order.
bool compositeFrames(const std::vector<uint8_t>& hnm,
                     const std::function<bool(int, const std::vector<uint32_t>*)>& sink,
                     std::string& err);

// Resize an ARGB picture w x h to W x H (Catmull-Rom; widened when shrinking).
std::vector<uint32_t> resample(const std::vector<uint32_t>& argb, int w, int h, int W, int H);
// Enlarge by line doubling and interpolating (area coverage; see Upscaler::Sharp).
std::vector<uint32_t> resampleSharp(const std::vector<uint32_t>& argb, int w, int h, int W, int H);
// The built-in upscaler opt asks for (Sharp or Smooth).
std::vector<uint32_t> upscale(const struct ConvertOptions& opt, const std::vector<uint32_t>& argb, int w, int h,
                              int W, int H);

// The menu sprite sets hd-conversion-utility upscales (names in the archive,
// without .HSQ): the main menu, the options panels and messages, the Hall of
// Fame / key setup / save panels, the menu fonts, and the race dashboards.
extern const char* const kMenuSprites[];
// Whether a set from kMenuSprites is a strip (ConvertOptions::strip).
bool spriteStrip(const std::string& name);

// Converts one sprite set (its bytes as stored in the archive) into an HD
// sprite pack at outPath (see sprpack.h). `kind` gets "colour" or "indexed".
bool convertSprites(const std::vector<uint8_t>& stored, const std::string& outPath,
                    const ConvertOptions& opt, std::string& kind, std::string& err);

// Converts one HNM file (its bytes) into an HD pack at outPath.
bool convertVideo(const std::vector<uint8_t>& hnm, const std::string& outPath,
                  const ConvertOptions& opt, std::string& err);

}  // namespace hd
