#include <rimgui/rt/model.h>

namespace rimgui::rt {

Model load_model(const std::filesystem::path &path, std::string *error)
{
	const ModelData data = read_model(path, error);
	Model model;
	model.bounds_min = data.bounds_min;
	model.bounds_max = data.bounds_max;
	model.warnings = data.warnings;
	model.parts = scene_core::objects_from<Object>(
		data, [](const MeshData &mesh, const std::string &name) { return std::make_shared<Mesh>(mesh, name); },
		[](const Image &image, bool srgb) { return Texture::from_image(image, srgb); });
	return model;
}

} // namespace rimgui::rt
