// The Vulkan lab's frame and objects for HLSL shaders compiled with DXC (rimgui/rt/vk/scene.h), as
// rimgui_vk_scene.glsl gives them to GLSL:
//   #include "rimgui_vk_scene.hlsl"
//   FrameData f = load_frame(pc.frame);      ObjectData o = load_object(pc.object);      Vertex v = load_vertex(o, i);
// Buffers are read by address (vk::RawBufferLoad), field by field at the offsets the C++ structs have (scalar layout).
// Matrices come back transposed, as HLSL's constructors fill rows: transform with xform(m, v), which is OpenGL's m * v.
#ifndef RIMGUI_VK_SCENE_HLSL
#define RIMGUI_VK_SCENE_HLSL
#include "rimgui_vk.hlsl"

float4x4 load_matrix(uint64_t a)
{
    // Four columns, each a row here: mul(v, m) is then the column-major matrix times v.
    return float4x4(vk::RawBufferLoad<float4>(a), vk::RawBufferLoad<float4>(a + 16), vk::RawBufferLoad<float4>(a + 32),
        vk::RawBufferLoad<float4>(a + 48));
}
float4 xform(float4x4 m, float4 v) { return mul(v, m); }
float3 xform3(float4x4 m, float3 v) { return mul(v, (float3x3)m); }

struct FrameData {
    float4x4 view, projection, view_projection, inverse_view_projection, inverse_projection;
    float3 camera_position;
    float time;
    float2 resolution;
    int frame_index, accumulation;
    float3 sun_direction;
    float ambient;
    float3 sun_color;
    int has_environment;
    float3 sky;
    float environment_intensity;
    float3 horizon;
    float environment_rotation;
    float3 ground;
    float prefiltered_levels;
    uint environment, irradiance, prefiltered, brdf_lut;
    int environment_as_sky;
    uint object_count;
    uint64_t objects;
};

FrameData load_frame(uint64_t a)
{
    FrameData f;
    f.view = load_matrix(a);
    f.projection = load_matrix(a + 64);
    f.view_projection = load_matrix(a + 128);
    f.inverse_view_projection = load_matrix(a + 192);
    f.inverse_projection = load_matrix(a + 256);
    f.camera_position = vk::RawBufferLoad<float3>(a + 320);
    f.time = vk::RawBufferLoad<float>(a + 332);
    f.resolution = vk::RawBufferLoad<float2>(a + 336);
    f.frame_index = vk::RawBufferLoad<int>(a + 344);
    f.accumulation = vk::RawBufferLoad<int>(a + 348);
    f.sun_direction = vk::RawBufferLoad<float3>(a + 352);
    f.ambient = vk::RawBufferLoad<float>(a + 364);
    f.sun_color = vk::RawBufferLoad<float3>(a + 368);
    f.has_environment = vk::RawBufferLoad<int>(a + 380);
    f.sky = vk::RawBufferLoad<float3>(a + 384);
    f.environment_intensity = vk::RawBufferLoad<float>(a + 396);
    f.horizon = vk::RawBufferLoad<float3>(a + 400);
    f.environment_rotation = vk::RawBufferLoad<float>(a + 412);
    f.ground = vk::RawBufferLoad<float3>(a + 416);
    f.prefiltered_levels = vk::RawBufferLoad<float>(a + 428);
    f.environment = vk::RawBufferLoad<uint>(a + 432);
    f.irradiance = vk::RawBufferLoad<uint>(a + 436);
    f.prefiltered = vk::RawBufferLoad<uint>(a + 440);
    f.brdf_lut = vk::RawBufferLoad<uint>(a + 444);
    f.environment_as_sky = vk::RawBufferLoad<int>(a + 448);
    f.object_count = vk::RawBufferLoad<uint>(a + 452);
    f.objects = vk::RawBufferLoad<uint64_t>(a + 456, 8);
    return f;
}

struct ObjectData {
    float4x4 model, normal_matrix;
    float3 albedo;
    float alpha;
    float3 emissive;
    float roughness;
    float metallic, normal_scale, occlusion_strength, alpha_cutoff;
    int alpha_mode, maps;
    uint albedo_map, normal_map, metal_rough_map, occlusion_map, emissive_map;
    uint index_count;
    uint64_t vertices, indices;
};

ObjectData load_object(uint64_t a)
{
    ObjectData o;
    o.model = load_matrix(a);
    o.normal_matrix = load_matrix(a + 64);
    o.albedo = vk::RawBufferLoad<float3>(a + 128);
    o.alpha = vk::RawBufferLoad<float>(a + 140);
    o.emissive = vk::RawBufferLoad<float3>(a + 144);
    o.roughness = vk::RawBufferLoad<float>(a + 156);
    o.metallic = vk::RawBufferLoad<float>(a + 160);
    o.normal_scale = vk::RawBufferLoad<float>(a + 164);
    o.occlusion_strength = vk::RawBufferLoad<float>(a + 168);
    o.alpha_cutoff = vk::RawBufferLoad<float>(a + 172);
    o.alpha_mode = vk::RawBufferLoad<int>(a + 176);
    o.maps = vk::RawBufferLoad<int>(a + 180);
    o.albedo_map = vk::RawBufferLoad<uint>(a + 184);
    o.normal_map = vk::RawBufferLoad<uint>(a + 188);
    o.metal_rough_map = vk::RawBufferLoad<uint>(a + 192);
    o.occlusion_map = vk::RawBufferLoad<uint>(a + 196);
    o.emissive_map = vk::RawBufferLoad<uint>(a + 200);
    o.index_count = vk::RawBufferLoad<uint>(a + 204);
    o.vertices = vk::RawBufferLoad<uint64_t>(a + 208, 8);
    o.indices = vk::RawBufferLoad<uint64_t>(a + 216, 8);
    return o;
}

struct Vertex {
    float3 position;
    float3 normal;
    float2 uv;
};

Vertex load_vertex(ObjectData o, uint index)
{
    const uint64_t a = o.vertices + uint64_t(index) * 32;
    Vertex v;
    v.position = vk::RawBufferLoad<float3>(a);
    v.normal = vk::RawBufferLoad<float3>(a + 12);
    v.uv = vk::RawBufferLoad<float2>(a + 24);
    return v;
}
#endif
