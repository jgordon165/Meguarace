#pragma once
// HNM -> HD pack conversion (used by the megarace-hd tool). Everything here
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
    // Built-in: a Catmull-Rom resize, no extra programs, runs anywhere.
    // Real-ESRGAN: runs realesrgan-ncnn-vulkan (MIT, uses the GPU through
    // Vulkan) at 4x over each batch of frames (1280x800), then resizes that
    // to the picture size.
    enum class Upscaler { Builtin, RealEsrgan } upscaler = Upscaler::Builtin;
    std::string realesrgan;     // path to realesrgan-ncnn-vulkan(.exe)
    std::string model = "realesr-animevideov3";
    std::string tempDir;        // scratch folder for Real-ESRGAN batches
    int jobs = 0;               // worker threads for resize/encode; 0 = all cores
    int batch = 240;            // frames per upscaling batch (bounds memory and scratch space)
    std::function<void(int done, int total)> progress;
};

// Composited screens of a video, as the original shows them: one 320x200
// ARGB picture per HNM frame that changes the picture, and `repeat` marking
// frames that don't. Calls `sink(frameIndex, picture or nullptr)` in order.
bool compositeFrames(const std::vector<uint8_t>& hnm,
                     const std::function<bool(int, const std::vector<uint32_t>*)>& sink,
                     std::string& err);

// Resize an ARGB picture w x h to W x H (Catmull-Rom; widened when shrinking).
std::vector<uint32_t> resample(const std::vector<uint32_t>& argb, int w, int h, int W, int H);

// Converts one HNM file (its bytes) into an HD pack at outPath.
bool convertVideo(const std::vector<uint8_t>& hnm, const std::string& outPath,
                  const ConvertOptions& opt, std::string& err);

}  // namespace hd
