#include "voice_filter.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kCutoff = 80.0f;          // high-pass, Hz
constexpr float kFloor = 0.1778f;         // -15 dB: the lowest gain
constexpr float kAlpha = 0.98f;           // decision-directed smoothing
constexpr float kPercentile = 0.20f;
// Noise power is exponentially distributed per bin, so its 20th percentile
// is -ln(0.8) = 0.223 of its mean: divide by that (x4.48) to get the mean.
constexpr float kPercentileToMean = 4.48f;
// Hann analysis and synthesis at 75% overlap sum to 1.5.
constexpr float kOlaGain = 1.0f / 1.5f;

std::vector<float> hann() {
    std::vector<float> w(VoiceFilter::kN);
    for (int i = 0; i < VoiceFilter::kN; ++i)
        w[size_t(i)] = 0.5f - 0.5f * std::cos(2 * kPi * float(i) / float(VoiceFilter::kN));   // periodic
    return w;
}
}  // namespace

void VoiceFilter::HighPass::design(float cutoff, int rate) {
    // RBJ biquad high-pass, Q = 1/sqrt(2) (Butterworth).
    float w0 = 2 * kPi * cutoff / float(rate);
    float al = std::sin(w0) / (2 * 0.70710678f), c = std::cos(w0), a0 = 1 + al;
    b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = (1 + c) / 2 / a0;
    a1 = -2 * c / a0; a2 = (1 - al) / a0;
    x1 = x2 = y1 = y2 = 0;
}

void VoiceFilter::fft(std::complex<float>* a, int n, bool inverse) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = 2 * kPi / float(len) * (inverse ? 1.0f : -1.0f);
        std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (int k = 0; k < len / 2; ++k) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (int i = 0; i < n; ++i) a[i] /= float(n);
}

std::vector<float> VoiceFilter::profile(const uint8_t* pcm, size_t n, int rate) {
    if (n < size_t(kN) * 4 || rate <= 0) return {};
    HighPass hp;
    hp.design(kCutoff, rate);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) x[i] = hp.run((float(pcm[i]) - 128.0f) / 128.0f);
    const auto w = hann();
    std::vector<std::vector<float>> bins(kBins);
    std::vector<std::complex<float>> buf(kN);
    for (size_t t = 0; t + kN <= n; t += kHop) {
        float energy = 0;
        for (int i = 0; i < kN; ++i) {
            buf[size_t(i)] = x[t + size_t(i)] * w[size_t(i)];
            energy += x[t + size_t(i)] * x[t + size_t(i)];
        }
        if (energy < 1e-7f) continue;          // digital silence between phrases
        fft(buf.data(), kN, false);
        for (int b = 0; b < kBins; ++b) bins[size_t(b)].push_back(std::norm(buf[size_t(b)]));
    }
    if (bins[0].size() < 8) return {};
    std::vector<float> noise(kBins);
    for (int b = 0; b < kBins; ++b) {
        auto& v = bins[size_t(b)];
        auto k = v.begin() + long(float(v.size() - 1) * kPercentile);
        std::nth_element(v.begin(), k, v.end());
        noise[size_t(b)] = *k * kPercentileToMean + 1e-12f;
    }
    return noise;
}

void VoiceFilter::start(std::vector<float> noise, int rate) {
    on_ = noise.size() == size_t(kBins) && rate > 0;
    if (!on_) return;
    noise_ = std::move(noise);
    window_ = hann();
    hp_.design(kCutoff, rate);
    gainPrev_.assign(kBins, 1.0f);
    powerPrev_.assign(kBins, 0.0f);
    // Start as if kN - kHop zeros came first; their output is dropped, so
    // the output lines up with the input.
    in_.assign(size_t(kN - kHop), 0.0f);
    raw_ = in_;
    ola_.assign(kN, 0.0f);
    skip_ = size_t(kN - kHop);
    taken_ = given_ = 0;
}

void VoiceFilter::flush(std::vector<float>& out) {
    out.clear();
    if (!on_ || taken_ <= given_) return;
    const uint64_t pending = taken_ - given_;
    // Zeros push the last real samples through the windows; keep only those.
    std::vector<float> zeros(size_t(kN), 0.0f), tail;
    const uint64_t takenBefore = taken_;
    process(zeros.data(), zeros.size(), tail);
    out.assign(tail.begin(), tail.begin() + long(std::min<uint64_t>(pending, tail.size())));
    taken_ = takenBefore;   // the zeros weren't input
    given_ = taken_;
}

void VoiceFilter::process(const float* in, size_t n, std::vector<float>& out) {
    out.clear();
    if (!on_) { out.assign(in, in + n); return; }
    taken_ += n;
    for (size_t i = 0; i < n; ++i) {
        in_.push_back(hp_.run(in[i]));
        raw_.push_back(in[i]);
    }
    std::vector<std::complex<float>> buf(kN);
    size_t used = 0;
    while (in_.size() - used >= size_t(kN)) {
        const float* f = in_.data() + used;
        for (int i = 0; i < kN; ++i) buf[size_t(i)] = f[i] * window_[size_t(i)];
        fft(buf.data(), kN, false);
        for (int b = 0; b < kBins; ++b) {
            float p = std::norm(buf[size_t(b)]);
            float nz = noise_[size_t(b)];
            float post = p / nz;                                   // a posteriori SNR
            float prio = kAlpha * gainPrev_[size_t(b)] * gainPrev_[size_t(b)] * powerPrev_[size_t(b)] / nz +
                         (1 - kAlpha) * std::max(post - 1.0f, 0.0f);   // a priori SNR
            float g = std::max(prio / (1 + prio), kFloor);         // Wiener
            gainPrev_[size_t(b)] = g;
            powerPrev_[size_t(b)] = p;
            buf[size_t(b)] *= g;
            if (b > 0 && b < kN / 2) buf[size_t(kN - b)] = std::conj(buf[size_t(b)]);
        }
        fft(buf.data(), kN, true);
        for (int i = 0; i < kN; ++i) ola_[size_t(i)] += buf[size_t(i)].real() * window_[size_t(i)] * kOlaGain;
        // The first hop is complete: emit it, shift the accumulator.
        for (int i = 0; i < kHop; ++i) {
            if (skip_) { --skip_; continue; }
            out.push_back(bypass_ ? raw_[used + size_t(i)] : ola_[size_t(i)]);
            ++given_;
        }
        std::copy(ola_.begin() + kHop, ola_.end(), ola_.begin());
        std::fill(ola_.end() - kHop, ola_.end(), 0.0f);
        used += kHop;
    }
    in_.erase(in_.begin(), in_.begin() + long(used));
    raw_.erase(raw_.begin(), raw_.begin() + long(used));
}
