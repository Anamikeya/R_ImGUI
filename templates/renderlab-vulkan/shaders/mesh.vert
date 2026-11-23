#version 460
// Scene geometry for every technique: world position, normal and texture coordinates out. Vertices are pulled from the
// object's buffer by index (no vertex input), and clip space goes from OpenGL's conventions to Vulkan's depth.
#include "rimgui_vk_scene.glsl"
layout(push_constant, scalar) uniform Constants { FrameData frame; ObjectData object; } pc;

layout(location = 0) out vec3 v_world;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;

void main()
{
    Vertex v = vertex(pc.object, gl_VertexIndex);
    vec4 world = pc.object.model * vec4(v.position, 1.0);
    v_world = world.xyz;
    v_normal = mat3(pc.object.normal_matrix) * v.normal;
    v_uv = v.uv;
    gl_Position = rimgui_vk_clip(pc.frame.view_projection * world);
}
