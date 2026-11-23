#include <rimgui/rt/vk/accel.h>

#include <chrono>
#include <cstring>
#include <stdexcept>

namespace rimgui::rt::vk {

namespace {

VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a)
{
	return (v + a - 1) / a * a;
}

uint32_t scratch_alignment()
{
	VkPhysicalDeviceAccelerationStructurePropertiesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
	VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
	p.pNext = &as;
	vkGetPhysicalDeviceProperties2(vulkan::context()->physical_device, &p);
	return std::max(as.minAccelerationStructureScratchOffsetAlignment, 1u);
}

// A scratch buffer for one build, its address aligned as the GPU asks.
struct Scratch {
	std::shared_ptr<Buffer> buffer;
	VkDeviceAddress address = 0;
};

Scratch make_scratch(VkDeviceSize size)
{
	static const uint32_t alignment = scratch_alignment();
	Scratch s;
	s.buffer = Buffer::create(size + alignment, 0, Memory::Gpu, "AS scratch");
	s.address = align_up(s.buffer->address(), alignment);
	return s;
}

// The geometry that decides the top level: which meshes, where, which visible, which opaque.
std::size_t geometry_fingerprint(const Scene &scene)
{
	std::size_t h = 1469598103934665603ull;
	const auto mix = [&h](const void *data, std::size_t n) {
		const auto *p = static_cast<const unsigned char *>(data);
		for (std::size_t i = 0; i < n; ++i) {
			h = (h ^ p[i]) * 1099511628211ull;
		}
	};
	for (const auto &o : scene.objects) {
		const std::uintptr_t mesh = reinterpret_cast<std::uintptr_t>(o.mesh.get());
		mix(&mesh, sizeof mesh);
		const mat4 m = o.model();
		mix(m.m, sizeof m.m);
		const int flags[]{o.visible ? 1 : 0, int(o.material.alpha_mode), o.material.double_sided ? 1 : 0};
		mix(flags, sizeof flags);
	}
	const std::size_t count = scene.objects.size();
	mix(&count, sizeof count);
	return h;
}

} // namespace

bool AccelerationStructures::supported()
{
	const auto *c = vulkan::context();
	return c && c->features.acceleration_structure && Gpu::get();
}

AccelerationStructures::~AccelerationStructures()
{
	if (!vulkan::context()) {
		return;
	}
	for (auto &[mesh, entry] : blas_) {
		destroy(entry.second);
	}
	destroy(tlas_);
}

void AccelerationStructures::destroy(Structure &s)
{
	if (!s.handle) {
		return;
	}
	VkDevice device = vulkan::context()->device;
	VkAccelerationStructureKHR handle = s.handle;
	std::shared_ptr<Buffer> buffer = s.buffer;
	vulkan::defer([device, handle, buffer] { vkDestroyAccelerationStructureKHR(device, handle, nullptr); });
	s = {};
}

AccelerationStructures::Structure AccelerationStructures::build_bottom(const Mesh &mesh)
{
	VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	// Not marked opaque here: the instance decides (opaque materials force it), so one mesh serves both.
	auto &t = geometry.geometry.triangles;
	t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	t.vertexData.deviceAddress = mesh.vertices().address();
	t.vertexStride = sizeof(Vertex);
	t.maxVertex = mesh.vertex_count() ? mesh.vertex_count() - 1 : 0;
	t.indexType = VK_INDEX_TYPE_UINT32;
	t.indexData.deviceAddress = mesh.indices().address();

	VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	info.geometryCount = 1;
	info.pGeometries = &geometry;
	const uint32_t primitives = uint32_t(mesh.triangles());
	VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	const VkDevice device = vulkan::context()->device;
	vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &primitives, &sizes);

	Structure s;
	s.buffer = Buffer::create(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, Memory::Gpu, mesh.name() + " BLAS");
	VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
	create.buffer = s.buffer->buffer();
	create.size = sizes.accelerationStructureSize;
	create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	if (vkCreateAccelerationStructureKHR(device, &create, nullptr, &s.handle) != VK_SUCCESS) {
		throw std::runtime_error("vkCreateAccelerationStructureKHR failed for " + mesh.name());
	}
	Gpu::get()->name(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, uint64_t(s.handle), mesh.name() + " BLAS");
	const Scratch scratch = make_scratch(sizes.buildScratchSize);
	info.dstAccelerationStructure = s.handle;
	info.scratchData.deviceAddress = scratch.address;
	VkAccelerationStructureBuildRangeInfoKHR range{primitives, 0, 0, 0};
	const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
	vulkan::submit_now([&](VkCommandBuffer cb) { vkCmdBuildAccelerationStructuresKHR(cb, 1, &info, &ranges); });
	VkAccelerationStructureDeviceAddressInfoKHR address{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
	address.accelerationStructure = s.handle;
	s.address = vkGetAccelerationStructureDeviceAddressKHR(device, &address);
	return s;
}

bool AccelerationStructures::update(const Scene &scene)
{
	if (!supported()) {
		return false;
	}
	const std::size_t now = geometry_fingerprint(scene);
	if (tlas_.handle && now == geometry_) {
		return true;
	}
	const auto start = std::chrono::steady_clock::now();
	const VkDevice device = vulkan::context()->device;
	// Frames in flight trace the old top level: they finish before it is replaced.
	vkDeviceWaitIdle(device);

	// Bottom levels: one per mesh in use; those of meshes gone are dropped.
	std::map<const Mesh *, std::pair<std::weak_ptr<Mesh>, Structure>> kept;
	for (const auto &o : scene.objects) {
		if (!o.mesh || o.mesh->index_count() == 0 || kept.count(o.mesh.get())) {
			continue;
		}
		auto found = blas_.find(o.mesh.get());
		if (found != blas_.end() && !found->second.first.expired() && found->second.first.lock() == o.mesh) {
			kept.emplace(o.mesh.get(), found->second);
			found->second.second = {}; // Moved.
		} else {
			kept.emplace(o.mesh.get(), std::make_pair(std::weak_ptr<Mesh>(o.mesh), build_bottom(*o.mesh)));
		}
	}
	for (auto &[mesh, entry] : blas_) {
		destroy(entry.second);
	}
	blas_ = std::move(kept);

	// The top level over the visible objects.
	std::vector<VkAccelerationStructureInstanceKHR> instances;
	triangle_count_ = 0;
	for (std::size_t i = 0; i < scene.objects.size(); ++i) {
		const Object &o = scene.objects[i];
		if (!o.visible || !o.mesh || o.mesh->index_count() == 0) {
			continue;
		}
		VkAccelerationStructureInstanceKHR inst{};
		const mat4 m = o.model();
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 4; ++column) {
				inst.transform.matrix[row][column] = m.at(row, column);
			}
		}
		inst.instanceCustomIndex = uint32_t(i) & 0xFFFFFF;
		inst.mask = 0xFF;
		inst.instanceShaderBindingTableRecordOffset = 0;
		VkGeometryInstanceFlagsKHR flags = 0;
		if (o.material.alpha_mode == AlphaMode::Opaque) {
			flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
		}
		if (o.material.double_sided) {
			flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		}
		inst.flags = flags;
		inst.accelerationStructureReference = blas_.at(o.mesh.get()).second.address;
		instances.push_back(inst);
		triangle_count_ += o.mesh->triangles();
	}
	instance_count_ = instances.size();
	const std::size_t bytes = std::max<std::size_t>(instances.size(), 1) * sizeof(VkAccelerationStructureInstanceKHR);
	auto instance_buffer = Buffer::create(bytes, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, Memory::Upload, "TLAS instances");
	if (!instances.empty()) {
		std::memcpy(instance_buffer->mapped(), instances.data(), instances.size() * sizeof(VkAccelerationStructureInstanceKHR));
	}

	VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geometry.geometry.instances.data.deviceAddress = instance_buffer->address();
	VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
	info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	info.geometryCount = 1;
	info.pGeometries = &geometry;
	const uint32_t count = uint32_t(instances.size());
	VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &count, &sizes);

	destroy(tlas_);
	tlas_.buffer = Buffer::create(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, Memory::Gpu, "TLAS");
	VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
	create.buffer = tlas_.buffer->buffer();
	create.size = sizes.accelerationStructureSize;
	create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	if (vkCreateAccelerationStructureKHR(device, &create, nullptr, &tlas_.handle) != VK_SUCCESS) {
		throw std::runtime_error("vkCreateAccelerationStructureKHR failed for the scene");
	}
	Gpu::get()->name(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, uint64_t(tlas_.handle), "Scene TLAS");
	const Scratch scratch = make_scratch(sizes.buildScratchSize);
	info.dstAccelerationStructure = tlas_.handle;
	info.scratchData.deviceAddress = scratch.address;
	VkAccelerationStructureBuildRangeInfoKHR range{count, 0, 0, 0};
	const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
	vulkan::submit_now([&](VkCommandBuffer cb) { vkCmdBuildAccelerationStructuresKHR(cb, 1, &info, &ranges); });
	Gpu::get()->set_acceleration_structure(tlas_.handle);
	geometry_ = now;
	++builds_;
	build_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return true;
}

} // namespace rimgui::rt::vk
