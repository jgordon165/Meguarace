#pragma once
// The music's finishing stage for the ENHANCED and SOUNDFONT styles: a
// two-band shelving EQ (RBJ biquads) and a small stereo room reverb
// (Freeverb: 8 combs and 4 all-passes per side, Jezar's public-domain
// design, delay lengths scaled to the output rate). Header-only, no SDL.

#include <algorithm>
#include <cmath>
#include <vector>

namespace musicfx {

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] = {0, 0}, z2[2] = {0, 0};

    // RBJ cookbook shelf (slope 1); `high` picks the high shelf.
    void shelf(bool high, double rate, double freq, double gainDb) {
        const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979 * freq / rate;
        const double cw = std::cos(w), alpha = std::sin(w) / 2.0 * std::sqrt(2.0);
        const double s = high ? -1.0 : 1.0, sq = 2.0 * std::sqrt(A) * alpha;
        const double B0 = A * ((A + 1) - s * (A - 1) * cw + sq);
        const double B1 = s * 2 * A * ((A - 1) - s * (A + 1) * cw);
        const double B2 = A * ((A + 1) - s * (A - 1) * cw - sq);
        const double A0 = (A + 1) + s * (A - 1) * cw + sq;
        const double A1 = -s * 2 * ((A - 1) + s * (A + 1) * cw);
        const double A2 = (A + 1) + s * (A - 1) * cw - sq;
        b0 = float(B0 / A0); b1 = float(B1 / A0); b2 = float(B2 / A0);
        a1 = float(A1 / A0); a2 = float(A2 / A0);
    }
    float run(int c, float x) {             // transposed direct form II
        const float y = b0 * x + z1[c];
        z1[c] = b1 * x - a1 * y + z2[c];
        z2[c] = b2 * x - a2 * y;
        return y;
    }
};

class Reverb {
public:
    void init(int rate) {
        static const int COMB[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int AP[4] = {556, 441, 341, 225};
        const double k = rate / 44100.0;
        for (int c = 0; c < 2; ++c) {
            const int spread = c ? 23 : 0;      // the right side a little longer: width
            for (int i = 0; i < 8; ++i) comb_[c][i].assign(size_t(std::max(1.0, (COMB[i] + spread) * k)), 0.f);
            for (int i = 0; i < 4; ++i) ap_[c][i].assign(size_t(std::max(1.0, (AP[i] + spread) * k)), 0.f);
            for (auto& p : cpos_[c]) p = 0;
            for (auto& p : apos_[c]) p = 0;
            for (auto& f : filt_[c]) f = 0;
        }
    }
    // room 0..1 (size), damp 0..1 (high-frequency loss), wet 0..1.
    void set(float room, float damp, float wet) { fb_ = room * 0.28f + 0.7f; damp_ = damp * 0.4f; wet_ = wet; }

    void run(float& l, float& r) {
        const float in = (l + r) * 0.015f;   // Freeverb's fixed input gain
        float out[2] = {0, 0};
        for (int c = 0; c < 2; ++c) {
            float acc = 0;
            for (int i = 0; i < 8; ++i) {
                auto& b = comb_[c][i];
                size_t& p = cpos_[c][i];
                const float y = b[p];
                filt_[c][i] = y * (1 - damp_) + filt_[c][i] * damp_;
                b[p] = in + filt_[c][i] * fb_;
                if (++p >= b.size()) p = 0;
                acc += y;
            }
            for (int i = 0; i < 4; ++i) {
                auto& b = ap_[c][i];
                size_t& p = apos_[c][i];
                const float y = b[p];
                b[p] = acc + y * 0.5f;
                if (++p >= b.size()) p = 0;
                acc = y - acc;
            }
            out[c] = acc;
        }
        l += out[0] * wet_ * 3.0f;
        r += out[1] * wet_ * 3.0f;
    }

private:
    std::vector<float> comb_[2][8], ap_[2][4];
    size_t cpos_[2][8] = {}, apos_[2][4] = {};
    float filt_[2][8] = {};
    float fb_ = 0.84f, damp_ = 0.2f, wet_ = 0.2f;
};

// EQ then reverb, on interleaved stereo floats.
class Chain {
public:
    // lowDb at 120 Hz, highDb at 9 kHz; room/damp/wet as Reverb::set.
    void init(int rate, float lowDb, float highDb, float room, float damp, float wet) {
        rate_ = rate;
        low_.shelf(false, rate, 120.0, lowDb);
        high_.shelf(true, rate, 9000.0, highDb);
        rev_.init(rate);
        rev_.set(room, damp, wet);
    }
    int rate() const { return rate_; }
    void run(float* lr, int frames) {
        for (int i = 0; i < frames; ++i) {
            float l = lr[2 * i], r = lr[2 * i + 1];
            l = high_.run(0, low_.run(0, l));
            r = high_.run(1, low_.run(1, r));
            rev_.run(l, r);
            lr[2 * i] = l;
            lr[2 * i + 1] = r;
        }
    }

private:
    int rate_ = 0;
    Biquad low_, high_;
    Reverb rev_;
};

}  // namespace musicfx
