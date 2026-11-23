// A surface's material from an object's factors and maps, for including in Vulkan fragment shaders:
//   #include "rimgui_vk_material.glsl"
//   Surface s = surface(pc.object, v_world, v_normal, v_uv);   // discards masked-out pixels
// The same as rimgui_material.glsl (the OpenGL kit's): normal maps need no tangents (Schuler 2006).
#ifndef RIMGUI_VK_MATERIAL_GLSL
#define RIMGUI_VK_MATERIAL_GLSL
#include "rimgui_vk_scene.glsl"

struct Surface {
    vec3 albedo;
    float alpha;
    float roughness;
    float metallic;
    vec3 normal;      // World space, unit length, facing the viewer for double-sided materials.
    float occlusion;  // 1: none.
    vec3 emissive;
};

bool has_map(ObjectData o, int bit) { return (o.maps & bit) != 0; }

vec3 perturbed_normal(ObjectData o, vec3 n, vec3 world, vec2 uv)
{
    vec3 dp1 = dFdx(world), dp2 = dFdy(world);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float scale = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-20));
    vec3 m = sample_material(o.normal_map, uv).xyz * 2.0 - 1.0;
    m.xy *= o.normal_scale;
    // glTF's normal maps have green pointing along +v; the image's v runs downwards here, so -b.
    return normalize(mat3(t * scale, -b * scale, n) * m);
}

Surface surface(ObjectData o, vec3 world, vec3 geometric_normal, vec2 uv)
{
    Surface s;
    vec4 base = vec4(o.albedo, o.alpha);
    if (has_map(o, 1)) base *= sample_material(o.albedo_map, uv);
    s.albedo = base.rgb;
    s.alpha = base.a;
    if (o.alpha_mode == 1 && s.alpha < o.alpha_cutoff) discard;
    s.roughness = o.roughness;
    s.metallic = o.metallic;
    if (has_map(o, 4)) {
        vec4 mr = sample_material(o.metal_rough_map, uv);
        s.roughness *= mr.g;
        s.metallic *= mr.b;
    }
    s.roughness = clamp(s.roughness, 0.03, 1.0);
    s.metallic = clamp(s.metallic, 0.0, 1.0);
    vec3 n = normalize(geometric_normal);
    if (!gl_FrontFacing) n = -n; // The back of a double-sided surface.
    s.normal = has_map(o, 2) ? perturbed_normal(o, n, world, uv) : n;
    s.occlusion = has_map(o, 8) ? mix(1.0, sample_material(o.occlusion_map, uv).r, o.occlusion_strength) : 1.0;
    s.emissive = o.emissive;
    if (has_map(o, 16)) s.emissive *= sample_material(o.emissive_map, uv).rgb;
    return s;
}
#endif
