#version 460
// The G-buffer: albedo and roughness; the view-space normal and metallic; emissive and occlusion.
#include "rimgui_vk_material.glsl"
layout(push_constant, scalar) uniform Constants { FrameData frame; ObjectData object; } pc;
layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 0) out vec4 o_albedo_roughness;
layout(location = 1) out vec4 o_normal_metallic;
layout(location = 2) out vec4 o_emissive_occlusion;

void main()
{
    Surface s = surface(pc.object, v_world, v_normal, v_uv);
    o_albedo_roughness = vec4(s.albedo, s.roughness);
    o_normal_metallic = vec4(normalize(mat3(pc.frame.view) * s.normal), s.metallic);
    o_emissive_occlusion = vec4(s.emissive, s.occlusion);
}
