// The mesh-shader technique's push constants (MeshletPbr::Constants in main.cpp): Forward PBR's (pbr_constants.glsl),
// which pbr.frag reads, then the object's meshlets and the culling.
#extension GL_EXT_mesh_shader : require
#include "rimgui_vk_scene.glsl"
#include "rimgui_vk_meshlets.glsl"

layout(buffer_reference, scalar, buffer_reference_align = 4) buffer CullCounters {
    uint frustum; // Meshlets culled for being outside the view
    uint cone;    // Meshlets culled for facing away
};

layout(push_constant, scalar) uniform Constants {
    FrameData frame;
    ObjectData object;
    mat4 light_view_projection;
    uint shadow_map;
    int view_mode;
    int use_shadows;
    float shadow_bias;
    int pcf_radius;
    uint pad0;
    Meshlets meshlets;
    MeshletVertices meshlet_vertices;
    MeshletTriangles meshlet_triangles;
    CullCounters counters;
    uint meshlet_count;
    uint cull;            // 1 frustum, 2 normal cone (only for one-sided objects with a uniform scale)
    vec4 planes[6];       // The view frustum in world space, facing inwards
} pc;

const uint TASK_GROUP = 32u;

struct TaskPayload {
    uint meshlets[TASK_GROUP];
};
