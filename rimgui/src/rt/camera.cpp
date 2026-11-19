#include <rimgui/rt/camera.h>

#include <algorithm>

namespace rimgui::rt {

mat4 inverse(const mat4 &a)
{
	const float *m = a.m;
	float inv[16];
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	mat4 r;
	if (std::fabs(det) < 1e-20f) {
		return r;
	}
	for (int i = 0; i < 16; ++i) {
		r.m[i] = inv[i] / det;
	}
	return r;
}

vec3 OrbitCamera::forward() const
{
	return normalize({-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch)});
}

vec3 OrbitCamera::position() const
{
	return target - forward() * distance;
}

mat4 OrbitCamera::view() const
{
	return look_at(position(), target, {0, 1, 0});
}

mat4 OrbitCamera::projection(float aspect) const
{
	return perspective(vertical_fov, std::max(aspect, 1e-3f), near_plane, far_plane);
}

void OrbitCamera::frame(vec3 centre, float radius)
{
	target = centre;
	distance = std::max(radius, 0.01f) / std::sin(vertical_fov / 2) * 1.1f;
}

bool OrbitCamera::update(bool hovered, ImVec2 size, float seconds)
{
	const ImGuiIO &io = ImGui::GetIO();
	const bool any_down = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
						  ImGui::IsMouseDown(ImGuiMouseButton_Right);
	if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
					   ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
		dragging_ = true;
	}
	if (!any_down) {
		dragging_ = false;
	}
	const bool active = dragging_ || hovered;
	if (!active) {
		return false;
	}
	bool moved = false;
	const ImVec2 d = io.MouseDelta;
	const float height = std::max(size.y, 1.0f);
	if (dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && (d.x != 0 || d.y != 0)) {
		yaw -= d.x * 0.008f;
		pitch = std::clamp(pitch + d.y * 0.008f, -1.55f, 1.55f);
		moved = true;
	}
	if (dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Middle) && (d.x != 0 || d.y != 0)) {
		const vec3 f = forward(), right = normalize(cross(f, {0, 1, 0})), up = cross(right, f);
		const float per_pixel = 2 * distance * std::tan(vertical_fov / 2) / height;
		target += right * (-d.x * per_pixel) + up * (d.y * per_pixel);
		moved = true;
	}
	if (hovered && io.MouseWheel != 0) {
		distance = std::clamp(distance * std::pow(0.88f, io.MouseWheel), near_plane * 2, far_plane * 0.5f);
		moved = true;
	}
	if (dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		// Fly: turn about the eye, and the keys move it.
		const vec3 eye = position();
		if (d.x != 0 || d.y != 0) {
			yaw -= d.x * 0.005f;
			pitch = std::clamp(pitch + d.y * 0.005f, -1.55f, 1.55f);
			moved = true;
		}
		const vec3 f = forward(), right = normalize(cross(f, {0, 1, 0}));
		vec3 step{0, 0, 0};
		if (ImGui::IsKeyDown(ImGuiKey_W)) step += f;
		if (ImGui::IsKeyDown(ImGuiKey_S)) step += -f;
		if (ImGui::IsKeyDown(ImGuiKey_D)) step += right;
		if (ImGui::IsKeyDown(ImGuiKey_A)) step += -right;
		if (ImGui::IsKeyDown(ImGuiKey_E)) step += vec3{0, 1, 0};
		if (ImGui::IsKeyDown(ImGuiKey_Q)) step += vec3{0, -1, 0};
		const float speed = fly_speed * (io.KeyShift ? 4.0f : 1.0f) * seconds;
		const vec3 new_eye = eye + normalize(step) * speed;
		target = new_eye + forward() * distance;
		moved = moved || length(step) > 0;
	}
	return moved;
}

} // namespace rimgui::rt
