#pragma once
// Streaming linear resampler: 8-bit unsigned or 16-bit signed PCM, mono or
// stereo, at any rate -> interleaved signed 16-bit stereo at the device rate.
//
// Exact length: across any number of push() calls the output is
// floor(total_input * dst / src) +/- 1 frames, because the fractional read
// position and the last input frame carry over between chunks. (Converting
// each video frame's sound chunk separately with SDL_AudioStream dropped
// more than half the samples, making speech run ahead of the picture.)
//
// No SDL dependency, so it is unit-tested directly.

#include <cstddef>
#include <cstdint>
#include <vector>

class Resampler {
public:
    void reset() { pos_ = 0.0; havePrev_ = false; }

    void push(const uint8_t* data, size_t len, int bits, int channels,
              int srcRate, int dstRate, std::vector<int16_t>& out) {
        if (srcRate <= 0 || dstRate <= 0 || channels < 1 || channels > 2) return;
        const size_t bytesPerFrame = size_t(bits / 8) * size_t(channels);
        if (bytesPerFrame == 0) return;
        const size_t frames = len / bytesPerFrame;

        src_.clear();
        src_.reserve((frames + 1) * 2);
        if (havePrev_) { src_.push_back(prevL_); src_.push_back(prevR_); }
        for (size_t i = 0; i < frames; ++i) {
            const uint8_t* p = data + i * bytesPerFrame;
            int16_t l = sample(p, bits);
            int16_t r = channels == 2 ? sample(p + bits / 8, bits) : l;
            src_.push_back(l);
            src_.push_back(r);
        }
        const size_t n = src_.size() / 2;
        if (n == 0) return;

        const double step = double(srcRate) / double(dstRate);
        // Emit while both neighbours of the read position are available.
        while (pos_ + 1.0 < double(n)) {
            size_t i = size_t(pos_);
            double f = pos_ - double(i);
            for (int c = 0; c < 2; ++c) {
                double a = src_[2 * i + size_t(c)], b = src_[2 * (i + 1) + size_t(c)];
                out.push_back(int16_t(a + (b - a) * f));
            }
            pos_ += step;
        }
        // Keep the last frame as the left neighbour for the next chunk.
        pos_ -= double(n - 1);
        prevL_ = src_[2 * (n - 1)];
        prevR_ = src_[2 * (n - 1) + 1];
        havePrev_ = true;
    }

    // Emits the final input frame (call once at the end of a one-shot sound).
    void finish(std::vector<int16_t>& out) {
        if (havePrev_ && pos_ <= 0.0) { out.push_back(prevL_); out.push_back(prevR_); }
    }

private:
    static int16_t sample(const uint8_t* p, int bits) {
        if (bits == 16) return int16_t(uint16_t(p[0] | p[1] << 8));
        return int16_t((int(p[0]) - 128) << 8);
    }

    std::vector<int16_t> src_;
    double pos_ = 0.0;
    bool havePrev_ = false;
    int16_t prevL_ = 0, prevR_ = 0;
};
