#include <rimgui/rt/mesh.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

namespace rimgui::rt {

void MeshData::compute_bounds()
{
	if (vertices.empty()) {
		bounds_min = bounds_max = {0, 0, 0};
		return;
	}
	bounds_min = bounds_max = vertices.front().position;
	for (const auto &v : vertices) {
		bounds_min = {std::min(bounds_min.x, v.position.x), std::min(bounds_min.y, v.position.y), std::min(bounds_min.z, v.position.z)};
		bounds_max = {std::max(bounds_max.x, v.position.x), std::max(bounds_max.y, v.position.y), std::max(bounds_max.z, v.position.z)};
	}
}

MeshData make_sphere(float radius, int segments, int rings)
{
	MeshData d;
	for (int r = 0; r <= rings; ++r) {
		const float v = float(r) / float(rings), theta = v * pi;
		for (int s = 0; s <= segments; ++s) {
			const float u = float(s) / float(segments), phi = u * 2 * pi;
			const vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
			d.vertices.push_back({n * radius, n, {u, v}});
		}
	}
	const auto at = [&](int r, int s) { return unsigned(r * (segments + 1) + s); };
	for (int r = 0; r < rings; ++r) {
		for (int s = 0; s < segments; ++s) {
			d.indices.insert(d.indices.end(), {at(r, s), at(r, s + 1), at(r + 1, s), at(r, s + 1), at(r + 1, s + 1), at(r + 1, s)});
		}
	}
	d.compute_bounds();
	return d;
}

MeshData make_cube(float half)
{
	MeshData d;
	const vec3 normals[6]{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	for (const vec3 n : normals) {
		const vec3 up = std::fabs(n.y) > 0.5f ? vec3{0, 0, 1} : vec3{0, 1, 0};
		const vec3 right = cross(up, n);
		const auto base = unsigned(d.vertices.size());
		const vec2 uvs[4]{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
		const float sx[4]{-1, 1, 1, -1}, sy[4]{-1, -1, 1, 1};
		for (int i = 0; i < 4; ++i) {
			d.vertices.push_back({(n + right * sx[i] + up * sy[i]) * half, n, uvs[i]});
		}
		d.indices.insert(d.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	}
	d.compute_bounds();
	return d;
}

MeshData make_plane(float half, float uv_repeat)
{
	MeshData d;
	d.vertices = {{{-half, 0, -half}, {0, 1, 0}, {0, 0}},
		{{-half, 0, half}, {0, 1, 0}, {0, uv_repeat}},
		{{half, 0, half}, {0, 1, 0}, {uv_repeat, uv_repeat}},
		{{half, 0, -half}, {0, 1, 0}, {uv_repeat, 0}}};
	d.indices = {0, 1, 2, 0, 2, 3};
	d.compute_bounds();
	return d;
}

MeshData make_torus(float major, float minor, int segments, int sides)
{
	MeshData d;
	for (int i = 0; i <= segments; ++i) {
		const float u = float(i) / float(segments) * 2 * pi;
		const vec3 centre{std::cos(u) * major, 0, std::sin(u) * major};
		for (int j = 0; j <= sides; ++j) {
			const float v = float(j) / float(sides) * 2 * pi;
			const vec3 n{std::cos(u) * std::cos(v), std::sin(v), std::sin(u) * std::cos(v)};
			d.vertices.push_back({centre + n * minor, n, {float(i) / float(segments), float(j) / float(sides)}});
		}
	}
	const auto at = [&](int i, int j) { return unsigned(i * (sides + 1) + j); };
	for (int i = 0; i < segments; ++i) {
		for (int j = 0; j < sides; ++j) {
			d.indices.insert(d.indices.end(), {at(i, j), at(i, j + 1), at(i + 1, j), at(i, j + 1), at(i + 1, j + 1), at(i + 1, j)});
		}
	}
	d.compute_bounds();
	return d;
}

MeshData load_obj(const std::filesystem::path &path, std::string *error)
{
	MeshData d;
	std::ifstream in(path);
	if (!in) {
		if (error) {
			*error = path.string() + ": cannot be read";
		}
		return d;
	}
	std::vector<vec3> positions, normals;
	std::vector<vec2> uvs;
	std::map<std::tuple<int, int, int>, unsigned> made;
	bool missing_normals = false;
	std::string line;
	const auto index = [](const std::string &text, std::size_t count) {
		if (text.empty()) {
			return -1;
		}
		const int i = std::atoi(text.c_str());
		return i > 0 ? i - 1 : int(count) + i; // Negative indices count back from the end.
	};
	while (std::getline(in, line)) {
		std::istringstream s(line);
		std::string tag;
		s >> tag;
		if (tag == "v") {
			vec3 p;
			s >> p.x >> p.y >> p.z;
			positions.push_back(p);
		} else if (tag == "vn") {
			vec3 n;
			s >> n.x >> n.y >> n.z;
			normals.push_back(n);
		} else if (tag == "vt") {
			vec2 t;
			s >> t.x >> t.y;
			t.y = 1 - t.y; // OBJ counts v from the bottom of the image; textures here start at the top.
			uvs.push_back(t);
		} else if (tag == "f") {
			std::vector<unsigned> polygon;
			std::string corner;
			while (s >> corner) {
				std::string parts[3];
				std::size_t k = 0;
				for (const char c : corner) {
					if (c == '/') {
						k = std::min<std::size_t>(k + 1, 2);
					} else {
						parts[k] += c;
					}
				}
				const int p = index(parts[0], positions.size()), t = index(parts[1], uvs.size()), n = index(parts[2], normals.size());
				if (p < 0 || p >= int(positions.size())) {
					continue;
				}
				const auto key = std::make_tuple(p, t, n);
				auto found = made.find(key);
				if (found == made.end()) {
					Vertex v;
					v.position = positions[std::size_t(p)];
					if (t >= 0 && t < int(uvs.size())) {
						v.uv = uvs[std::size_t(t)];
					}
					if (n >= 0 && n < int(normals.size())) {
						v.normal = normals[std::size_t(n)];
					} else {
						missing_normals = true;
					}
					found = made.emplace(key, unsigned(d.vertices.size())).first;
					d.vertices.push_back(v);
				}
				polygon.push_back(found->second);
			}
			for (std::size_t i = 2; i < polygon.size(); ++i) {
				d.indices.insert(d.indices.end(), {polygon[0], polygon[i - 1], polygon[i]});
			}
		}
	}
	if (missing_normals) {
		// Smooth normals: each vertex the area-weighted sum of its triangles' normals, by position.
		std::map<std::tuple<float, float, float>, vec3> sums;
		for (std::size_t i = 0; i + 2 < d.indices.size(); i += 3) {
			const vec3 a = d.vertices[d.indices[i]].position, b = d.vertices[d.indices[i + 1]].position, c = d.vertices[d.indices[i + 2]].position;
			const vec3 n = cross(b - a, c - a);
			for (std::size_t k = 0; k < 3; ++k) {
				const vec3 p = d.vertices[d.indices[i + k]].position;
				sums[{p.x, p.y, p.z}] += n;
			}
		}
		for (auto &v : d.vertices) {
			if (length(v.normal) < 1e-6f) {
				v.normal = normalize(sums[{v.position.x, v.position.y, v.position.z}]);
			}
		}
	}
	if (d.indices.empty() && error) {
		*error = path.string() + ": no triangles";
	}
	d.compute_bounds();
	return d;
}

Mesh::Mesh(const MeshData &data, std::string name) : min_(data.bounds_min), max_(data.bounds_max), name_(std::move(name))
{
	glGenVertexArrays(1, &vao_);
	glGenBuffers(1, &vbo_);
	glGenBuffers(1, &ebo_);
	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(data.vertices.size() * sizeof(Vertex)), data.vertices.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(data.indices.size() * sizeof(unsigned)), data.indices.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, position)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, normal)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, uv)));
	glBindVertexArray(0);
	count_ = GLsizei(data.indices.size());
}

Mesh::~Mesh()
{
	glDeleteBuffers(1, &ebo_);
	glDeleteBuffers(1, &vbo_);
	glDeleteVertexArrays(1, &vao_);
}

void Mesh::draw() const
{
	glBindVertexArray(vao_);
	glDrawElements(GL_TRIANGLES, count_, GL_UNSIGNED_INT, nullptr);
	glBindVertexArray(0);
}

void draw_fullscreen_triangle()
{
	static GLuint empty = 0;
	if (!empty) {
		glGenVertexArrays(1, &empty);
	}
	glBindVertexArray(empty);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
}

} // namespace rimgui::rt
