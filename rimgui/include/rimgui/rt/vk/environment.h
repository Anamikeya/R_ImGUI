#pragma once
// Image-based lighting from an equirectangular HDR image on Vulkan, built by compute shaders the way the OpenGL kit
// builds it (rimgui/rt/environment.h; Karis 2013):
//   environment   the image as a cubemap with mipmaps (RGBA32F: a photographed sun overflows half floats)
//   irradiance    the cosine-weighted hemisphere integral round every normal: diffuse light
//   prefiltered   one mip level for each roughness, by GGX importance sampling: specular light
//   brdf_lut      the split sum's second half, by NoV and roughness, shared by all environments
// Shaders reach them through FrameData (rimgui_vk_scene.glsl) and rimgui_vk_lighting.glsl.
#include <rimgui/rt/vk/gpu.h>
#include <rimgui/rt/vk/shader.h>

#include <filesystem>
#include <memory>
#include <string>

namespace rimgui::rt::vk {

struct FrameData;

class Environment {
public:
	Environment() = default;
	Environment(const Environment &) = delete;
	Environment &operator=(const Environment &) = delete;

	// Builds the BRDF table, which shading uses with or without an image; load() does it too.
	bool prepare(ShaderLibrary &shaders, std::string *error = nullptr);
	// Reads the image and builds every map; the previous environment stays when it fails.
	bool load(const std::filesystem::path &path, ShaderLibrary &shaders, std::string *error = nullptr);
	void clear();
	bool loaded() const { return environment_ != nullptr; }
	const std::string &path() const { return path_; }

	float intensity = 1;        // Multiplies everything the environment gives.
	float rotation_degrees = 0; // About the up axis.
	bool show_as_sky = true;    // The sky pass draws the image; off: the sun's gradient sky, still lit by the image.

	// The environment's part of FrameData: has_environment, the maps' indices, intensity, rotation, levels, as_sky.
	void fill(FrameData &frame) const;

	const std::shared_ptr<Image> &environment() const { return environment_; }
	const std::shared_ptr<Image> &irradiance() const { return irradiance_; }
	const std::shared_ptr<Image> &prefiltered() const { return prefiltered_; }
	const std::shared_ptr<Image> &brdf_lut() const { return brdf_lut_; }
	int prefiltered_levels() const { return prefiltered_levels_; }
	double build_ms() const { return build_ms_; }

private:
	std::shared_ptr<Image> environment_, irradiance_, prefiltered_, brdf_lut_;
	int prefiltered_levels_ = 0;
	std::string path_;
	double build_ms_ = 0;
};

} // namespace rimgui::rt::vk
