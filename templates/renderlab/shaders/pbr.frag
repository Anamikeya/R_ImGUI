#version 330 core
#include "lighting.glsl"
#include "brdf.glsl"

in vec3 v_world;
in vec3 v_normal;
out vec4 o_color;

uniform vec3 u_camera_position;
uniform vec3 u_albedo, u_emissive;
uniform float u_roughness, u_metallic;
uniform int u_view_mode; // 0 final, 1 diffuse, 2 specular, 3 shadow, 4 normals

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
    vec3 n = normalize(v_normal);
    vec3 v = normalize(u_camera_position - v_world);
    vec3 diffuse, specular;
    brdf(n, v, u_sun_direction, u_albedo, u_roughness, u_metallic, diffuse, specular);
    float s = shadow(v_world, n);

    // Ambient: the sky round the normal for diffuse, round the reflection for specular, blurred by roughness.
    float NoV = clamp(dot(n, v), 0.0, 1.0);
    vec3 f0 = mix(vec3(0.04), u_albedo, u_metallic);
    vec3 F = F_SchlickRoughness(NoV, f0, u_roughness);
    vec3 r = reflect(-v, n);
    vec3 ambient_diffuse = (1.0 - F) * (1.0 - u_metallic) * u_albedo * sky_light(n) * u_ambient;
    vec3 ambient_specular = F * mix(sky_light(r), sky_light(n), u_roughness) * u_ambient;

    vec3 direct_diffuse = diffuse * u_sun_color * s, direct_specular = specular * u_sun_color * s;
    vec3 colour;
    if (u_view_mode == 1) colour = direct_diffuse + ambient_diffuse;
    else if (u_view_mode == 2) colour = direct_specular + ambient_specular;
    else if (u_view_mode == 3) colour = vec3(s);
    else if (u_view_mode == 4) colour = n * 0.5 + 0.5;
    else colour = direct_diffuse + direct_specular + ambient_diffuse + ambient_specular + u_emissive;
    o_color = vec4(colour, 1.0);
}
