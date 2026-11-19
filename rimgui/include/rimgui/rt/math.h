#pragma once
// Small vector and matrix types for the research kit, laid out as GLSL expects: mat4 is column-major, and data()
// goes straight to glUniformMatrix4fv. Clip space is OpenGL's (-1 to 1 in z).
#include <cmath>

namespace rimgui::rt {

inline constexpr float pi = 3.14159265358979f;

struct vec2 {
	float x = 0, y = 0;
};

struct vec3 {
	float x = 0, y = 0, z = 0;
	constexpr vec3() = default;
	constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
	const float *data() const { return &x; }
	float *data() { return &x; }
};
inline vec3 operator+(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline vec3 operator-(vec3 a, vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline vec3 operator-(vec3 a) { return {-a.x, -a.y, -a.z}; }
inline vec3 operator*(vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline vec3 operator*(float s, vec3 a) { return a * s; }
inline vec3 operator*(vec3 a, vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline vec3 &operator+=(vec3 &a, vec3 b) { return a = a + b; }
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(vec3 a) { return std::sqrt(dot(a, a)); }
inline vec3 normalize(vec3 a)
{
	const float l = length(a);
	return l > 1e-12f ? a * (1 / l) : vec3{0, 0, 0};
}

struct vec4 {
	float x = 0, y = 0, z = 0, w = 0;
	const float *data() const { return &x; }
};

struct mat4 {
	float m[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // m[column * 4 + row]
	const float *data() const { return m; }
	float &at(int row, int column) { return m[column * 4 + row]; }
	float at(int row, int column) const { return m[column * 4 + row]; }
};

inline mat4 operator*(const mat4 &a, const mat4 &b)
{
	mat4 r;
	for (int c = 0; c < 4; ++c) {
		for (int row = 0; row < 4; ++row) {
			float s = 0;
			for (int k = 0; k < 4; ++k) {
				s += a.at(row, k) * b.at(k, c);
			}
			r.at(row, c) = s;
		}
	}
	return r;
}

inline vec4 operator*(const mat4 &a, vec4 v)
{
	const float in[4]{v.x, v.y, v.z, v.w};
	float out[4]{};
	for (int row = 0; row < 4; ++row) {
		for (int k = 0; k < 4; ++k) {
			out[row] += a.at(row, k) * in[k];
		}
	}
	return {out[0], out[1], out[2], out[3]};
}

inline mat4 translate(vec3 t)
{
	mat4 r;
	r.at(0, 3) = t.x;
	r.at(1, 3) = t.y;
	r.at(2, 3) = t.z;
	return r;
}

inline mat4 scale(vec3 s)
{
	mat4 r;
	r.at(0, 0) = s.x;
	r.at(1, 1) = s.y;
	r.at(2, 2) = s.z;
	return r;
}

// Turned by `radians` about the unit `axis`, right-handed.
inline mat4 rotate(float radians, vec3 axis)
{
	const vec3 a = normalize(axis);
	const float c = std::cos(radians), s = std::sin(radians), t = 1 - c;
	mat4 r;
	r.at(0, 0) = t * a.x * a.x + c;
	r.at(0, 1) = t * a.x * a.y - s * a.z;
	r.at(0, 2) = t * a.x * a.z + s * a.y;
	r.at(1, 0) = t * a.x * a.y + s * a.z;
	r.at(1, 1) = t * a.y * a.y + c;
	r.at(1, 2) = t * a.y * a.z - s * a.x;
	r.at(2, 0) = t * a.x * a.z - s * a.y;
	r.at(2, 1) = t * a.y * a.z + s * a.x;
	r.at(2, 2) = t * a.z * a.z + c;
	return r;
}

inline mat4 perspective(float vertical_fov_radians, float aspect, float near_plane, float far_plane)
{
	const float f = 1 / std::tan(vertical_fov_radians / 2);
	mat4 r;
	r.m[0] = f / aspect;
	r.m[5] = f;
	r.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
	r.m[11] = -1;
	r.m[14] = 2 * far_plane * near_plane / (near_plane - far_plane);
	r.m[15] = 0;
	return r;
}

inline mat4 orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane)
{
	mat4 r;
	r.m[0] = 2 / (right - left);
	r.m[5] = 2 / (top - bottom);
	r.m[10] = -2 / (far_plane - near_plane);
	r.m[12] = -(right + left) / (right - left);
	r.m[13] = -(top + bottom) / (top - bottom);
	r.m[14] = -(far_plane + near_plane) / (far_plane - near_plane);
	return r;
}

inline mat4 look_at(vec3 eye, vec3 target, vec3 up)
{
	const vec3 f = normalize(target - eye), s = normalize(cross(f, up)), u = cross(s, f);
	mat4 r;
	r.at(0, 0) = s.x;
	r.at(0, 1) = s.y;
	r.at(0, 2) = s.z;
	r.at(1, 0) = u.x;
	r.at(1, 1) = u.y;
	r.at(1, 2) = u.z;
	r.at(2, 0) = -f.x;
	r.at(2, 1) = -f.y;
	r.at(2, 2) = -f.z;
	r.at(0, 3) = -dot(s, eye);
	r.at(1, 3) = -dot(u, eye);
	r.at(2, 3) = dot(f, eye);
	return r;
}

// The general inverse; the identity for a matrix that has none.
mat4 inverse(const mat4 &a);
inline mat4 transpose(const mat4 &a)
{
	mat4 r;
	for (int row = 0; row < 4; ++row) {
		for (int c = 0; c < 4; ++c) {
			r.at(row, c) = a.at(c, row);
		}
	}
	return r;
}

} // namespace rimgui::rt
