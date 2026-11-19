#pragma once
// A camera for looking at what a renderer draws, with the editor's controls:
//   left drag (or Alt + left drag)  orbit round the target
//   middle drag                     pan
//   wheel                           zoom
//   right drag + W A S D Q E        fly; Shift is faster
//   F                               frame the scene's bounds again
#include <rimgui/rt/math.h>

#include <imgui.h>

namespace rimgui::rt {

struct OrbitCamera {
	vec3 target{0, 0.5f, 0};
	float distance = 4;
	float yaw = 0.6f;    // About the up axis, radians; 0 looks along -z.
	float pitch = 0.35f; // Up from the horizon, radians.
	float vertical_fov = 50 * pi / 180;
	float near_plane = 0.05f, far_plane = 200;
	float fly_speed = 3; // Metres a second.

	vec3 position() const;
	vec3 forward() const;
	mat4 view() const;
	mat4 projection(float aspect) const;
	// Frames a sphere: the target at its centre, far enough back to see all of it.
	void frame(vec3 centre, float radius);
	// Mouse and keys over a view of `size` that is `hovered` (or being dragged). True when the camera moved.
	bool update(bool hovered, ImVec2 size, float seconds);

private:
	bool dragging_ = false;
};

} // namespace rimgui::rt
