#pragma once
// Meshes cut into meshlets for mesh shaders: small groups of triangles (at most 64 vertices and 124 triangles, what
// mesh shaders on current GPUs do best) with what a task shader needs to cull a whole group at once: a bounding sphere,
// and a normal cone for back-facing groups (the conservative test meshoptimizer uses). Part of rimgui_rt_core.
//
// A task shader culls meshlet m, seen from `eye`, when
//   dot(centre - eye, cone_axis) >= cone_cutoff * length(centre - eye) + radius     (every triangle faces away)
// or when its sphere is outside the view frustum. cone_cutoff >= 1 means the cone never culls (triangles facing too
// many ways).
#include <rimgui/rt/math.h>
#include <rimgui/rt/mesh_data.h>

#include <cstdint>
#include <vector>

namespace rimgui::rt {

struct Meshlet {
	vec3 centre;
	float radius;
	vec3 cone_axis; // Unit length: the average facing of its triangles.
	float cone_cutoff;
	uint32_t vertex_offset, vertex_count;     // Into MeshletData::vertices.
	uint32_t triangle_offset, triangle_count; // Into MeshletData::triangles.
};
static_assert(sizeof(Meshlet) == 48, "Meshlet is read by shaders as 48 bytes");

struct MeshletData {
	std::vector<Meshlet> meshlets;
	std::vector<uint32_t> vertices;  // Indices into the mesh's vertices, each meshlet's run in turn.
	std::vector<uint32_t> triangles; // Three local vertex indices a triangle, one byte each (bits 0-7, 8-15, 16-23).
};

inline constexpr uint32_t meshlet_max_vertices = 64, meshlet_max_triangles = 124;

// Groups the triangles in their order (meshes from files and the test shapes are in spatial order already).
MeshletData build_meshlets(const MeshData &mesh, uint32_t max_vertices = meshlet_max_vertices, uint32_t max_triangles = meshlet_max_triangles);

} // namespace rimgui::rt
