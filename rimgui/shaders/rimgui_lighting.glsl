// The lab's lights, for including: #include "rimgui_lighting.glsl"
//   the sun        u_sun_direction (towards it), u_sun_color (colour times intensity)
//   the sky        a gradient (u_sky, u_horizon, u_ground) scaled by u_ambient, or, when an HDR environment is
//                  loaded, image-based lighting from it
// Set with frame.set_lighting(program), which does both.
#ifndef RIMGUI_LIGHTING_GLSL
#define RIMGUI_LIGHTING_GLSL
#include "rimgui_brdf.glsl"

uniform vec3 u_sun_direction;
uniform vec3 u_sun_color;
uniform vec3 u_sky, u_horizon, u_ground;
uniform float u_ambient;

uniform int u_has_environment;
uniform samplerCube u_environment, u_irradiance, u_prefiltered;
uniform sampler2D u_brdf_lut;
uniform float u_environment_intensity;
uniform float u_environment_rotation; // Radians about +y.
uniform float u_prefiltered_levels;

// The gradient sky's light from round a direction: a hemisphere light, ground below and sky above.
vec3 sky_light(vec3 dir)
{
    float up = clamp(dir.y, -1.0, 1.0);
    return up > 0.0 ? mix(u_horizon, u_sky, up) : mix(u_horizon, u_ground, -up);
}

// A world direction turned into the environment image's frame.
vec3 environment_direction(vec3 d)
{
    float c = cos(u_environment_rotation), s = sin(u_environment_rotation);
    return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

// Irradiance / pi round a normal: what a white Lambert surface reflects.
vec3 environment_diffuse(vec3 n)
{
    if (u_has_environment == 1) return texture(u_irradiance, environment_direction(n)).rgb * u_environment_intensity;
    return sky_light(n) * u_ambient;
}

// The prefiltered environment along a reflection, for a roughness.
vec3 environment_specular(vec3 r, float roughness)
{
    if (u_has_environment == 1) {
        float level = roughness * (u_prefiltered_levels - 1.0);
        return textureLod(u_prefiltered, environment_direction(r), level).rgb * u_environment_intensity;
    }
    return mix(sky_light(r), sky_light(vec3(0.0, 1.0, 0.0)) * 0.6 + sky_light(r) * 0.4, roughness) * u_ambient;
}

// The split sum's scale and bias for F0.
vec2 environment_brdf(float NoV, float roughness)
{
    return texture(u_brdf_lut, vec2(clamp(NoV, 0.0, 1.0), clamp(roughness, 0.0, 1.0))).rg;
}

// All the light that is not the sun, on a surface: diffuse and specular image-based lighting, with multiple
// scattering energy compensation for the specular lobe (Fdez-Aguera 2019), times ambient occlusion.
vec3 ambient_light(vec3 n, vec3 v, vec3 albedo, float roughness, float metallic, float occlusion)
{
    float NoV = clamp(dot(n, v), 1e-4, 1.0);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec2 ab = environment_brdf(NoV, roughness);
    vec3 Fr = max(vec3(1.0 - roughness), f0) - f0;
    vec3 k_s = f0 + Fr * pow(1.0 - NoV, 5.0);
    vec3 FssEss = k_s * ab.x + ab.y;
    float Ess = ab.x + ab.y;
    float Ems = 1.0 - Ess;
    vec3 F_avg = f0 + (1.0 - f0) / 21.0;
    vec3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
    vec3 diffuse_colour = albedo * (1.0 - metallic);
    vec3 k_d = diffuse_colour * (1.0 - FssEss - FmsEms);
    vec3 irradiance = environment_diffuse(n);
    vec3 radiance = environment_specular(reflect(-v, n), roughness);
    return (FssEss * radiance + (FmsEms + k_d) * irradiance) * occlusion;
}
#endif
