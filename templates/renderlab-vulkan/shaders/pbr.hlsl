// Forward PBR in HLSL, compiled by DXC: mesh.vert and pbr.frag line for line, both stages in this file
// ("pbr.hlsl:VSMain", "pbr.hlsl:PSMain"), with the same push constants (pbr_constants.glsl). The lab's "Forward PBR
// (HLSL)" draws with it, for comparing the two languages' output: it should be the same picture.
#include "rimgui_vk_material.hlsl"
#include "rimgui_vk_lighting.hlsl"

struct Constants {
    uint64_t frame;
    uint64_t object;
    column_major float4x4 light_view_projection;
    uint shadow_map;
    int view_mode;
    int use_shadows;
    float shadow_bias;
    int pcf_radius;
};
[[vk::push_constant]] Constants pc;

struct Varyings {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 world : WORLD;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

Varyings VSMain(uint index : SV_VertexID)
{
    FrameData f = load_frame(pc.frame);
    ObjectData o = load_object(pc.object);
    Vertex v = load_vertex(o, index);
    float4 world = xform(o.model, float4(v.position, 1.0));
    Varyings out_;
    out_.world = world.xyz;
    out_.normal = xform3(o.normal_matrix, v.normal);
    out_.uv = v.uv;
    out_.position = rimgui_vk_clip(xform(f.view_projection, world));
    return out_;
}

float shadow(FrameData f, float3 world, float3 n)
{
    if (pc.use_shadows == 0) return 1.0;
    float slope = 1.0 - saturate(dot(n, f.sun_direction));
    // The push constant matrix is column-major: mul(m, v) is m * v.
    float4 p = mul(pc.light_view_projection, float4(world + n * (0.02 * slope), 1.0));
    float3 uvz = p.xyz / p.w * 0.5 + 0.5;
    if (uvz.z > 1.0 || any(uvz.xy < 0.0) || any(uvz.xy > 1.0)) return 1.0;
    float2 texel = 1.0 / float2(size_2d(pc.shadow_map));
    float lit = 0.0, count = 0.0;
    for (int y = -pc.pcf_radius; y <= pc.pcf_radius; ++y) {
        for (int x = -pc.pcf_radius; x <= pc.pcf_radius; ++x) {
            float stored = sample_nearest(pc.shadow_map, uvz.xy + float2(x, y) * texel).r;
            lit += uvz.z - pc.shadow_bias <= stored ? 1.0 : 0.0;
            count += 1.0;
        }
    }
    return lit / count;
}

float4 PSMain(Varyings i, bool front : SV_IsFrontFace) : SV_Target0
{
    FrameData f = load_frame(pc.frame);
    ObjectData o = load_object(pc.object);
    Surface s = surface(o, i.world, i.normal, i.uv, front);
    float3 n = s.normal;
    float3 v = normalize(f.camera_position - i.world);
    float3 diffuse, specular;
    brdf(n, v, f.sun_direction, s.albedo, s.roughness, s.metallic, diffuse, specular);
    float sh = shadow(f, i.world, normalize(i.normal));
    float3 direct_diffuse = diffuse * f.sun_color * sh, direct_specular = specular * f.sun_color * sh;
    float3 ambient = ambient_light(f, n, v, s.albedo, s.roughness, s.metallic, s.occlusion);
    float3 colour;
    if (pc.view_mode == 1) colour = direct_diffuse;
    else if (pc.view_mode == 2) colour = direct_specular;
    else if (pc.view_mode == 3) colour = float3(sh, sh, sh);
    else if (pc.view_mode == 4) colour = n * 0.5 + 0.5;
    else if (pc.view_mode == 5) colour = ambient;
    else colour = direct_diffuse + direct_specular + ambient + s.emissive;
    return float4(colour, o.alpha_mode == 2 ? s.alpha : 1.0);
}
