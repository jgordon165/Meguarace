#version 450
// quad.vert, plus the texture's size in texels for edge.frag.
layout(location = 0) in vec4 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec2 a_size;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec2 v_size;
void main() {
    gl_Position = a_position;
    v_uv = a_uv;
    v_color = a_color;
    v_size = a_size;
}
