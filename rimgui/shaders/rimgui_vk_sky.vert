#version 460
// The full-screen triangle at the far plane (depth 1): drawn with depth LESS_OR_EQUAL inside the output's rendering,
// it shows only where nothing else was drawn. Frame::draw_sky draws it.
#include "rimgui_vk_scene.glsl"
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_uv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 1.0, 1.0);
}
