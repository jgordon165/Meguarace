#pragma once
// HD screen compositing, shared by the engine (Video::present) and the tests.
// Header-only and free of SDL and libwebp.

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace hd {

// Original pixels whose source is HD (src[i] != 0) take their K x K block
// from `hd`; the others are the indexed pixel through the palette, scaled up
// K x K (sprites, dashboard, text: everything the game draws itself). All
// buffers are row-major; `hd` and `out` are (w*K) x (h*K).
inline void compose(const uint8_t* fb, const uint8_t* src, const uint32_t* pal, const uint32_t* hd,
                    int w, int h, int k, uint32_t* out) {
    const size_t W = size_t(w) * size_t(k);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = size_t(y) * size_t(w) + size_t(x);
            size_t o = size_t(y) * size_t(k) * W + size_t(x) * size_t(k);
            if (src[i]) {
                for (int r = 0; r < k; ++r)
                    std::memcpy(out + o + size_t(r) * W, hd + o + size_t(r) * W, size_t(k) * 4);
            } else {
                uint32_t c = pal[fb[i]];
                for (int r = 0; r < k; ++r) std::fill_n(out + o + size_t(r) * W, k, c);
            }
        }
    }
}

}  // namespace hd
