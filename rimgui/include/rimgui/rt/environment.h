#pragma once
// Image-based lighting from an equirectangular HDR image (a .hdr file), prepared on the GPU the way realtime
// renderers do it (Karis 2013, "Real Shading in Unreal Engine 4"):
//   environment   the image as a cubemap with mipmaps (the sky, and what the others are made from)
//   irradiance    the cosine-weighted hemisphere integral round every normal: diffuse light
//   prefiltered   one mip level for each roughness, by GGX importance sampling: specular light (split sum, first half)
//   brdf_lut      the split sum's second half, by NoV and roughness, shared by all environments
//
// Shaders read them through rimgui_lighting.glsl (environment_diffuse, environment_specular, ambient_light), on
// texture units 12 to 15.
#include <rimgui/rt/shader.h>

#include <filesystem>
#include <string>

namespace rimgui::rt {

inline constexpr int environment_texture_unit = 12;

class Environment {
public:
	Environment() = default;
	~Environment();
	Environment(const Environment &) = delete;
	Environment &operator=(const Environment &) = delete;

	// Builds the BRDF table, which shading uses with or without an image; load() does it too.
	bool prepare(ShaderLibrary &shaders, std::string *error = nullptr) { return make_brdf_lut(shaders, error); }
	// Reads the image and builds every map; the previous environment stays when it fails. `shaders` must find the
	// kit's shaders (rimgui_*.frag).
	bool load(const std::filesystem::path &path, ShaderLibrary &shaders, std::string *error = nullptr);
	void clear();
	bool loaded() const { return environment_ != 0; }
	const std::string &path() const { return path_; }

	float intensity = 1;        // Multiplies everything the environment gives.
	float rotation_degrees = 0; // About the up axis.
	bool show_as_sky = true;    // The sky pass draws the image; off: the sun's gradient sky, still lit by the image.

	// Sets u_has_environment, u_environment, u_irradiance, u_prefiltered, u_brdf_lut, u_environment_intensity,
	// u_environment_rotation, u_prefiltered_levels and u_environment_as_sky on `program`, which must be in use. With no
	// environment loaded only u_has_environment (0) and the BRDF table are set.
	void bind(Program &program) const;

	GLuint environment() const { return environment_; }
	GLuint irradiance() const { return irradiance_; }
	GLuint prefiltered() const { return prefiltered_; }
	GLuint brdf_lut() const { return brdf_lut_; }
	int prefiltered_levels() const { return prefiltered_levels_; }
	// How long building took, in milliseconds (GPU work waited for).
	double build_ms() const { return build_ms_; }

private:
	bool make_brdf_lut(ShaderLibrary &shaders, std::string *error);
	GLuint environment_ = 0, irradiance_ = 0, prefiltered_ = 0;
	GLuint brdf_lut_ = 0;
	int prefiltered_levels_ = 0;
	std::string path_;
	double build_ms_ = 0;
};

} // namespace rimgui::rt
