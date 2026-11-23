#include <rimgui/rt/meshlets.h>

#include <algorithm>
#include <cmath>

namespace rimgui::rt {

namespace {

// A finished meshlet's sphere and normal cone, from its triangles.
void bound(Meshlet &m, const MeshData &mesh, const MeshletData &out)
{
	vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
	for (uint32_t i = 0; i < m.vertex_count; ++i) {
		const vec3 p = mesh.vertices[out.vertices[m.vertex_offset + i]].position;
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
	}
	m.centre = (lo + hi) * 0.5f;
	m.radius = 0;
	for (uint32_t i = 0; i < m.vertex_count; ++i) {
		m.radius = std::max(m.radius, length(mesh.vertices[out.vertices[m.vertex_offset + i]].position - m.centre));
	}
	// The cone: the triangles' average facing, and how far the farthest one leans from it.
	std::vector<vec3> normals;
	vec3 sum{0, 0, 0};
	for (uint32_t t = 0; t < m.triangle_count; ++t) {
		const uint32_t packed = out.triangles[m.triangle_offset + t];
		const vec3 a = mesh.vertices[out.vertices[m.vertex_offset + (packed & 0xFF)]].position;
		const vec3 b = mesh.vertices[out.vertices[m.vertex_offset + ((packed >> 8) & 0xFF)]].position;
		const vec3 c = mesh.vertices[out.vertices[m.vertex_offset + ((packed >> 16) & 0xFF)]].position;
		const vec3 n = cross(b - a, c - a);
		const float l = length(n);
		if (l > 1e-12f) {
			normals.push_back(n * (1.0f / l));
			sum = sum + normals.back();
		}
	}
	const float sum_length = length(sum);
	m.cone_axis = sum_length > 1e-6f ? sum * (1.0f / sum_length) : vec3{0, 0, 1};
	m.cone_cutoff = 1; // Never culls, unless the triangles are close enough to one facing.
	if (sum_length > 1e-6f && !normals.empty()) {
		float min_dot = 1;
		for (const vec3 &n : normals) {
			min_dot = std::min(min_dot, dot(n, m.cone_axis));
		}
		if (min_dot > 0.1f) {
			m.cone_cutoff = std::sqrt(1 - min_dot * min_dot);
		}
	}
}

} // namespace

MeshletData build_meshlets(const MeshData &mesh, uint32_t max_vertices, uint32_t max_triangles)
{
	max_vertices = std::clamp(max_vertices, 3u, 256u);
	max_triangles = std::clamp(max_triangles, 1u, 512u);
	MeshletData out;
	std::vector<int> local(mesh.vertices.size(), -1); // A mesh vertex's index in the meshlet being filled.
	Meshlet current{};
	const auto finish = [&] {
		if (current.triangle_count == 0) {
			return;
		}
		for (uint32_t i = 0; i < current.vertex_count; ++i) {
			local[out.vertices[current.vertex_offset + i]] = -1;
		}
		bound(current, mesh, out);
		out.meshlets.push_back(current);
		current = {};
		current.vertex_offset = uint32_t(out.vertices.size());
		current.triangle_offset = uint32_t(out.triangles.size());
	};
	for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
		const uint32_t v[3]{mesh.indices[t], mesh.indices[t + 1], mesh.indices[t + 2]};
		if (v[0] >= mesh.vertices.size() || v[1] >= mesh.vertices.size() || v[2] >= mesh.vertices.size()) {
			continue;
		}
		uint32_t fresh = 0;
		for (int k = 0; k < 3; ++k) {
			const bool repeated = (k > 0 && v[k] == v[0]) || (k > 1 && v[k] == v[1]);
			fresh += local[v[k]] < 0 && !repeated ? 1 : 0;
		}
		if (current.vertex_count + fresh > max_vertices || current.triangle_count + 1 > max_triangles) {
			finish();
		}
		uint32_t packed = 0;
		for (int k = 0; k < 3; ++k) {
			if (local[v[k]] < 0) {
				local[v[k]] = int(current.vertex_count++);
				out.vertices.push_back(v[k]);
			}
			packed |= uint32_t(local[v[k]]) << (8 * k);
		}
		out.triangles.push_back(packed);
		++current.triangle_count;
	}
	finish();
	return out;
}

} // namespace rimgui::rt
