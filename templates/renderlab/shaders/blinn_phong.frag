#version 330 core
#include "lighting.glsl"

in vec3 v_world;
in vec3 v_normal;
out vec4 o_color;

uniform vec3 u_camera_position;
uniform vec3 u_albedo, u_emissive;
uniform float u_shininess, u_specular;

void main()
{
    vec3 n = normalize(v_normal);
    vec3 v = normalize(u_camera_position - v_world);
    vec3 l = u_sun_direction;
    vec3 h = normalize(l + v);
    float diffuse = max(dot(n, l), 0.0);
    float highlight = diffuse > 0.0 ? pow(max(dot(n, h), 0.0), u_shininess) * u_specular : 0.0;
    vec3 colour = u_albedo * (diffuse * u_sun_color / 3.14159 + sky_light(n) * u_ambient) + highlight * u_sun_color + u_emissive;
    o_color = vec4(colour, 1.0);
}
