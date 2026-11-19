#version 330 core
// The G-buffer: albedo and roughness; the view-space normal and metallic; emissive.
in vec3 v_normal;
layout(location = 0) out vec4 o_albedo_roughness;
layout(location = 1) out vec4 o_normal_metallic;
layout(location = 2) out vec3 o_emissive;

uniform mat4 u_view;
uniform vec3 u_albedo, u_emissive;
uniform float u_roughness, u_metallic;

void main()
{
    o_albedo_roughness = vec4(u_albedo, u_roughness);
    o_normal_metallic = vec4(normalize(mat3(u_view) * normalize(v_normal)), u_metallic);
    o_emissive = u_emissive;
}
