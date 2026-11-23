#pragma once
// Models read from files into CPU data, for any graphics API to upload: glTF 2.0 (.gltf with its files, .glb, embedded
// and data-URI images) with metal-roughness materials, and Wavefront OBJ. Part of rimgui_rt_core (no graphics API).
//
// A file needing what this does not decode (Draco or meshopt compression, WebP or Basis images) is refused by name,
// never read as nothing.
#include <rimgui/rt/image.h>
#include <rimgui/rt/math.h>
#include <rimgui/rt/mesh_data.h>

#include <filesystem>
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

// A material as the file has it; images are shared between the materials that use them.
struct MaterialData {
	vec3 albedo{0.8f, 0.8f, 0.8f};
	float alpha = 1;
	float roughness = 0.5f, metallic = 0;
	vec3 emissive{0, 0, 0};
	float normal_scale = 1, occlusion_strength = 1;
	AlphaMode alpha_mode = AlphaMode::Opaque;
	float alpha_cutoff = 0.5f;
	bool double_sided = false;
	// Albedo and emissive are colour (sRGB); the others are data.
	std::shared_ptr<const Image> albedo_map, normal_map, metal_rough_map, occlusion_map, emissive_map;
};

struct PartData {
	std::string name;
	std::shared_ptr<const MeshData> mesh; // Shared between nodes that use the same primitive.
	MaterialData material;
	mat4 base; // The node's transform in the file.
};

struct ModelData {
	std::vector<PartData> parts;
	vec3 bounds_min{0, 0, 0}, bounds_max{0, 0, 0}; // Of every part, transformed.
	std::vector<std::string> warnings;             // What was skipped: an unsupported primitive, a missing image.
	std::string source;                            // The file's path.
};

// Empty parts and `error` set when the file cannot be read.
ModelData read_model(const std::filesystem::path &path, std::string *error = nullptr);

// The file types read_model reads, by extension (".gltf", ".glb", ".obj").
bool is_model_file(const std::filesystem::path &path);

} // namespace rimgui::rt
