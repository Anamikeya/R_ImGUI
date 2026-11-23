// The lab's BRDF and lights in HLSL: rimgui_brdf.glsl and rimgui_vk_lighting.glsl, function for function, so an HLSL
// technique shades as the GLSL ones do.
#ifndef RIMGUI_VK_LIGHTING_HLSL
#define RIMGUI_VK_LIGHTING_HLSL
#include "rimgui_vk_scene.hlsl"

static const float PI = 3.14159265359;

float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float ggx_v = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float ggx_l = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(ggx_v + ggx_l, 1e-5);
}

float3 F_Schlick(float VoH, float3 f0) { return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0); }

// Light from one direction, per unit of incoming radiance, split into diffuse and specular, each times NoL.
void brdf(float3 n, float3 v, float3 l, float3 albedo, float roughness, float metallic, out float3 diffuse, out float3 specular)
{
    float3 h = normalize(v + l);
    float NoV = abs(dot(n, v)) + 1e-5;
    float NoL = saturate(dot(n, l));
    float NoH = saturate(dot(n, h));
    float VoH = saturate(dot(v, h));
    float a = max(roughness * roughness, 0.002);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = F_Schlick(VoH, f0);
    specular = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F * NoL;
    diffuse = (1.0 - F) * (1.0 - metallic) * albedo / PI * NoL;
}

float3 sky_light(FrameData f, float3 dir)
{
    float up = clamp(dir.y, -1.0, 1.0);
    return up > 0.0 ? lerp(f.horizon, f.sky, up) : lerp(f.horizon, f.ground, -up);
}

float3 environment_direction(FrameData f, float3 d)
{
    float c = cos(f.environment_rotation), s = sin(f.environment_rotation);
    return float3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

float3 environment_diffuse(FrameData f, float3 n)
{
    if (f.has_environment == 1) return sample_cube(f.irradiance, environment_direction(f, n), 0.0).rgb * f.environment_intensity;
    return sky_light(f, n) * f.ambient;
}

float3 environment_specular(FrameData f, float3 r, float roughness)
{
    if (f.has_environment == 1) {
        float level = roughness * (f.prefiltered_levels - 1.0);
        return sample_cube(f.prefiltered, environment_direction(f, r), level).rgb * f.environment_intensity;
    }
    return lerp(sky_light(f, r), sky_light(f, float3(0.0, 1.0, 0.0)) * 0.6 + sky_light(f, r) * 0.4, roughness) * f.ambient;
}

float2 environment_brdf(FrameData f, float NoV, float roughness)
{
    return sample_2d_lod(f.brdf_lut, float2(saturate(NoV), saturate(roughness)), 0.0).rg;
}

// Image-based light with multiple scattering compensation (Fdez-Aguera 2019), times occlusion.
float3 ambient_light(FrameData f, float3 n, float3 v, float3 albedo, float roughness, float metallic, float occlusion)
{
    float NoV = clamp(dot(n, v), 1e-4, 1.0);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float2 ab = environment_brdf(f, NoV, roughness);
    float3 Fr = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), f0) - f0;
    float3 k_s = f0 + Fr * pow(1.0 - NoV, 5.0);
    float3 FssEss = k_s * ab.x + ab.y;
    float Ess = ab.x + ab.y;
    float Ems = 1.0 - Ess;
    float3 F_avg = f0 + (1.0 - f0) / 21.0;
    float3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
    float3 k_d = albedo * (1.0 - metallic) * (1.0 - FssEss - FmsEms);
    float3 irradiance = environment_diffuse(f, n);
    float3 radiance = environment_specular(f, reflect(-v, n), roughness);
    return (FssEss * radiance + (FmsEms + k_d) * irradiance) * occlusion;
}
#endif
