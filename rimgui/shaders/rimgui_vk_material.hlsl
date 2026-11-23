// A surface's material in HLSL pixel shaders: rimgui_vk_material.glsl, line for line (normal maps without tangents,
// from screen-space derivatives).
#ifndef RIMGUI_VK_MATERIAL_HLSL
#define RIMGUI_VK_MATERIAL_HLSL
#include "rimgui_vk_scene.hlsl"

struct Surface {
    float3 albedo;
    float alpha;
    float roughness;
    float metallic;
    float3 normal;
    float occlusion;
    float3 emissive;
};

bool has_map(ObjectData o, int bit) { return (o.maps & bit) != 0; }

float3 perturbed_normal(ObjectData o, float3 n, float3 world, float2 uv)
{
    float3 dp1 = ddx(world), dp2 = ddy(world);
    float2 duv1 = ddx(uv), duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float scale = rsqrt(max(max(dot(t, t), dot(b, b)), 1e-20));
    float3 m = sample_material(o.normal_map, uv).xyz * 2.0 - 1.0;
    m.xy *= o.normal_scale;
    // As the GLSL: columns t, -b, n.
    return normalize(t * scale * m.x - b * scale * m.y + n * m.z);
}

// `front`: SV_IsFrontFace.
Surface surface(ObjectData o, float3 world, float3 geometric_normal, float2 uv, bool front)
{
    Surface s;
    float4 base = float4(o.albedo, o.alpha);
    if (has_map(o, 1)) base *= sample_material(o.albedo_map, uv);
    s.albedo = base.rgb;
    s.alpha = base.a;
    if (o.alpha_mode == 1 && s.alpha < o.alpha_cutoff) discard;
    s.roughness = o.roughness;
    s.metallic = o.metallic;
    if (has_map(o, 4)) {
        float4 mr = sample_material(o.metal_rough_map, uv);
        s.roughness *= mr.g;
        s.metallic *= mr.b;
    }
    s.roughness = clamp(s.roughness, 0.03, 1.0);
    s.metallic = saturate(s.metallic);
    float3 n = normalize(geometric_normal);
    if (!front) n = -n;
    s.normal = has_map(o, 2) ? perturbed_normal(o, n, world, uv) : n;
    s.occlusion = has_map(o, 8) ? lerp(1.0, sample_material(o.occlusion_map, uv).r, o.occlusion_strength) : 1.0;
    s.emissive = o.emissive;
    if (has_map(o, 16)) s.emissive *= sample_material(o.emissive_map, uv).rgb;
    return s;
}
#endif
