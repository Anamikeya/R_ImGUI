#version 330 core
#include "lighting.glsl"
#include "brdf.glsl"
// The deferred lighting pass: one full-screen triangle that lights every pixel from the G-buffer.
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
    vec4 ar = texture(u_albedo, v_uv), nm = texture(u_normal, v_uv);
    vec3 albedo = ar.rgb;
    float roughness = ar.a, metallic = nm.a;
    vec3 n = normalize(transpose(mat3(u_view)) * nm.xyz); // View space back to world (the view is a rotation).
    vec3 v = normalize(u_camera_position - world);
    vec3 diffuse, specular;
    brdf(n, v, u_sun_direction, albedo, roughness, metallic, diffuse, specular);
    float NoV = clamp(dot(n, v), 0.0, 1.0);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_SchlickRoughness(NoV, f0, roughness);
    vec3 ambient = ((1.0 - F) * (1.0 - metallic) * albedo * sky_light(n) + F * mix(sky_light(reflect(-v, n)), sky_light(n), roughness)) * u_ambient * ao;
    o_color = vec4((diffuse + specular) * u_sun_color + ambient + texture(u_emissive, v_uv).rgb, 1.0);
}
