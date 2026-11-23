#include <rimgui/rt/vk/scene.h>

#include <cstring>

namespace rimgui::rt::vk {

Mesh::Mesh(const MeshData &data, std::string name) : name_(std::move(name)), data_(data)
{
	data_.compute_bounds();
	min_ = data_.bounds_min;
	max_ = data_.bounds_max;
	vertex_count_ = uint32_t(data_.vertices.size());
	index_count_ = uint32_t(data_.indices.size());
	// Ray tracing reads them as build input; mesh shaders and vertex pulling read them by address.
	VkBufferUsageFlags extra = 0;
	if (vulkan::context()->features.acceleration_structure) {
		extra |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
	}
	const VkDeviceSize vertex_bytes = std::max<VkDeviceSize>(data_.vertices.size() * sizeof(Vertex), 16);
	const VkDeviceSize index_bytes = std::max<VkDeviceSize>(data_.indices.size() * sizeof(uint32_t), 16);
	vertices_ = Buffer::create(vertex_bytes, extra | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, Memory::Gpu, name_ + " vertices");
	indices_ = Buffer::create(index_bytes, extra | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, Memory::Gpu, name_ + " indices");
	if (!data_.vertices.empty()) {
		vertices_->upload(data_.vertices.data(), data_.vertices.size() * sizeof(Vertex));
	}
	if (!data_.indices.empty()) {
		static_assert(sizeof(unsigned) == sizeof(uint32_t));
		indices_->upload(data_.indices.data(), data_.indices.size() * sizeof(uint32_t));
	}
}

namespace {

uint32_t index_of(const std::shared_ptr<Image> &image)
{
	return image ? image->sampled_index() : UINT32_MAX;
}

} // namespace

VkDeviceAddress Scene::upload() const
{
	std::vector<ObjectData> data(std::max<std::size_t>(objects.size(), 1));
	for (std::size_t i = 0; i < objects.size(); ++i) {
		const Object &o = objects[i];
		const Material &m = o.material;
		ObjectData &d = data[i];
		d.model = o.model();
		d.normal_matrix = transpose(inverse(d.model));
		d.albedo = m.albedo;
		d.alpha = m.alpha;
		d.emissive = m.emissive;
		d.roughness = m.roughness;
		d.metallic = m.metallic;
		d.normal_scale = m.normal_scale;
		d.occlusion_strength = m.occlusion_strength;
		d.alpha_cutoff = m.alpha_cutoff;
		d.alpha_mode = int32_t(m.alpha_mode);
		d.maps = m.maps();
		d.albedo_map = index_of(m.albedo_map);
		d.normal_map = index_of(m.normal_map);
		d.metal_rough_map = index_of(m.metal_rough_map);
		d.occlusion_map = index_of(m.occlusion_map);
		d.emissive_map = index_of(m.emissive_map);
		d.index_count = o.mesh ? o.mesh->index_count() : 0;
		d.vertices = o.mesh ? o.mesh->vertices().address() : 0;
		d.indices = o.mesh ? o.mesh->indices().address() : 0;
	}
	uploaded_ = Gpu::get()->upload(data.data(), data.size() * sizeof(ObjectData));
	return uploaded_;
}

void Scene::draw(VkCommandBuffer cb, const Pipeline &pipeline, DrawFilter filter, vec3 camera_position, const void *constants,
	uint32_t size) const
{
	if (!pipeline.ready() || size < sizeof(DrawConstants) || size > 256) {
		return;
	}
	if (!uploaded_) {
		upload();
	}
	unsigned char bytes[256];
	std::memcpy(bytes, constants, size);
	pipeline.bind(cb, bytes, size);
	VkCullModeFlags culling = pipeline.cull();
	for (const Object *o : scene_core::draw_order(objects, filter, camera_position)) {
		if (o->mesh->index_count() == 0) {
			continue;
		}
		const VkDeviceAddress address = object_address(std::size_t(o - objects.data()));
		std::memcpy(bytes + offsetof(DrawConstants, object), &address, sizeof address);
		vkCmdPushConstants(cb, Gpu::get()->pipeline_layout(), VK_SHADER_STAGE_ALL, 0, size, bytes);
		const VkCullModeFlags wanted = o->material.double_sided ? VK_CULL_MODE_NONE : pipeline.cull();
		if (wanted != culling) {
			vkCmdSetCullMode(cb, wanted);
			culling = wanted;
		}
		vkCmdBindIndexBuffer(cb, o->mesh->indices().buffer(), 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cb, o->mesh->index_count(), 1, 0, 0, 0);
	}
	if (culling != pipeline.cull()) {
		vkCmdSetCullMode(cb, pipeline.cull());
	}
}

Model load_model(const std::filesystem::path &path, std::string *error)
{
	const ModelData data = read_model(path, error);
	Model model;
	model.bounds_min = data.bounds_min;
	model.bounds_max = data.bounds_max;
	model.warnings = data.warnings;
	model.parts = scene_core::objects_from<Object>(
		data, [](const MeshData &mesh, const std::string &name) { return std::make_shared<Mesh>(mesh, name); },
		[](const rt::Image &image, bool srgb) { return Image::from_image(image, srgb); });
	return model;
}

std::shared_ptr<Image> load_texture(const std::filesystem::path &path, bool srgb, std::string *error)
{
	const auto image = decode_image(path, error);
	if (!image) {
		return nullptr;
	}
	auto texture = Image::from_image(*image, srgb && !image->hdr);
	if (!texture && error) {
		*error = path.string() + ": could not be put on the GPU";
	}
	return texture;
}

} // namespace rimgui::rt::vk
