// A surface's material, from the factors and maps Scene::draw sets, for including in fragment shaders:
//   #include "rimgui_material.glsl"
//   Surface s = surface(v_world, v_normal, v_uv);   // discards masked-out pixels
// Normal maps need no tangents: the tangent frame comes from screen-space derivatives (Schuler 2006).
#ifndef RIMGUI_MATERIAL_GLSL
#define RIMGUI_MATERIAL_GLSL

uniform vec3 u_albedo;
uniform float u_alpha;
uniform float u_roughness, u_metallic;
uniform vec3 u_emissive;
uniform float u_normal_scale, u_occlusion_strength;
uniform int u_alpha_mode; // 0 opaque, 1 mask, 2 blend
uniform float u_alpha_cutoff;
uniform int u_maps;       // 1 albedo, 2 normal, 4 metal-roughness, 8 occlusion, 16 emissive
uniform sampler2D u_albedo_map, u_normal_map, u_metal_rough_map, u_occlusion_map, u_emissive_map;

struct Surface {
    vec3 albedo;
    float alpha;
    float roughness;
    float metallic;
    vec3 normal;      // World space, unit length, facing the viewer for double-sided materials.
    float occlusion;  // 1: none.
    vec3 emissive;
};

bool has_map(int bit) { return (u_maps & bit) != 0; }

vec3 perturbed_normal(vec3 n, vec3 world, vec2 uv)
{
    vec3 dp1 = dFdx(world), dp2 = dFdy(world);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float scale = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-20));
    vec3 m = texture(u_normal_map, uv).xyz * 2.0 - 1.0;
    m.xy *= u_normal_scale;
    // glTF's normal maps have green pointing along +v; the image's v runs downwards here, so -b.
    return normalize(mat3(t * scale, -b * scale, n) * m);
}

Surface surface(vec3 world, vec3 geometric_normal, vec2 uv)
{
    Surface s;
    vec4 base = vec4(u_albedo, u_alpha);
    if (has_map(1)) base *= texture(u_albedo_map, uv);
    s.albedo = base.rgb;
    s.alpha = base.a;
    if (u_alpha_mode == 1 && s.alpha < u_alpha_cutoff) discard;
    s.roughness = u_roughness;
    s.metallic = u_metallic;
    if (has_map(4)) {
        vec4 mr = texture(u_metal_rough_map, uv);
        s.roughness *= mr.g;
        s.metallic *= mr.b;
    }
    s.roughness = clamp(s.roughness, 0.03, 1.0);
    s.metallic = clamp(s.metallic, 0.0, 1.0);
    vec3 n = normalize(geometric_normal);
    if (!gl_FrontFacing) n = -n; // The back of a double-sided surface.
    s.normal = has_map(2) ? perturbed_normal(n, world, uv) : n;
    s.occlusion = has_map(8) ? mix(1.0, texture(u_occlusion_map, uv).r, u_occlusion_strength) : 1.0;
    s.emissive = u_emissive;
    if (has_map(16)) s.emissive *= texture(u_emissive_map, uv).rgb;
    return s;
}
#endif
