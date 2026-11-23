#include "app_internal.h"

#include <rimgui/app.h>
#include <rimgui/theme.h>

#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#include <windows.h>
#endif
#include <GL/gl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace rimgui {

namespace {

bool quit_requested = false;
int requested_exit_code = 0;
const AppConfig *running_config = nullptr;
bool layout_reset = false;
Backend running_backend = Backend::OpenGL;

void dock_space(const AppConfig &config)
{
	const ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
	ImGuiDockNode *node = ImGui::DockBuilderGetNode(dock);
	if (layout_reset) {
		layout_reset = false;
		ImGui::DockBuilderRemoveNode(dock);
		ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
		node = ImGui::DockBuilderGetNode(dock);
	}
	if (!config.first_layout || !node || node->IsSplitNode() || !node->Windows.empty()) {
		return;
	}
	DockAreas a;
	a.center = dock;
	ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->Size);
	// The toolbar strip is as tall as its rows of buttons, whatever the window's height.
	const float rows = static_cast<float>(std::max(config.toolbar_rows, 1));
	const float strip = rows * ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2;
	ImGui::DockBuilderSplitNode(a.center, ImGuiDir_Up, std::clamp(strip / ImGui::GetMainViewport()->Size.y, 0.02f, 0.3f), &a.top, &a.center);
	ImGui::DockBuilderSplitNode(a.center, ImGuiDir_Left, 0.19f, &a.left, &a.center);
	ImGui::DockBuilderSplitNode(a.center, ImGuiDir_Right, 0.30f, &a.right, &a.center);
	ImGui::DockBuilderSplitNode(a.center, ImGuiDir_Down, 0.30f, &a.bottom, &a.center);
	ImGui::DockBuilderSplitNode(a.bottom, ImGuiDir_Right, 0.40f, &a.bottom_right, &a.bottom);
	config.first_layout(a);
	ImGui::DockBuilderFinish(dock);
}

// ---- OpenGL --------------------------------------------------------------------------------------------------------

class OpenGLRenderer final : public detail::Renderer {
public:
	void window_hints(const AppConfig &config, bool fallback) override
	{
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, fallback ? 3 : config.gl_major);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, fallback ? 3 : config.gl_minor);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	}
	std::string start(GLFWwindow *window, const AppConfig &, bool hidden, bool) override
	{
		window_ = window;
		glfwMakeContextCurrent(window);
		glfwSwapInterval(hidden ? 0 : 1);
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 330");
		return {};
	}
	bool begin_frame() override
	{
		ImGui_ImplOpenGL3_NewFrame();
		return true;
	}
	bool end_frame(const ImVec4 &clear, const std::string &screenshot) override
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(window_, &width, &height);
		glViewport(0, 0, width, height);
		glClearColor(clear.x, clear.y, clear.z, clear.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		bool ok = true;
		if (!screenshot.empty()) {
			std::vector<unsigned char> pixels(std::size_t(width) * std::size_t(height) * 3), flipped(pixels.size());
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
			const std::size_t row = std::size_t(width) * 3;
			for (int y = 0; y < height; ++y) {
				std::memcpy(flipped.data() + std::size_t(y) * row, pixels.data() + std::size_t(height - 1 - y) * row, row);
			}
			ok = detail::write_ppm(screenshot, width, height, flipped.data());
		}
		glfwSwapBuffers(window_);
		return ok;
	}
	void idle() override {}
	void stop() override
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
	}

private:
	GLFWwindow *window_ = nullptr;
};

} // namespace

namespace detail {

std::unique_ptr<Renderer> make_opengl_renderer()
{
	return std::make_unique<OpenGLRenderer>();
}

bool write_ppm(const std::string &path, int width, int height, const unsigned char *rgb)
{
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return false;
	}
	out << "P6\n" << width << ' ' << height << "\n255\n";
	out.write(reinterpret_cast<const char *>(rgb), std::streamsize(std::size_t(width) * std::size_t(height) * 3));
	return static_cast<bool>(out);
}

} // namespace detail

void quit(int exit_code)
{
	quit_requested = true;
	requested_exit_code = exit_code;
}

void reset_layout()
{
	layout_reset = true;
}

Backend backend()
{
	return running_backend;
}

int run(const AppConfig &config, const std::function<void()> &frame, int argc, char **argv)
{
	std::string screenshot;
	int screenshot_frame = 60;
	bool hidden = false, validation = config.vulkan_validation;
	Backend chosen = config.backend;
	for (int i = 1; i < argc; ++i) {
		const std::string_view arg = argv[i];
		if (arg == "--hidden") hidden = true;
		else if (arg == "--vulkan") chosen = Backend::Vulkan;
		else if (arg == "--opengl") chosen = Backend::OpenGL;
		else if (arg == "--validation") validation = true;
		else if (arg == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
		else if (arg == "--screenshot-frame" && i + 1 < argc) screenshot_frame = std::max(1, std::atoi(argv[++i]));
	}

	std::unique_ptr<detail::Renderer> renderer;
	if (chosen == Backend::Vulkan) {
#ifdef RIMGUI_HAS_VULKAN
		renderer = detail::make_vulkan_renderer();
#else
		std::cerr << "This program was built without the Vulkan SDK, so it cannot draw with Vulkan.\n";
		return 1;
#endif
	} else {
		renderer = detail::make_opengl_renderer();
	}
	running_backend = chosen;

	glfwSetErrorCallback([](int code, const char *message) { std::cerr << "GLFW " << code << ": " << message << '\n'; });
	if (!glfwInit()) {
		std::cerr << "The window system could not start.\n";
		return 1;
	}
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	// A screenshot run does not take the keyboard from whatever has it.
	glfwWindowHint(GLFW_FOCUS_ON_SHOW, screenshot.empty() && !hidden ? GLFW_TRUE : GLFW_FALSE);

	// The size asked for at the monitor's scale, kept within its work area, centred on it.
	GLFWmonitor *monitor = glfwGetPrimaryMonitor();
	float scale = 1, unused = 1;
	int area_x = 0, area_y = 0, area_width = config.width, area_height = config.height;
	if (monitor) {
		glfwGetMonitorContentScale(monitor, &scale, &unused);
		glfwGetMonitorWorkarea(monitor, &area_x, &area_y, &area_width, &area_height);
	}
	const float fit = std::min({std::max(scale, 1.0f), 0.9f * float(area_width) / float(config.width), 0.9f * float(area_height) / float(config.height)});
	const int width = std::max(int(float(config.width) * fit), 640), height = std::max(int(float(config.height) * fit), 400);
	renderer->window_hints(config, false);
	GLFWwindow *window = glfwCreateWindow(width, height, config.title.c_str(), nullptr, nullptr);
	if (!window) {
		renderer->window_hints(config, true);
		window = glfwCreateWindow(width, height, config.title.c_str(), nullptr, nullptr);
	}
	if (!window) {
		std::cerr << (chosen == Backend::Vulkan ? "The window could not be made.\n" : "The window could not be made (OpenGL 3.3 is needed).\n");
		glfwTerminate();
		return 1;
	}
	if (monitor) {
		glfwSetWindowPos(window, area_x + std::max(area_width - width, 0) / 2, area_y + std::max(area_height - height, 0) / 2);
	}
	running_config = &config;
	glfwSetDropCallback(window, [](GLFWwindow *, int count, const char **paths) {
		if (running_config && running_config->on_drop) {
			running_config->on_drop(std::vector<std::string>(paths, paths + count));
		}
	});

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	io.ConfigDragClickToInputText = true; // Number fields: drag to scrub, click to type.
	io.IniFilename = config.layout_file.empty() || !screenshot.empty() || hidden ? nullptr : config.layout_file.c_str();

	apply_theme(ImGui::GetStyle());
	// The font atlas is built once, for the scale the window opens at; another monitor scales it.
	const float font_scale = std::max(scale, 1.0f);
	load_fonts(base_font_size * font_scale);
	float applied_scale = 0;

	const std::string problem = renderer->start(window, config, hidden, validation);
	if (!problem.empty()) {
		std::cerr << problem << '\n';
		renderer->stop();
		ImGui::DestroyContext();
		running_config = nullptr;
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}
	if (!hidden) {
		glfwShowWindow(window);
	}

	if (config.on_start) {
		config.on_start();
	}
	quit_requested = false;
	requested_exit_code = 0;
	int frames = 0;
	int exit_code = 0;
	while (!glfwWindowShouldClose(window) && !quit_requested) {
		glfwPollEvents();
		if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
			glfwWaitEventsTimeout(0.1);
			continue;
		}
		float window_scale = 1;
		glfwGetWindowContentScale(window, &window_scale, &unused);
		if (window_scale > 0 && window_scale != applied_scale) {
			scale_style(window_scale, font_scale);
			applied_scale = window_scale;
		}
		if (!renderer->begin_frame()) {
			continue;
		}
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		dock_space(config);
		frame();
		ImGui::Render();
		const bool capture = !screenshot.empty() && ++frames == screenshot_frame;
		if (!renderer->end_frame(config.clear_color, capture ? screenshot : std::string())) {
			std::cerr << "Could not write " << screenshot << '\n';
			exit_code = 1;
		}
		if (capture) {
			quit_requested = true;
		}
	}

	renderer->idle();
	if (config.on_exit) {
		config.on_exit();
	}
	renderer->stop();
	ImGui::DestroyContext();
	running_config = nullptr;
	glfwDestroyWindow(window);
	glfwTerminate();
	return exit_code != 0 ? exit_code : requested_exit_code;
}

} // namespace rimgui
