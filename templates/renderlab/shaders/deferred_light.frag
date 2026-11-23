#version 330 core
#include "rimgui_lighting.glsl"
// The deferred lighting pass: one full-screen triangle that lights every pixel from the G-buffer, with the sun and
// image-based lighting, ambient occlusion from both the material and SSAO.
in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_albedo, u_normal, u_emissive, u_depth, u_ao;
uniform mat4 u_view, u_inverse_view_projection;
uniform vec3 u_camera_position;
uniform int u_use_ao, u_ao_only;

void main()
{
    float depth = texture(u_depth, v_uv).r;
    float ao = u_use_ao == 1 ? texture(u_ao, v_uv).r : 1.0;
    if (u_ao_only == 1) {
        o_color = vec4(vec3(ao), 1.0);
        return;
    }
    if (depth >= 1.0) {
        o_color = vec4(0.0);
        return;
    }
    vec4 world4 = u_inverse_view_projection * vec4(v_uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 world = world4.xyz / world4.w;
    vec4 ar = texture(u_albedo, v_uv), nm = texture(u_normal, v_uv), eo = texture(u_emissive, v_uv);
    vec3 albedo = ar.rgb;
    float roughness = ar.a, metallic = nm.a;
    vec3 n = normalize(transpose(mat3(u_view)) * nm.xyz); // View space back to world (the view is a rotation).
    vec3 v = normalize(u_camera_position - world);
    vec3 diffuse, specular;
    brdf(n, v, u_sun_direction, albedo, roughness, metallic, diffuse, specular);
    vec3 ambient = ambient_light(n, v, albedo, roughness, metallic, ao * eo.a);
    o_color = vec4((diffuse + specular) * u_sun_color + ambient + eo.rgb, 1.0);
}
