#include <rimgui/rt/model.h>

#include <map>
#include <utility>

namespace rimgui::rt {

Model load_model(const std::filesystem::path &path, std::string *error)
{
	const ModelData data = read_model(path, error);
	Model model;
	model.bounds_min = data.bounds_min;
	model.bounds_max = data.bounds_max;
	model.warnings = data.warnings;
	// Each mesh and each image once on the GPU, however many parts share it.
	std::map<const MeshData *, std::shared_ptr<Mesh>> meshes;
	std::map<std::pair<const Image *, bool>, std::shared_ptr<Texture>> textures;
	const auto texture = [&](const std::shared_ptr<const Image> &image, bool srgb) -> std::shared_ptr<Texture> {
		if (!image) {
			return nullptr;
		}
		auto &slot = textures[{image.get(), srgb}];
		if (!slot) {
			slot = Texture::from_image(*image, srgb);
		}
		return slot;
	};
	for (const PartData &part : data.parts) {
		Object o;
		o.name = part.name;
		auto &mesh = meshes[part.mesh.get()];
		if (!mesh) {
			mesh = std::make_shared<Mesh>(*part.mesh, part.name);
		}
		o.mesh = mesh;
		o.base = part.base;
		const MaterialData &m = part.material;
		Material &out = o.material;
		out.albedo = m.albedo;
		out.alpha = m.alpha;
		out.roughness = m.roughness;
		out.metallic = m.metallic;
		out.emissive = m.emissive;
		out.normal_scale = m.normal_scale;
		out.occlusion_strength = m.occlusion_strength;
		out.alpha_mode = m.alpha_mode;
		out.alpha_cutoff = m.alpha_cutoff;
		out.double_sided = m.double_sided;
		out.albedo_map = texture(m.albedo_map, true);
		out.normal_map = texture(m.normal_map, false);
		out.metal_rough_map = texture(m.metal_rough_map, false);
		out.occlusion_map = texture(m.occlusion_map, false);
		out.emissive_map = texture(m.emissive_map, true);
		o.source = data.source;
		model.parts.push_back(std::move(o));
	}
	return model;
}

} // namespace rimgui::rt
