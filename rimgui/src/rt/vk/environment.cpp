#include <rimgui/rt/vk/environment.h>

#include <rimgui/rt/image.h>
#include <rimgui/rt/vk/scene.h>

#include <algorithm>
#include <chrono>

namespace rimgui::rt::vk {

namespace {

constexpr uint32_t environment_size = 512, irradiance_size = 32, prefiltered_size = 256, prefiltered_level_count = 6, lut_size = 256;

std::shared_ptr<Image> make_cube(uint32_t size, uint32_t mips, const char *name)
{
	ImageDesc d;
	d.width = d.height = size;
	d.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	d.mips = mips;
	d.cube = true;
	d.storage = true;
	d.name = name;
	return Image::create(d);
}

uint32_t groups(uint32_t size)
{
	return (size + 7) / 8;
}

struct FaceConstants {
	uint32_t source, target;
	int32_t size, face;
	float source_size, roughness;
};

} // namespace

bool Environment::prepare(ShaderLibrary &shaders, std::string *error)
{
	if (brdf_lut_) {
		return true;
	}
	Pipeline &p = shaders.compute("rimgui.brdf_lut", "rimgui_vk_brdf_lut.comp");
	if (!p.ready()) {
		if (error) *error = "the BRDF table shader did not build: " + p.error();
		return false;
	}
	ImageDesc d;
	d.width = d.height = lut_size;
	d.format = VK_FORMAT_R16G16_SFLOAT;
	d.storage = true;
	d.name = "BRDF table";
	auto lut = Image::create(d);
	vulkan::submit_now([&](VkCommandBuffer cb) {
		struct {
			uint32_t target;
			int32_t size;
		} c{lut->storage_index(), int32_t(lut_size)};
		p.bind(cb, c);
		vkCmdDispatch(cb, groups(lut_size), groups(lut_size), 1);
	});
	brdf_lut_ = lut;
	return true;
}

bool Environment::load(const std::filesystem::path &path, ShaderLibrary &shaders, std::string *error)
{
	const auto start = std::chrono::steady_clock::now();
	if (!prepare(shaders, error)) {
		return false;
	}
	std::string problem;
	const FloatImage image = read_float_image(path, &problem);
	if (!image.pixels) {
		if (error) *error = problem;
		return false;
	}
	const uint32_t most = vulkan::context()->properties.limits.maxImageDimension2D;
	if (uint32_t(image.width) > most || uint32_t(image.height) > most) {
		if (error) *error = path.string() + ": " + std::to_string(image.width) + "x" + std::to_string(image.height) +
							" is larger than this GPU's textures (" + std::to_string(most) + ")";
		return false;
	}
	Pipeline &to_cube = shaders.compute("rimgui.equirect_to_cube", "rimgui_vk_equirect_to_cube.comp");
	Pipeline &irradiance = shaders.compute("rimgui.irradiance", "rimgui_vk_irradiance.comp");
	Pipeline &prefilter = shaders.compute("rimgui.prefilter", "rimgui_vk_prefilter.comp");
	for (Pipeline *p : {&to_cube, &irradiance, &prefilter}) {
		if (!p->ready()) {
			if (error) *error = p->name() + " did not build: " + p->error();
			return false;
		}
	}
	rt::Image source;
	source.name = path.string();
	source.width = image.width;
	source.height = image.height;
	source.channels = 3;
	source.hdr = true;
	source.floats.assign(image.pixels.get(), image.pixels.get() + std::size_t(image.width) * std::size_t(image.height) * 3);
	const auto equirect = Image::from_image(source, false);
	if (!equirect) {
		if (error) *error = path.string() + ": could not be put on the GPU";
		return false;
	}

	auto cube = make_cube(environment_size, 0, "Environment");
	auto irr = make_cube(irradiance_size, 1, "Irradiance");
	auto pre = make_cube(prefiltered_size, prefiltered_level_count, "Prefiltered");
	vulkan::submit_now([&](VkCommandBuffer cb) {
		for (int face = 0; face < 6; ++face) {
			const FaceConstants c{equirect->sampled_index(), cube->level_storage_index(0, uint32_t(face)), int32_t(environment_size), face, 0, 0};
			to_cube.bind(cb, c);
			vkCmdDispatch(cb, groups(environment_size), groups(environment_size), 1);
		}
		cube->generate_mips(cb); // Barriers before and after.
		for (int face = 0; face < 6; ++face) {
			const FaceConstants c{cube->sampled_index(), irr->level_storage_index(0, uint32_t(face)), int32_t(irradiance_size), face,
				float(environment_size), 0};
			irradiance.bind(cb, c);
			vkCmdDispatch(cb, groups(irradiance_size), groups(irradiance_size), 1);
		}
		for (uint32_t level = 0; level < prefiltered_level_count; ++level) {
			const uint32_t size = std::max(prefiltered_size >> level, 1u);
			for (int face = 0; face < 6; ++face) {
				const FaceConstants c{cube->sampled_index(), pre->level_storage_index(level, uint32_t(face)), int32_t(size), face,
					float(environment_size), float(level) / float(prefiltered_level_count - 1)};
				prefilter.bind(cb, c);
				vkCmdDispatch(cb, groups(size), groups(size), 1);
			}
		}
		barrier(cb);
	});

	environment_ = cube;
	irradiance_ = irr;
	prefiltered_ = pre;
	prefiltered_levels_ = int(prefiltered_level_count);
	path_ = path.string();
	build_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return true;
}

void Environment::clear()
{
	environment_.reset();
	irradiance_.reset();
	prefiltered_.reset();
	path_.clear();
	prefiltered_levels_ = 0;
}

void Environment::fill(FrameData &f) const
{
	f.has_environment = loaded() ? 1 : 0;
	f.environment = environment_ ? environment_->sampled_index() : UINT32_MAX;
	f.irradiance = irradiance_ ? irradiance_->sampled_index() : UINT32_MAX;
	f.prefiltered = prefiltered_ ? prefiltered_->sampled_index() : UINT32_MAX;
	f.brdf_lut = brdf_lut_ ? brdf_lut_->sampled_index() : UINT32_MAX;
	f.environment_intensity = intensity;
	f.environment_rotation = rotation_degrees * pi / 180;
	f.prefiltered_levels = float(prefiltered_levels_);
	f.environment_as_sky = show_as_sky ? 1 : 0;
}

} // namespace rimgui::rt::vk
