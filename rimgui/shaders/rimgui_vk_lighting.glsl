// The Vulkan lab's lights, for including: #include "rimgui_vk_lighting.glsl"
// The same functions as rimgui_lighting.glsl (the OpenGL kit's), reading the sun, the sky and the environment from the
// frame: the sun is f.sun_direction and f.sun_color; everything else is ambient_light(f, ...).
#ifndef RIMGUI_VK_LIGHTING_GLSL
#define RIMGUI_VK_LIGHTING_GLSL
#include "rimgui_vk_scene.glsl"
#include "rimgui_brdf.glsl"

// The gradient sky's light from round a direction: a hemisphere light, ground below and sky above.
vec3 sky_light(FrameData f, vec3 dir)
{
    float up = clamp(dir.y, -1.0, 1.0);
    return up > 0.0 ? mix(f.horizon, f.sky, up) : mix(f.horizon, f.ground, -up);
}

// A world direction turned into the environment image's frame.
vec3 environment_direction(FrameData f, vec3 d)
{
    float c = cos(f.environment_rotation), s = sin(f.environment_rotation);
    return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

// Irradiance / pi round a normal: what a white Lambert surface reflects.
vec3 environment_diffuse(FrameData f, vec3 n)
{
    if (f.has_environment == 1) return sample_cube(f.irradiance, environment_direction(f, n), 0.0).rgb * f.environment_intensity;
    return sky_light(f, n) * f.ambient;
}

// The prefiltered environment along a reflection, for a roughness.
vec3 environment_specular(FrameData f, vec3 r, float roughness)
{
    if (f.has_environment == 1) {
        float level = roughness * (f.prefiltered_levels - 1.0);
        return sample_cube(f.prefiltered, environment_direction(f, r), level).rgb * f.environment_intensity;
    }
    return mix(sky_light(f, r), sky_light(f, vec3(0.0, 1.0, 0.0)) * 0.6 + sky_light(f, r) * 0.4, roughness) * f.ambient;
}

// The split sum's scale and bias for F0.
vec2 environment_brdf(FrameData f, float NoV, float roughness)
{
    return sample_2d_lod(f.brdf_lut, vec2(clamp(NoV, 0.0, 1.0), clamp(roughness, 0.0, 1.0)), 0.0).rg;
}

// All the light that is not the sun, on a surface: diffuse and specular image-based lighting, with multiple
// scattering energy compensation for the specular lobe (Fdez-Aguera 2019), times ambient occlusion.
vec3 ambient_light(FrameData f, vec3 n, vec3 v, vec3 albedo, float roughness, float metallic, float occlusion)
{
    float NoV = clamp(dot(n, v), 1e-4, 1.0);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec2 ab = environment_brdf(f, NoV, roughness);
    vec3 Fr = max(vec3(1.0 - roughness), f0) - f0;
    vec3 k_s = f0 + Fr * pow(1.0 - NoV, 5.0);
    vec3 FssEss = k_s * ab.x + ab.y;
    float Ess = ab.x + ab.y;
    float Ems = 1.0 - Ess;
    vec3 F_avg = f0 + (1.0 - f0) / 21.0;
    vec3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
    vec3 diffuse_colour = albedo * (1.0 - metallic);
    vec3 k_d = diffuse_colour * (1.0 - FssEss - FmsEms);
    vec3 irradiance = environment_diffuse(f, n);
    vec3 radiance = environment_specular(f, reflect(-v, n), roughness);
    return (FssEss * radiance + (FmsEms + k_d) * irradiance) * occlusion;
}

// The sky seen along a direction: the environment image when it is shown as the sky, else the gradient with a sun disc.
vec3 sky_colour(FrameData f, vec3 dir)
{
    if (f.has_environment == 1 && f.environment_as_sky == 1) {
        return sample_cube(f.environment, environment_direction(f, dir), 0.0).rgb * f.environment_intensity;
    }
    vec3 colour = dir.y > 0.0 ? mix(f.horizon, f.sky, pow(dir.y, 0.5)) : mix(f.horizon, f.ground, pow(-dir.y, 0.4));
    float sun = smoothstep(0.9995, 0.9998, dot(dir, f.sun_direction));
    return colour + f.sun_color * sun * 8.0;
}
#endif
