#include <rimgui/rt/vk/meshlets.h>

namespace rimgui::rt::vk {

namespace {

template <typename T>
std::shared_ptr<Buffer> upload(const std::vector<T> &data, const std::string &name)
{
	const VkDeviceSize bytes = std::max<VkDeviceSize>(data.size() * sizeof(T), 16);
	auto buffer = Buffer::create(bytes, 0, Memory::Gpu, name);
	if (!data.empty()) {
		buffer->upload(data.data(), data.size() * sizeof(T));
	}
	return buffer;
}

} // namespace

const MeshletBuffers &MeshletCache::get(const std::shared_ptr<Mesh> &mesh)
{
	auto found = cache_.find(mesh.get());
	if (found != cache_.end() && found->second.first.lock() == mesh) {
		return found->second.second;
	}
	const MeshletData data = build_meshlets(mesh->data());
	MeshletBuffers b;
	b.meshlets = upload(data.meshlets, mesh->name() + " meshlets");
	b.vertices = upload(data.vertices, mesh->name() + " meshlet vertices");
	b.triangles = upload(data.triangles, mesh->name() + " meshlet triangles");
	b.count = uint32_t(data.meshlets.size());
	b.triangle_count = uint32_t(data.triangles.size());
	auto &slot = cache_[mesh.get()];
	slot = {std::weak_ptr<Mesh>(mesh), std::move(b)};
	return slot.second;
}

void MeshletCache::prune()
{
	for (auto it = cache_.begin(); it != cache_.end();) {
		it = it->second.first.expired() ? cache_.erase(it) : std::next(it);
	}
}

} // namespace rimgui::rt::vk
