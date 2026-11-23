#pragma once
// What the lab draws: objects with metal-roughness materials (glTF's model), a sun and a sky.
//
// Uniforms Scene::draw sets for each object, for shaders to declare (rimgui_material.glsl declares them all):
//   u_model, u_normal_matrix                          transforms
//   u_albedo, u_alpha, u_roughness, u_metallic,        factors, multiplied with the maps
//   u_emissive, u_normal_scale, u_occlusion_strength
//   u_alpha_mode (0 opaque, 1 mask, 2 blend), u_alpha_cutoff
//   u_maps                                             which maps there are: bits of MaterialMaps
//   u_albedo_map, u_normal_map, u_metal_rough_map,     texture units 7 to 11 (techniques keep 0 to 6 for their own;
//                                                      the environment has 12 to 15)
//   u_occlusion_map, u_emissive_map
#include <rimgui/rt/math.h>
#include <rimgui/rt/mesh.h>
#include <rimgui/rt/shader.h>
#include <rimgui/rt/texture.h>

#include <memory>
#include <string>
#include <vector>

namespace rimgui::rt {

enum MaterialMaps : int {
	map_albedo = 1,
	map_normal = 2,
	map_metal_rough = 4, // Roughness in green, metallic in blue, as glTF packs them.
	map_occlusion = 8,   // In red.
	map_emissive = 16,
};

enum class AlphaMode : int { Opaque = 0, Mask = 1, Blend = 2 };

// The first texture unit materials use; units below it are free for techniques.
inline constexpr int material_texture_unit = 7;

struct Material {
	vec3 albedo{0.8f, 0.8f, 0.8f};
	float alpha = 1;
	float roughness = 0.5f, metallic = 0;
	vec3 emissive{0, 0, 0};
	float normal_scale = 1, occlusion_strength = 1;
	AlphaMode alpha_mode = AlphaMode::Opaque;
	float alpha_cutoff = 0.5f;
	bool double_sided = false;
	std::shared_ptr<Texture> albedo_map, normal_map, metal_rough_map, occlusion_map, emissive_map;
	// Maps can be switched off one by one in the Scene panel, to see what each does.
	int maps_enabled = 31;
	int maps() const;
};

struct Object {
	std::string name;
	std::shared_ptr<Mesh> mesh;
	vec3 position{0, 0, 0}, rotation_degrees{0, 0, 0}, scale{1, 1, 1};
	// Applied before position, rotation and scale: a model file's own node transforms.
	mat4 base;
	Material material;
	bool visible = true;
	// The file it came from, if any, so a saved lab can load it again.
	std::string source;
	mat4 model() const;
};

struct Sun {
	float azimuth_degrees = 35, elevation_degrees = 45;
	vec3 color{1, 0.96f, 0.9f};
	float intensity = 3;
	vec3 sky{0.25f, 0.45f, 0.85f}, horizon{0.62f, 0.68f, 0.78f}, ground{0.22f, 0.19f, 0.16f};
	float ambient = 0.25f; // How strongly the sky (or the environment map) lights what the sun does not.
	vec3 direction() const; // Towards the sun, unit length.
};

// Which objects a draw call takes.
enum class DrawFilter {
	All,         // Opaque and masked first, then blended ones back to front with alpha blending on.
	Opaque,      // Opaque and masked only: for G-buffers, depth and shadow passes.
	Transparent, // Blended only, back to front, blending left to the caller.
};

struct Scene {
	std::vector<Object> objects;
	Sun sun;
	// Draws the visible objects with `program`, which must be in use, setting each one's transform and material
	// uniforms and binding its maps. `camera_position` orders blended objects; culling follows double_sided.
	void draw(Program &program, DrawFilter filter = DrawFilter::All, vec3 camera_position = {0, 0, 0}) const;
	// A sphere round every visible object.
	void bounds(vec3 &centre, float &radius) const;
	// Sets the sun and sky uniforms: u_sun_direction, u_sun_color (colour times intensity), u_sky, u_horizon, u_ground,
	// u_ambient.
	void set_lighting(Program &program) const;
	// A number that changes whenever anything that changes the picture does: for restarting accumulation.
	std::size_t fingerprint() const;
};

} // namespace rimgui::rt
