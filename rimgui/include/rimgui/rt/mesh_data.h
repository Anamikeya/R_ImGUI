#pragma once
// Triangle meshes on the CPU: test shapes, and Wavefront OBJ files. Part of rimgui_rt_core (no graphics API); each kit
// uploads them (rimgui/rt/mesh.h for OpenGL).
#include <rimgui/rt/math.h>

#include <filesystem>
#include <string>
#include <vector>

namespace rimgui::rt {

struct Vertex {
	vec3 position, normal;
	vec2 uv;
};

struct MeshData {
	std::vector<Vertex> vertices;
	std::vector<unsigned> indices; // Three to a triangle, counter-clockwise from outside.
	vec3 bounds_min{0, 0, 0}, bounds_max{0, 0, 0};
	void compute_bounds();
};

MeshData make_sphere(float radius = 0.5f, int segments = 64, int rings = 32);
MeshData make_cube(float half = 0.5f);
MeshData make_plane(float half = 5, float uv_repeat = 5); // Facing +y.
MeshData make_torus(float major = 0.5f, float minor = 0.18f, int segments = 64, int sides = 32);
// An OBJ file: positions, normals and texture coordinates; polygons become triangles, and smooth normals are made
// where the file has none. Empty data and `error` set when it cannot be read.
MeshData load_obj(const std::filesystem::path &path, std::string *error = nullptr);

} // namespace rimgui::rt
