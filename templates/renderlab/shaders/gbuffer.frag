#version 330 core
#include "rimgui_material.glsl"
// The G-buffer: albedo and roughness; the view-space normal and metallic; emissive and occlusion.
in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
layout(location = 0) out vec4 o_albedo_roughness;
layout(location = 1) out vec4 o_normal_metallic;
layout(location = 2) out vec4 o_emissive_occlusion;

uniform mat4 u_view;

void main()
{
    Surface s = surface(v_world, v_normal, v_uv);
    o_albedo_roughness = vec4(s.albedo, s.roughness);
    o_normal_metallic = vec4(normalize(mat3(u_view) * s.normal), s.metallic);
    o_emissive_occlusion = vec4(s.emissive, s.occlusion);
}
