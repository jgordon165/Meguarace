#version 450
// The texel times the vertex colour (SDL's colour and alpha mod); fills
// sample a white texel.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(binding = 0) uniform sampler2D u_tex;
layout(location = 0) out vec4 o_color;
void main() {
    o_color = texture(u_tex, v_uv) * v_color;
}
