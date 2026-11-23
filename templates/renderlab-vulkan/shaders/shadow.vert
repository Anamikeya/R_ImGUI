#version 460
// The shadow map: depth from the sun. Texture coordinates go on for alpha-masked materials (leaves, fences).
#include "rimgui_vk_scene.glsl"
layout(push_constant, scalar) uniform Constants { FrameData frame; ObjectData object; mat4 light_view_projection; } pc;
layout(location = 0) out vec2 v_uv;
void main()
{
    Vertex v = vertex(pc.object, gl_VertexIndex);
    v_uv = v.uv;
    gl_Position = rimgui_vk_clip(pc.light_view_projection * pc.object.model * vec4(v.position, 1.0));
}
