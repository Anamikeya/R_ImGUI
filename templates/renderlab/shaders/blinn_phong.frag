#version 330 core
#include "rimgui_material.glsl"
#include "rimgui_lighting.glsl"
// The classic: Lambert diffuse and a Blinn-Phong highlight from the sun, and the environment's diffuse light.

in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
out vec4 o_color;

uniform vec3 u_camera_position;
uniform float u_shininess, u_specular;

void main()
{
    Surface s = surface(v_world, v_normal, v_uv);
    vec3 n = s.normal;
    vec3 v = normalize(u_camera_position - v_world);
    vec3 l = u_sun_direction;
    vec3 h = normalize(l + v);
    float diffuse = max(dot(n, l), 0.0);
    float highlight = diffuse > 0.0 ? pow(max(dot(n, h), 0.0), u_shininess) * u_specular : 0.0;
    vec3 colour = s.albedo * (diffuse * u_sun_color / 3.14159 + environment_diffuse(n) * s.occlusion) + highlight * u_sun_color + s.emissive;
    o_color = vec4(colour, u_alpha_mode == 2 ? s.alpha : 1.0);
}
