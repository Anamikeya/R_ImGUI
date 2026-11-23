// The Vulkan lab's frame and objects as shaders read them (rimgui/rt/vk/scene.h), for including:
//   #include "rimgui_vk_scene.glsl"
//   layout(push_constant, scalar) uniform Constants { FrameData frame; ObjectData object; ...yours... } pc;
// FrameData has the camera, the sun and sky, the environment's maps and every object; ObjectData one object's
// transform, material and mesh. Vertices are pulled by index: Vertex v = vertex(pc.object, gl_VertexIndex).
#ifndef RIMGUI_VK_SCENE_GLSL
#define RIMGUI_VK_SCENE_GLSL
#include "rimgui_vk.glsl"

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
};
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Vertices { Vertex v[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer Indices { uint i[]; };

layout(buffer_reference, scalar, buffer_reference_align = 16) readonly buffer ObjectData {
    mat4 model;
    mat4 normal_matrix;
    vec3 albedo;
    float alpha;
    vec3 emissive;
    float roughness;
    float metallic;
    float normal_scale;
    float occlusion_strength;
    float alpha_cutoff;
    int alpha_mode; // 0 opaque, 1 mask, 2 blend
    int maps;       // 1 albedo, 2 normal, 4 metal-roughness, 8 occlusion, 16 emissive
    uint albedo_map, normal_map, metal_rough_map, occlusion_map, emissive_map; // Bindless indices.
    uint index_count;
    Vertices vertices;
    Indices indices;
};
const uint OBJECT_DATA_SIZE = 224u;

layout(buffer_reference, scalar, buffer_reference_align = 16) readonly buffer FrameData {
    mat4 view;
    mat4 projection;
    mat4 view_projection;
    mat4 inverse_view_projection;
    mat4 inverse_projection;
    vec3 camera_position;
    float time;
    vec2 resolution;
    int frame_index;
    int accumulation; // Frames in a row with nothing changed; 0 when progressive techniques start over.
    vec3 sun_direction; // Towards the sun.
    float ambient;
    vec3 sun_color;     // Colour times intensity.
    int has_environment;
    vec3 sky;
    float environment_intensity;
    vec3 horizon;
    float environment_rotation; // Radians about +y.
    vec3 ground;
    float prefiltered_levels;
    uint environment, irradiance, prefiltered, brdf_lut; // Cube, cube, cube, 2D.
    int environment_as_sky;
    uint object_count;
    uint64_t objects; // ObjectData[object_count]: object_at(frame, i).
};

// Object `i` of the scene (Scene::objects order), as ray tracing finds them by instance.
ObjectData object_at(FrameData f, uint i) { return ObjectData(f.objects + uint64_t(i) * uint64_t(OBJECT_DATA_SIZE)); }

Vertex vertex(ObjectData o, uint index) { return o.vertices.v[index]; }
#endif
