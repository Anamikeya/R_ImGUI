#pragma once
// Hardware ray tracing's acceleration structures for a scene (VK_KHR_acceleration_structure): one bottom level per
// mesh, built once and shared by every object using it, and a top level over the visible objects, rebuilt when the
// scene's geometry changes (objects added, moved, hidden; an alpha mode changed), not when the camera moves.
//
// Shaders reach the top level as u_scene (binding 4: define RIMGUI_RAY_TRACING or RIMGUI_RAY_QUERY before including
// rimgui_vk.glsl). Each instance's custom index is its object's index in Scene::objects, so a hit finds its object's
// data, material and vertices with object_at(frame, gl_InstanceCustomIndexEXT) (rimgui_vk_scene.glsl). Opaque materials
// are marked opaque; masked and blended ones run any-hit shaders (or the candidate loop of a ray query) for their alpha.
#include <rimgui/rt/vk/gpu.h>
#include <rimgui/rt/vk/scene.h>

#include <map>
#include <memory>

namespace rimgui::rt::vk {

class AccelerationStructures {
public:
	AccelerationStructures() = default;
	~AccelerationStructures();
	AccelerationStructures(const AccelerationStructures &) = delete;
	AccelerationStructures &operator=(const AccelerationStructures &) = delete;

	// True when the GPU can build them.
	static bool supported();
	// Builds what is missing and, when the geometry changed, the top level again (waiting for the GPU first, so frames
	// in flight never see it change), and puts it in binding 4. False on a GPU without ray tracing.
	bool update(const Scene &scene);
	VkAccelerationStructureKHR top_level() const { return tlas_.handle; }

	// For the Timings panel.
	std::size_t bottom_levels() const { return blas_.size(); }
	std::size_t instances() const { return instance_count_; }
	std::size_t triangles() const { return triangle_count_; }
	double last_build_ms() const { return build_ms_; }
	int builds() const { return builds_; }

private:
	struct Structure {
		VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
		std::shared_ptr<Buffer> buffer;
		VkDeviceAddress address = 0;
	};
	static void destroy(Structure &s);
	Structure build_bottom(const Mesh &mesh);
	std::map<const Mesh *, std::pair<std::weak_ptr<Mesh>, Structure>> blas_;
	Structure tlas_;
	std::size_t geometry_ = 0;
	std::size_t instance_count_ = 0, triangle_count_ = 0;
	double build_ms_ = 0;
	int builds_ = 0;
};

} // namespace rimgui::rt::vk
