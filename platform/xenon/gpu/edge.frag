#version 450
// ANTI-ALIAS: the track video scaled up with its diagonal edges smoothed.
// Each output pixel sits in a square of four source texels; the square is
// split into two triangles along whichever diagonal joins the more alike
// pair (the edge runs that way), and the pixel is interpolated within its
// triangle, so stairs along a diagonal become a line. The weights are
// sharpened toward the texels, so flat edges stay crisp.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec2 v_size;
layout(binding = 0) uniform sampler2D u_tex;
layout(location = 0) out vec4 o_color;

// (A macro: xsc takes no function calls.)
#define LUMA(c) (dot((c).rgb, vec3(0.299, 0.587, 0.114)) + (c).a * 0.5)

void main() {
    vec2 q = v_uv * v_size - 0.5;
    vec2 i = floor(q);
    vec2 frac0 = q - i;
    vec2 t = 1.0 / v_size;
    vec2 base = (i + 0.5) * t;
    vec4 a = texture(u_tex, base);
    vec4 b = texture(u_tex, base + vec2(t.x, 0.0));
    vec4 c = texture(u_tex, base + vec2(0.0, t.y));
    vec4 d = texture(u_tex, base + t);
    // Crisper weights: a steeper ramp across the middle of the square.
    vec2 f = clamp((frac0 - 0.5) * 2.0 + 0.5, 0.0, 1.0);
    // a-d alike: split along a-d (x = y); else along b-c (x + y = 1).
    float ad = step(abs(LUMA(a) - LUMA(d)), abs(LUMA(b) - LUMA(c)));
    // Along a-d: above the diagonal (x > y) a, b, d; below a, c, d.
    float up = step(f.y, f.x);
    vec4 adUp = a + f.x * (b - a) + f.y * (d - b);
    vec4 adDown = a + f.y * (c - a) + f.x * (d - c);
    // Along b-c: before the diagonal a, b, c; after b, c, d.
    float before = step(f.x + f.y, 1.0);
    vec4 bcBefore = a + f.x * (b - a) + f.y * (c - a);
    vec4 bcAfter = d + (1.0 - f.x) * (c - d) + (1.0 - f.y) * (b - d);
    vec4 alongAd = mix(adDown, adUp, up);
    vec4 alongBc = mix(bcAfter, bcBefore, before);
    o_color = mix(alongBc, alongAd, ad) * v_color;
}
