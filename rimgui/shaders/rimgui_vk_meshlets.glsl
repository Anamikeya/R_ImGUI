// Meshlets as task and mesh shaders read them (rimgui/rt/vk/meshlets.h), for including after rimgui_vk_scene.glsl.
#ifndef RIMGUI_VK_MESHLETS_GLSL
#define RIMGUI_VK_MESHLETS_GLSL

struct Meshlet {
    vec3 centre;
    float radius;
    vec3 cone_axis;
    float cone_cutoff; // >= 1: the cone never culls
    uint vertex_offset, vertex_count;
    uint triangle_offset, triangle_count;
};
layout(buffer_reference, scalar, buffer_reference_align = 16) readonly buffer Meshlets { Meshlet m[]; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer MeshletVertices { uint v[]; };
// Three local vertex indices a triangle, a byte each.
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer MeshletTriangles { uint t[]; };

uvec3 meshlet_triangle(uint packed) { return uvec3(packed & 0xFFu, (packed >> 8) & 0xFFu, (packed >> 16) & 0xFFu); }

// Every triangle of the meshlet faces away from `eye` (all in object space, or all in world space).
bool cone_culled(Meshlet m, vec3 centre, float radius, vec3 axis, vec3 eye)
{
    if (m.cone_cutoff >= 1.0) return false;
    vec3 to = centre - eye;
    return dot(to, axis) >= m.cone_cutoff * length(to) + radius;
}

// A sphere outside one of the frustum's planes (planes facing inwards: dot(n, p) + d >= 0 inside).
bool frustum_culled(vec4 planes[6], vec3 centre, float radius)
{
    for (int i = 0; i < 6; ++i) {
        if (dot(planes[i].xyz, centre) + planes[i].w < -radius) return true;
    }
    return false;
}
#endif
