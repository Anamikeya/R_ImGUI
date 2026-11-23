#include <rimgui/rt/scene_core.h>

#include <cmath>

namespace rimgui::rt {

vec3 Sun::direction() const
{
	const float a = azimuth_degrees * pi / 180, e = elevation_degrees * pi / 180;
	return normalize({std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)});
}

mat4 object_transform(vec3 position, vec3 rotation_degrees, vec3 size, const mat4 &base)
{
	const float d = pi / 180;
	return translate(position) * rotate(rotation_degrees.y * d, {0, 1, 0}) * rotate(rotation_degrees.x * d, {1, 0, 0}) *
		   rotate(rotation_degrees.z * d, {0, 0, 1}) * scale(size) * base;
}

} // namespace rimgui::rt
