// What ray tracing shaders need from a hit, for including after rimgui_vk_scene.glsl (with RIMGUI_RAY_TRACING or
// RIMGUI_RAY_QUERY defined before rimgui_vk.glsl, for u_scene):
//   HitSurface s = hit_surface(frame, object, primitive, barycentrics, ray_direction);
//   bool solid = alpha_passes(frame, object, primitive, barycentrics, random);   // for any-hit and ray query loops
// Materials are read as rasterising reads them, at the top mip level (rays have no screen derivatives); normal maps use
// a tangent frame from the triangle's texture coordinates. Plus a small random number generator and GGX sampling.
#ifndef RIMGUI_VK_RAYTRACING_GLSL
#define RIMGUI_VK_RAYTRACING_GLSL
#include "rimgui_vk_scene.glsl"

// ---- Random numbers ------------------------------------------------------------------------------------------------

// PCG (O'Neill 2014), one 32-bit state per path.
uint pcg(inout uint state)
{
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float random01(inout uint state) { return float(pcg(state) >> 8) * (1.0 / 16777216.0); }
uint seed_for(uvec2 pixel, uint frame) { uint s = pixel.x * 1973u + pixel.y * 9277u + frame * 26699u; pcg(s); return s; }

// ---- The surface at a hit ------------------------------------------------------------------------------------------

struct HitSurface {
    vec3 position;          // World space.
    vec3 normal;            // Shading normal (normal map applied), facing the ray's side.
    vec3 geometric_normal;  // The triangle's own, facing the ray's side.
    vec2 uv;
    vec3 albedo;
    float alpha;
    float roughness;
    float metallic;
    vec3 emissive;
};

void triangle(ObjectData o, uint primitive, out Vertex a, out Vertex b, out Vertex c)
{
    a = vertex(o, o.indices.i[primitive * 3u]);
    b = vertex(o, o.indices.i[primitive * 3u + 1u]);
    c = vertex(o, o.indices.i[primitive * 3u + 2u]);
}

vec4 material_lod0(uint index, vec2 uv)
{
    return textureLod(sampler2D(u_textures[nonuniformEXT(index)], u_samplers[SAMPLER_LINEAR_REPEAT]), uv, 0.0);
}

// Whether a candidate hit on a masked or blended material counts: masked below the cutoff do not; blended count with
// probability alpha (stochastic transparency), so averaged over frames they let light through as they should.
bool alpha_passes(FrameData f, uint object, uint primitive, vec2 bary, float random)
{
    ObjectData o = object_at(f, object);
    if (o.alpha_mode == 0) return true;
    Vertex a, b, c;
    triangle(o, primitive, a, b, c);
    vec2 uv = a.uv * (1.0 - bary.x - bary.y) + b.uv * bary.x + c.uv * bary.y;
    float alpha = o.alpha * ((o.maps & 1) != 0 ? material_lod0(o.albedo_map, uv).a : 1.0);
    return o.alpha_mode == 1 ? alpha >= o.alpha_cutoff : random < alpha;
}

HitSurface hit_surface(FrameData f, uint object, uint primitive, vec2 bary, vec3 ray_direction)
{
    ObjectData o = object_at(f, object);
    Vertex a, b, c;
    triangle(o, primitive, a, b, c);
    vec3 w = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
    HitSurface s;
    vec3 p = a.position * w.x + b.position * w.y + c.position * w.z;
    s.position = (o.model * vec4(p, 1.0)).xyz;
    s.uv = a.uv * w.x + b.uv * w.y + c.uv * w.z;
    mat3 normal_matrix = mat3(o.normal_matrix);
    vec3 ng = normalize(normal_matrix * cross(b.position - a.position, c.position - a.position));
    vec3 n = normalize(normal_matrix * (a.normal * w.x + b.normal * w.y + c.normal * w.z));
    // Both normals on the side the ray came from (the back of double-sided surfaces).
    if (dot(ng, ray_direction) > 0.0) ng = -ng;
    if (dot(n, ng) < 0.0) n = -n;
    if ((o.maps & 2) != 0) {
        vec3 dp1 = b.position - a.position, dp2 = c.position - a.position;
        vec2 duv1 = b.uv - a.uv, duv2 = c.uv - a.uv;
        float det = duv1.x * duv2.y - duv1.y * duv2.x;
        if (abs(det) > 1e-12) {
            float r = 1.0 / det;
            mat3 model3 = mat3(o.model);
            vec3 t = normalize(model3 * ((dp1 * duv2.y - dp2 * duv1.y) * r));
            vec3 bt = normalize(model3 * ((dp2 * duv1.x - dp1 * duv2.x) * r));
            t = normalize(t - n * dot(n, t));
            bt = normalize(bt - n * dot(n, bt) - t * dot(t, bt));
            vec3 m = material_lod0(o.normal_map, s.uv).xyz * 2.0 - 1.0;
            m.xy *= o.normal_scale;
            // As rimgui_vk_material.glsl: glTF's green along +v, the image's v downwards, so -bt.
            vec3 mapped = normalize(mat3(t, -bt, n) * m);
            if (dot(mapped, ng) > 0.0) n = mapped;
        }
    }
    s.normal = n;
    s.geometric_normal = ng;
    vec4 base = vec4(o.albedo, o.alpha);
    if ((o.maps & 1) != 0) base *= material_lod0(o.albedo_map, s.uv);
    s.albedo = base.rgb;
    s.alpha = base.a;
    s.roughness = o.roughness;
    s.metallic = o.metallic;
    if ((o.maps & 4) != 0) {
        vec4 mr = material_lod0(o.metal_rough_map, s.uv);
        s.roughness *= mr.g;
        s.metallic *= mr.b;
    }
    s.roughness = clamp(s.roughness, 0.03, 1.0);
    s.metallic = clamp(s.metallic, 0.0, 1.0);
    s.emissive = o.emissive;
    if ((o.maps & 16) != 0) s.emissive *= material_lod0(o.emissive_map, s.uv).rgb;
    return s;
}

// ---- Sampling ------------------------------------------------------------------------------------------------------

// A direction round `n`, cosine-weighted: pdf = cos / pi.
vec3 sample_cosine(vec3 n, vec2 xi)
{
    float phi = 6.28318530718 * xi.x;
    float r = sqrt(xi.y);
    vec3 local = vec3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - xi.y, 0.0)));
    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    return normalize(t * local.x + b * local.y + n * local.z);
}

// A GGX half vector round `n` for `a` = roughness squared: pdf of the reflected direction = D NoH / (4 VoH).
vec3 sample_ggx_half(vec3 n, float a, vec2 xi)
{
    float phi = 6.28318530718 * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
    vec3 h = vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    return normalize(t * h.x + b * h.y + n * h.z);
}

// Offsets a ray's origin off the surface it leaves, along the geometric normal, scaled with the distance from the
// origin so large scenes do not self-intersect.
vec3 offset_origin(vec3 p, vec3 ng)
{
    return p + ng * (1e-4 + 1e-5 * max(max(abs(p.x), abs(p.y)), abs(p.z)));
}
#endif
