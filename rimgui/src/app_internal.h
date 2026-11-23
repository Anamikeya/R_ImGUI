#pragma once
// The application shell's parts that depend on the graphics API: one Renderer per API, chosen by run().
#include <rimgui/app.h>

#include <imgui.h>

#include <memory>
#include <string>

struct GLFWwindow;

namespace rimgui::detail {

class Renderer {
public:
	virtual ~Renderer() = default;
	// Window hints before the window is made; called again with `fallback` when the first window could not be made.
	virtual void window_hints(const AppConfig &config, bool fallback) = 0;
	// After the window and ImGui's context exist: the API's objects and ImGui's renderer backend. Empty on success.
	virtual std::string start(GLFWwindow *window, const AppConfig &config, bool hidden, bool validation) = 0;
	// Before ImGui::NewFrame. False skips this frame (a swapchain being made again).
	virtual bool begin_frame() = 0;
	// After ImGui::Render: draws ImGui's lists to the window and shows them; with `screenshot` set, also saves the
	// window as a binary PPM first. False when the screenshot could not be written.
	virtual bool end_frame(const ImVec4 &clear_color, const std::string &screenshot) = 0;
	// After the last frame, while ImGui's context still exists, before on_exit: waits for the GPU.
	virtual void idle() = 0;
	// After on_exit: ImGui's renderer backend and the API's objects.
	virtual void stop() = 0;
};

std::unique_ptr<Renderer> make_opengl_renderer();
#ifdef RIMGUI_HAS_VULKAN
std::unique_ptr<Renderer> make_vulkan_renderer();
#endif

// Writes RGB rows, top row first, as a binary PPM.
bool write_ppm(const std::string &path, int width, int height, const unsigned char *rgb_rows_top_first);

} // namespace rimgui::detail
