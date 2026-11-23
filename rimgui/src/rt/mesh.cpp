#include <rimgui/rt/mesh.h>

#include <cstddef>

namespace rimgui::rt {

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
