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

bool save_ppm(const std::string &path, int width, int height)
{
	std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		return false;
	}
	out << "P6\n" << width << ' ' << height << "\n255\n";
	const std::size_t row = static_cast<std::size_t>(width) * 3;
	for (int y = height - 1; y >= 0; --y) {
		out.write(reinterpret_cast<const char *>(pixels.data() + static_cast<std::size_t>(y) * row), static_cast<std::streamsize>(row));
	}
	return static_cast<bool>(out);
}

} // namespace

void quit(int exit_code)
{
	quit_requested = true;
	requested_exit_code = exit_code;
}

void reset_layout()
{
	layout_reset = true;
}

int run(const AppConfig &config, const std::function<void()> &frame, int argc, char **argv)
{
	std::string screenshot;
	int screenshot_frame = 60;
	bool hidden = false;
	for (int i = 1; i < argc; ++i) {
		if (std::string_view(argv[i]) == "--hidden") {
			hidden = true;
		}
		if (std::string_view(argv[i]) == "--screenshot" && i + 1 < argc) {
			screenshot = argv[++i];
		} else if (std::string_view(argv[i]) == "--screenshot-frame" && i + 1 < argc) {
			screenshot_frame = std::max(1, std::atoi(argv[++i]));
		}
	}

	glfwSetErrorCallback([](int code, const char *message) { std::cerr << "GLFW " << code << ": " << message << '\n'; });
	if (!glfwInit()) {
		std::cerr << "The window system could not start.\n";
		return 1;
	}
	const auto context = [](int major, int minor) {
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	};
	context(config.gl_major, config.gl_minor);
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
	GLFWwindow *window = glfwCreateWindow(width, height, config.title.c_str(), nullptr, nullptr);
	if (!window && (config.gl_major != 3 || config.gl_minor != 3)) {
		context(3, 3);
		window = glfwCreateWindow(width, height, config.title.c_str(), nullptr, nullptr);
	}
	if (!window) {
		std::cerr << "The window could not be made (OpenGL 3.3 is needed).\n";
		glfwTerminate();
		return 1;
	}
	if (monitor) {
		glfwSetWindowPos(window, area_x + std::max(area_width - width, 0) / 2, area_y + std::max(area_height - height, 0) / 2);
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(hidden ? 0 : 1);
	if (!hidden) {
		glfwShowWindow(window);
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

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");

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
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		dock_space(config);
		frame();
		ImGui::Render();
		int fb_width = 0, fb_height = 0;
		glfwGetFramebufferSize(window, &fb_width, &fb_height);
		glViewport(0, 0, fb_width, fb_height);
		glClearColor(config.clear_color.x, config.clear_color.y, config.clear_color.z, config.clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		if (!screenshot.empty() && ++frames == screenshot_frame) {
			if (!save_ppm(screenshot, fb_width, fb_height)) {
				std::cerr << "Could not write " << screenshot << '\n';
				exit_code = 1;
			}
			quit_requested = true;
		}
		glfwSwapBuffers(window);
	}

	if (config.on_exit) {
		config.on_exit();
	}
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	running_config = nullptr;
	glfwDestroyWindow(window);
	glfwTerminate();
	return exit_code != 0 ? exit_code : requested_exit_code;
}

} // namespace rimgui
