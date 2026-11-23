#pragma once
// Meshlets (rimgui/rt/meshlets.h) on the GPU for mesh shaders: each mesh's meshlets, vertex lists and packed triangles
// in buffers read by address, made on first use and kept while the mesh lives.
//
// Shaders read them as rimgui_vk_meshlets.glsl declares: Meshlets, MeshletVertices and MeshletTriangles.
#include <rimgui/rt/meshlets.h>
#include <rimgui/rt/vk/scene.h>

#include <map>
#include <memory>

namespace rimgui::rt::vk {

struct MeshletBuffers {
	std::shared_ptr<Buffer> meshlets, vertices, triangles;
	uint32_t count = 0;              // Meshlets.
	uint32_t triangle_count = 0;
};

class MeshletCache {
public:
	// The mesh's meshlets, built and uploaded the first time it is asked for.
	const MeshletBuffers &get(const std::shared_ptr<Mesh> &mesh);
	// Forgets meshes that no longer exist.
	void prune();
	std::size_t meshes() const { return cache_.size(); }

private:
	std::map<const Mesh *, std::pair<std::weak_ptr<Mesh>, MeshletBuffers>> cache_;
};

} // namespace rimgui::rt::vk
