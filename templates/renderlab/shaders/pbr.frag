#version 330 core
#include "rimgui_material.glsl"
#include "rimgui_lighting.glsl"
// Forward PBR: the sun through a shadow map, and image-based lighting from the environment (or the gradient sky).

in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
out vec4 o_color;

uniform vec3 u_camera_position;
uniform int u_view_mode; // 0 final, 1 diffuse, 2 specular, 3 shadow, 4 normals, 5 ambient only

uniform int u_use_shadows;
uniform sampler2D u_shadow_map;
uniform mat4 u_light_view_projection;
uniform float u_shadow_bias;
uniform int u_pcf_radius;

float shadow(vec3 world, vec3 n)
{
    if (u_use_shadows == 0) return 1.0;
    // Pushed out along the normal, more where the light grazes the surface.
    float slope = 1.0 - clamp(dot(n, u_sun_direction), 0.0, 1.0);
    vec4 p = u_light_view_projection * vec4(world + n * (0.02 * slope), 1.0);
    vec3 uvz = p.xyz / p.w * 0.5 + 0.5;
    if (uvz.z > 1.0 || any(lessThan(uvz.xy, vec2(0.0))) || any(greaterThan(uvz.xy, vec2(1.0)))) return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(u_shadow_map, 0));
    float lit = 0.0, count = 0.0;
    for (int y = -u_pcf_radius; y <= u_pcf_radius; ++y) {
        for (int x = -u_pcf_radius; x <= u_pcf_radius; ++x) {
            float stored = texture(u_shadow_map, uvz.xy + vec2(x, y) * texel).r;
            lit += uvz.z - u_shadow_bias <= stored ? 1.0 : 0.0;
            count += 1.0;
        }
    }
    return lit / count;
}

void main()
{
    Surface s = surface(v_world, v_normal, v_uv);
    vec3 n = s.normal;
    vec3 v = normalize(u_camera_position - v_world);
    vec3 diffuse, specular;
    brdf(n, v, u_sun_direction, s.albedo, s.roughness, s.metallic, diffuse, specular);
    float sh = shadow(v_world, normalize(v_normal));
    vec3 direct_diffuse = diffuse * u_sun_color * sh, direct_specular = specular * u_sun_color * sh;
    vec3 ambient = ambient_light(n, v, s.albedo, s.roughness, s.metallic, s.occlusion);

    vec3 colour;
    if (u_view_mode == 1) colour = direct_diffuse;
    else if (u_view_mode == 2) colour = direct_specular;
    else if (u_view_mode == 3) colour = vec3(sh);
    else if (u_view_mode == 4) colour = n * 0.5 + 0.5;
    else if (u_view_mode == 5) colour = ambient;
    else colour = direct_diffuse + direct_specular + ambient + s.emissive;
    o_color = vec4(colour, u_alpha_mode == 2 ? s.alpha : 1.0);
}
