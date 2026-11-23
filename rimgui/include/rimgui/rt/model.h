#pragma once
// Models from files, as scene objects: glTF 2.0 (.gltf with its files, or .glb) with metal-roughness materials and
// their textures, and Wavefront OBJ (.obj, geometry only).
#include <rimgui/rt/scene.h>

#include <filesystem>
#include <string>
#include <vector>

namespace rimgui::rt {

struct Model {
	// One object per mesh primitive, each with its node's transform in Object::base and the file's material.
	std::vector<Object> parts;
	vec3 bounds_min{0, 0, 0}, bounds_max{0, 0, 0}; // Of every part, transformed.
	std::vector<std::string> warnings;             // What was skipped: an unsupported primitive, a missing image.
};

// Empty parts and `error` set when the file cannot be read.
Model load_model(const std::filesystem::path &path, std::string *error = nullptr);

// The file types load_model reads, by extension (".gltf", ".glb", ".obj").
bool is_model_file(const std::filesystem::path &path);

} // namespace rimgui::rt
