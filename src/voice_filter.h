#pragma once
// Hiss reduction for the announcer's clips (Lance's B*.HNM videos).
//
// Their 8-bit, 11,111 Hz soundtracks carry a broadband hiss at about -35 dB
// under the voice, plus a DC offset and rumble below 80 Hz. The filter:
//   1. high-pass at 80 Hz (2nd-order Butterworth): offset and rumble;
//   2. spectral noise reduction: a short-time Fourier transform (512-sample
//      Hann windows, 128-sample hops, overlap-add), with a Wiener gain per
//      frequency from a decision-directed SNR estimate (Ephraim-Malah), never
//      below -15 dB, so the hiss is turned down rather than gated (no
//      "musical noise" chirps).
// The hiss is measured once over the whole clip before it plays (profile()),
// so the filter is right from the first word.
//
// On the US v1.0.1 disc: hiss between words about 10 dB lower, speech level
// within 0.3 dB. Runs as the clip streams; no game data is changed or stored.
//
// No SDL dependency, so it is unit-tested directly.

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

class VoiceFilter {
public:
    static constexpr int kN = 512, kHop = 128, kBins = kN / 2 + 1;

    // Noise power per frequency bin of a whole soundtrack (8-bit unsigned
    // PCM): the 20th percentile of each bin over its non-silent windows,
    // corrected to a mean. Empty if the sound is too short or silent.
    static std::vector<float> profile(const uint8_t* pcm, size_t n, int rate);

    // Starts filtering a new stream at `rate` with this noise profile; an
    // empty profile switches the filter off.
    void start(std::vector<float> noise, int rate);
    void stop() { on_ = false; }
    bool active() const { return on_; }
    // Bypassed: the original samples come out instead, with the same
    // alignment, so it can be switched mid-stream without a click in timing.
    void setBypass(bool b) { bypass_ = b; }
    bool bypassed() const { return bypass_; }

    // Filters the next samples (-1..1). The output lines up with the input:
    // the window's delay (384 samples) is taken off the front, so the last
    // 384 samples of a stream stay in the filter (the clips end in silence).
    void process(const float* in, size_t n, std::vector<float>& out);
    // The end of the stream: the samples still in the filter, so the output
    // has exactly as many samples as the input.
    void flush(std::vector<float>& out);

    // Building blocks, exposed for the tests.
    struct HighPass {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void design(float cutoff, int rate);
        float run(float x) {
            float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };
    static void fft(std::complex<float>* a, int n, bool inverse);

private:
    bool on_ = false, bypass_ = false;
    HighPass hp_;
    std::vector<float> noise_, window_, gainPrev_, powerPrev_;
    std::vector<float> in_;     // input (high-passed) not yet consumed by a window
    std::vector<float> raw_;    // the same input, unfiltered (for bypass)
    std::vector<float> ola_;    // overlap-add accumulator (kN)
    size_t skip_ = 0;           // output samples still to drop (the delay)
    uint64_t taken_ = 0, given_ = 0;   // samples in and out (flush evens them)
};
