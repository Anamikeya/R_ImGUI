#version 460
// The classic: Lambert diffuse and a Blinn-Phong highlight from the sun, and the environment's diffuse light.
#include "rimgui_vk_material.glsl"
#include "rimgui_vk_lighting.glsl"
layout(push_constant, scalar) uniform Constants { FrameData frame; ObjectData object; float shininess; float specular; } pc;

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main()
{
    FrameData f = pc.frame;
    Surface s = surface(pc.object, v_world, v_normal, v_uv);
    vec3 n = s.normal;
    vec3 v = normalize(f.camera_position - v_world);
    vec3 l = f.sun_direction;
    vec3 h = normalize(l + v);
    float diffuse = max(dot(n, l), 0.0);
    float highlight = diffuse > 0.0 ? pow(max(dot(n, h), 0.0), pc.shininess) * pc.specular : 0.0;
    vec3 colour = s.albedo * (diffuse * f.sun_color / 3.14159 + environment_diffuse(f, n) * s.occlusion) + highlight * f.sun_color + s.emissive;
    o_color = vec4(colour, pc.object.alpha_mode == 2 ? s.alpha : 1.0);
}
