// Microfacet BRDF pieces, for including: #include "rimgui_brdf.glsl"
// Copy this into your own shaders folder under another name to try other models; the lab finds yours first.
#ifndef RIMGUI_BRDF_GLSL
#define RIMGUI_BRDF_GLSL
const float PI = 3.14159265359;

// Trowbridge-Reitz / GGX normal distribution (Walter et al. 2007). `a` is roughness squared.
float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Height-correlated Smith visibility for GGX (Heitz 2014): G / (4 NoL NoV).
float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float ggx_v = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float ggx_l = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(ggx_v + ggx_l, 1e-5);
}

vec3 F_Schlick(float VoH, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

// Fresnel for ambient light, which has no single half vector (a fit by Sebastien Lagarde).
vec3 F_SchlickRoughness(float NoV, vec3 f0, float roughness)
{
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - NoV, 5.0);
}

// Light from one direction, per unit of incoming radiance, split into diffuse and specular parts, each already
// multiplied by NoL.
void brdf(vec3 n, vec3 v, vec3 l, vec3 albedo, float roughness, float metallic, out vec3 diffuse, out vec3 specular)
{
    vec3 h = normalize(v + l);
    float NoV = abs(dot(n, v)) + 1e-5;
    float NoL = clamp(dot(n, l), 0.0, 1.0);
    float NoH = clamp(dot(n, h), 0.0, 1.0);
    float VoH = clamp(dot(v, h), 0.0, 1.0);
    float a = max(roughness * roughness, 0.002);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_Schlick(VoH, f0);
    specular = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F * NoL;
    diffuse = (1.0 - F) * (1.0 - metallic) * albedo / PI * NoL;
}
#endif
