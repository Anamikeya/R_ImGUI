#pragma once
// Models from files on the GPU (OpenGL), as scene objects: read_model's data (rimgui/rt/model_data.h) uploaded, each
// mesh and image once.
#include <rimgui/rt/model_data.h>
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



} // namespace rimgui::rt
