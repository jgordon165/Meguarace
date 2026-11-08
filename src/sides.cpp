#include "sides.h"

#include <algorithm>
#include <cmath>

namespace sides {

namespace {

struct Rgb { float r, g, b; };

uint32_t pack(Rgb c, float a = 1.0f) {
    auto q = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return (q(a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}
Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb add(Rgb a, Rgb b, float k) { return {a.r + b.r * k, a.g + b.g * k, a.b + b.b * k}; }
Rgb mul(Rgb a, float k) { return {a.r * k, a.g * k, a.b * k}; }

// Small deterministic random numbers (the same picture every time).
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return float(next() & 0xFFFFFF) / float(0x1000000); }
};

float hash2(int x, int y, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xFFFFFF) / float(0x1000000);
}
float smooth(float t) { return t * t * (3 - 2 * t); }
float noise(float x, float y, uint32_t seed) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const float tx = smooth(x - float(xi)), ty = smooth(y - float(yi));
    const float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    const float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}
float fbm(float x, float y, uint32_t seed, int octaves) {
    float v = 0, amp = 0.5f, sum = 0;
    for (int i = 0; i < octaves; ++i) {
        v += noise(x, y, seed + uint32_t(i) * 101u) * amp;
        sum += amp;
        x *= 2.03f; y *= 2.03f; amp *= 0.5f;
    }
    return v / sum;
}

// Shadow toward the picture (the wing's right edge), so the game sits in front.
float edge_shade(int x, int w) {
    const float d = float(w - 1 - x) / float(std::max(1, w));   // 0 at the picture edge
    return 0.55f + 0.45f * std::min(1.0f, d * 6.0f);
}

// ---- CARBON ------------------------------------------------------------------
// 2x2 twill: tows of fibres alternating horizontal and vertical, each tow
// shaded across its width (a round bundle) with fine fibre lines, and a broad
// diagonal sheen over it all.
void carbon(Picture& p) {
    const int tow = std::max(4, p.h / 90);
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
            const int cx = x / tow, cy = y / tow;
            const bool horiz = ((cx + cy) / 2) % 2 == 0;          // twill: diagonal steps of two
            const float u = float(horiz ? y % tow : x % tow) / float(tow);   // across the tow
            const float bundle = std::sin(u * 3.14159f);           // 0 at the edges, 1 mid-tow
            const float fibre = 0.5f + 0.5f * std::sin(u * 40.0f + hash2(cx, cy, 7) * 6.0f);
            float l = 0.06f + 0.10f * bundle + 0.025f * fibre;
            // The sheen: a soft diagonal band of light.
            const float diag = (float(x) + float(y) * 0.6f) / float(p.h);
            const float sheen = std::exp(-std::pow((diag - 0.55f) * 3.0f, 2.0f));
            l += sheen * (horiz ? 0.30f : 0.16f) * bundle;
            l *= edge_shade(x, p.w);
            const Rgb c = {l * 0.95f, l * 0.98f, l * 1.08f};       // a cool, slightly blue black
            p.base[size_t(y) * size_t(p.w) + size_t(x)] = pack(c);
        }
}

// ---- SPACE -------------------------------------------------------------------
void stars(std::vector<uint32_t>& px, int w, int h, Rng& r, int count, float bright, bool glow) {
    for (int i = 0; i < count; ++i) {
        const int x = int(r.f() * float(w)), y = int(r.f() * float(h));
        const float m = bright * (0.35f + 0.65f * r.f() * r.f());
        const float tint = r.f();
        const Rgb c = tint < 0.2f ? Rgb{0.75f, 0.85f, 1.0f} : tint > 0.85f ? Rgb{1.0f, 0.85f, 0.7f} : Rgb{1, 1, 1};
        const int rad = glow && m > 0.75f * bright ? 2 : 1;
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; ++dx) {
                const int X = x + dx, Y = (y + dy + h) % h;              // wraps: it scrolls
                if (X < 0 || X >= w) continue;
                const bool cross = dx == 0 || dy == 0;
                const float k = (dx == 0 && dy == 0) ? 1.0f : cross ? 0.35f / float(std::abs(dx) + std::abs(dy)) : 0.0f;
                if (k <= 0) continue;
                uint32_t& d = px[size_t(Y) * size_t(w) + size_t(X)];
                const float alpha = std::max(float(d >> 24) / 255.0f, m * k);
                d = pack(c, alpha);                                // colour, brightness as alpha
            }
    }
}

void space(Picture& p, bool right) {
    const float s = 3.0f / float(p.h);                             // noise scale: picture-sized clouds
    const Rgb deep = {0.01f, 0.01f, 0.04f}, purple = {0.35f, 0.08f, 0.45f}, teal = {0.05f, 0.35f, 0.45f},
              pink = {0.85f, 0.25f, 0.55f};
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
            const float fx = float(x) * s + (right ? 7.3f : 0.0f), fy = float(y) * s + (right ? 2.1f : 0.0f);
            const float n1 = fbm(fx, fy, 11, 6), n2 = fbm(fx + 5.2f, fy + 1.3f, 23, 5);
            const float cloud = std::pow(std::clamp((n1 - 0.30f) * 2.0f, 0.0f, 1.0f), 1.3f);
            const float wisp = std::pow(std::clamp((n2 - 0.40f) * 2.4f, 0.0f, 1.0f), 1.6f);
            Rgb c = deep;
            c = add(c, mix(purple, teal, std::clamp((n2 - 0.3f) * 2.5f, 0.0f, 1.0f)), cloud * 1.5f);
            c = add(c, pink, wisp * cloud * 1.1f);
            c = mul(c, edge_shade(x, p.w));
            p.base[size_t(y) * size_t(p.w) + size_t(x)] = pack(c);
        }
    // A ringed planet, low in the wing, lit from the picture's side.
    {
        // (The right wing, mirrored when drawn, gets a small moon up high instead.)
        const float pr = float(p.w) * (right ? 0.16f : 0.42f), pcx = float(p.w) * (right ? 0.45f : 0.42f),
                    pcy = float(p.h) * (right ? 0.18f : 0.52f);
        const Rgb land = {0.85f, 0.45f, 0.30f}, band = {0.55f, 0.20f, 0.35f};
        auto ring = [&](float x, float y) {       // tilted ring: an ellipse band
            const float dx = (x - pcx) / (pr * 2.1f), dy = (y - pcy) / (pr * 0.38f);
            const float rr = std::sqrt(dx * dx + dy * dy);
            return (rr > 0.72f && rr < 1.0f) ? 0.55f * (1.0f - std::fabs(rr - 0.86f) / 0.14f) : 0.0f;
        };
        for (int y = 0; y < p.h; ++y)
            for (int x = 0; x < p.w; ++x) {
                const float dx = (float(x) - pcx) / pr, dy = (float(y) - pcy) / pr;
                const float d2 = dx * dx + dy * dy;
                uint32_t& px = p.base[size_t(y) * size_t(p.w) + size_t(x)];
                Rgb c = {float((px >> 16) & 0xFF) / 255.0f, float((px >> 8) & 0xFF) / 255.0f, float(px & 0xFF) / 255.0f};
                const bool front = float(y) > pcy;                 // the ring's near half covers the planet
                if (d2 < 1.0f) {
                    const float nz = std::sqrt(1.0f - d2);
                    const float light = std::clamp(0.15f + 0.85f * (dx * 0.6f - dy * 0.3f + nz * 0.5f), 0.0f, 1.0f);
                    const float stripes = 0.5f + 0.5f * std::sin(dy * 9.0f + fbm(dx * 3, dy * 6, 41, 3) * 4.0f);
                    c = mul(mix(band, land, stripes), light);
                    c = add(c, Rgb{0.4f, 0.2f, 0.6f}, std::pow(1.0f - nz, 3.0f) * 0.8f);   // atmosphere rim
                }
                const float rg = right ? 0.0f : ring(float(x), float(y));
                if (rg > 0 && (front || d2 >= 1.0f)) c = mix(c, Rgb{0.9f, 0.8f, 0.7f}, rg);
                px = pack(mul(c, d2 < 1.0f || rg > 0 ? edge_shade(x, p.w) : 1.0f));
            }
    }
    // Faint far stars in the base; brighter near ones in the drifting overlay.
    std::vector<uint32_t> far(p.base.size(), 0);
    Rng r(right ? 6 : 5);
    stars(far, p.w, p.h, r, p.w * p.h / 300, 0.55f, false);
    for (size_t i = 0; i < far.size(); ++i) {
        const float a = float(far[i] >> 24) / 255.0f;
        if (a <= 0) continue;
        const uint32_t b = p.base[i];
        const auto ch = [&](int sh) {
            return std::min(255u, ((b >> sh) & 0xFF) + uint32_t(a * float((far[i] >> sh) & 0xFF)));
        };
        p.base[i] = 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
    }
    p.overlay.assign(p.base.size(), 0);
    stars(p.overlay, p.w, p.h, r, p.w * p.h / 2500, 1.0f, true);
}

// ---- CYBERPUNK ---------------------------------------------------------------
void cyberpunk(Picture& p) {
    const int W = p.w, H = p.h;
    const int horizon = int(float(H) * 0.62f);
    const Rgb top = {0.02f, 0.01f, 0.06f}, low = {0.30f, 0.04f, 0.28f}, magenta = {1.0f, 0.15f, 0.75f},
              cyan = {0.1f, 0.9f, 1.0f}, amber = {1.0f, 0.75f, 0.25f};
    std::vector<Rgb> img(size_t(W) * size_t(H));
    auto at = [&](int x, int y) -> Rgb& { return img[size_t(y) * size_t(W) + size_t(x)]; };
    // Sky: dark violet down to a hot magenta haze at the horizon.
    for (int y = 0; y < H; ++y) {
        const float t = std::min(1.0f, float(y) / float(horizon));
        const Rgb sky = mix(top, low, std::pow(t, 2.2f));
        for (int x = 0; x < W; ++x) at(x, y) = sky;
    }
    // Skyline: two rows of towers, the far one hazy; lit windows, neon strips.
    Rng r(9);
    for (int layer = 0; layer < 2; ++layer) {
        const float haze = layer == 0 ? 0.55f : 0.0f;
        const Rgb body = mix(Rgb{0.03f, 0.02f, 0.07f}, low, haze);
        int x = -int(r.f() * 20);
        while (x < W) {
            const int bw = std::max(6, int(float(W) * (0.10f + 0.16f * r.f())));
            const int bh = int(float(horizon) * (layer == 0 ? 0.35f + 0.35f * r.f() : 0.20f + 0.45f * r.f()));
            const int y0 = horizon - bh;
            const bool sign = layer == 1 && r.f() < 0.45f;
            const Rgb neon = r.f() < 0.5f ? magenta : cyan;
            const int win = std::max(2, H / 180);
            for (int y = std::max(0, y0); y < horizon; ++y)
                for (int xx = std::max(0, x); xx < std::min(W, x + bw); ++xx) {
                    Rgb c = body;
                    const int wx = (xx - x) / win, wy = (y - y0) / win;
                    const bool window = (xx - x) % win != 0 && (y - y0) % win != 0 && wx % 2 == 1 && wy % 2 == 1;
                    if (window && hash2(wx + x * 7, wy, uint32_t(layer + 3)) > 0.62f) {
                        const float h = hash2(wx, wy + x, 99);
                        c = mix(body, h > 0.7f ? cyan : amber, layer == 0 ? 0.35f : 0.75f);
                    }
                    if (xx == x || xx == x + bw - 1) c = mix(c, neon, layer == 0 ? 0.15f : 0.5f);   // rim light
                    at(xx, y) = c;
                }
            if (sign) {                                            // a vertical neon sign
                const int sx = x + bw / 2 + int((r.f() - 0.5f) * float(bw) * 0.5f);
                const int sy0 = y0 + bh / 6, sy1 = sy0 + std::max(8, bh / 3);
                for (int y = std::max(0, sy0); y < std::min(horizon, sy1); ++y)
                    for (int dx = -win * 2; dx <= win * 2; ++dx) {
                        const int X = sx + dx;
                        if (X < 0 || X >= W) continue;
                        const float k = std::abs(dx) <= win / 2 ? 1.0f : 0.35f * std::exp(-float(std::abs(dx)) / float(win));
                        at(X, y) = add(at(X, y), neon, k);
                    }
            }
            x += bw + int(r.f() * float(W) * 0.04f);
        }
    }
    // Ground: a perspective grid toward a vanishing point beyond the picture
    // (to the right: the grid fans out from behind the game's screen).
    const float vx = float(W) * 1.6f, vy = float(horizon);
    for (int y = horizon; y < H; ++y) {
        const float depth = float(y - horizon + 1) / float(H - horizon);   // 0 far, 1 near
        const float z = 1.0f / depth;                                      // distance
        for (int x = 0; x < W; ++x) {
            Rgb c = mix(Rgb{0.06f, 0.01f, 0.08f}, Rgb{0.02f, 0.0f, 0.03f}, depth);
            // Lines across: evenly spaced in distance.
            const float across = std::fabs(std::fmod(z * 2.0f, 1.0f) - 0.5f);
            const float lineA = std::exp(-std::pow((0.5f - across) * z * 0.9f * float(H) / 220.0f, 2.0f) * 30.0f);
            // Lines toward the vanishing point: evenly spaced in angle.
            const float ang = std::atan2(float(y) - vy, vx - float(x));
            const float k = std::fabs(std::fmod(ang * 22.0f, 1.0f) - 0.5f);
            const float lineB = std::exp(-std::pow((0.5f - k) * depth * 7.0f, 2.0f) * 8.0f);
            // Blue at the horizon, purple, then red up close.
            const Rgb blue = {0.15f, 0.35f, 1.0f}, purple = {0.65f, 0.15f, 0.95f}, red = {1.0f, 0.08f, 0.12f};
            // (Compressed toward the horizon, so the red shows above the race's dashboard.)
            const Rgb grid = depth < 0.15f ? mix(blue, purple, depth / 0.15f)
                                           : mix(purple, red, std::min(1.0f, (depth - 0.15f) / 0.25f));
            c = add(c, grid, std::max(lineA, lineB) * (0.45f + 0.55f * depth));
            at(x, y) = c;
        }
    }
    // The horizon glows.
    for (int y = std::max(0, horizon - H / 40); y < std::min(H, horizon + H / 60); ++y) {
        const float k = std::exp(-std::pow(float(y - horizon) / float(std::max(1, H / 120)), 2.0f));
        for (int x = 0; x < W; ++x) at(x, y) = add(at(x, y), Rgb{0.3f, 0.3f, 1.0f}, 0.6f * k);   // where the grid starts blue
    }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            Rgb c = mul(at(x, y), edge_shade(x, W));
            if (x == W - 1 || x == W - 2) c = add(c, cyan, 0.5f);   // a neon trim along the picture
            p.base[size_t(y) * size_t(W) + size_t(x)] = pack(c);
        }
    // Overlay: rain, thin cyan-white streaks (it scrolls down).
    p.overlay.assign(p.base.size(), 0);
    Rng rr(17);
    const int drops = W * H / 900, len = std::max(6, H / 40);
    for (int i = 0; i < drops; ++i) {
        const int x = int(rr.f() * float(W)), y = int(rr.f() * float(H));
        const float a = 0.15f + 0.35f * rr.f();
        for (int k = 0; k < len; ++k) {
            const int Y = (y + k) % H, X = x - k / 6;
            if (X < 0) continue;
            p.overlay[size_t(Y) * size_t(W) + size_t(X)] = pack(Rgb{0.7f, 0.95f, 1.0f}, a * float(k) / float(len));
        }
    }
}

}  // namespace

Picture make(Style style, int w, int h) {
    Picture p;
    if (w <= 0 || h <= 0) return p;
    p.w = w;
    p.h = h;
    p.base.assign(size_t(w) * size_t(h), 0xFF000000u);
    switch (style) {
    case Carbon:    carbon(p); break;
    case Space: {
        space(p, false);
        Picture q;
        q.w = w; q.h = h;
        q.base.assign(p.base.size(), 0xFF000000u);
        space(q, true);
        p.right = std::move(q.base);
        p.rightOverlay = std::move(q.overlay);
        break;
    }
    case Cyberpunk: cyberpunk(p); break;
    default: break;
    }
    return p;
}

}  // namespace sides
