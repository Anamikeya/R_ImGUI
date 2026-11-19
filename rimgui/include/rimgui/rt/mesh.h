#pragma once
// Triangle meshes on the GPU: test shapes, and models from Wavefront OBJ files.
// Vertex attributes, as shaders declare them:
//   layout(location = 0) in vec3 a_position;
//   layout(location = 1) in vec3 a_normal;
//   layout(location = 2) in vec2 a_uv;
#include <rimgui/rt/gl.h>
#include <rimgui/rt/math.h>

#include <filesystem>
#include <memory>
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

class Mesh {
public:
	explicit Mesh(const MeshData &data, std::string name = {});
	~Mesh();
	Mesh(const Mesh &) = delete;
	Mesh &operator=(const Mesh &) = delete;

	void draw() const;
	const std::string &name() const { return name_; }
	vec3 bounds_min() const { return min_; }
	vec3 bounds_max() const { return max_; }
	std::size_t triangles() const { return count_ / 3; }

private:
	GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
	GLsizei count_ = 0;
	vec3 min_, max_;
	std::string name_;
};

// A triangle that covers the screen, for full-screen passes: draw it with any program and no attributes;
// gl_VertexID gives the corner (see shaders/fullscreen.vert).
void draw_fullscreen_triangle();

} // namespace rimgui::rt
