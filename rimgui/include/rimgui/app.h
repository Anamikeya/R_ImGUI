#pragma once
// A desktop application with R_ImGUI's look: a GLFW window, OpenGL 3, Dear ImGui with docking, the theme and fonts at
// the monitor's scale, and a dock space over the whole window. Your code draws its panels each frame.
//
//   int main(int argc, char **argv) {
//       rimgui::AppConfig config;
//       config.title = "My Tool";
//       config.first_layout = [](const rimgui::DockAreas &areas) { ImGui::DockBuilderDockWindow("Files", areas.left); };
//       return rimgui::run(config, [] { ImGui::Begin("Files"); ImGui::End(); }, argc, argv);
//   }
#include <imgui.h>

#include <functional>
#include <string>

namespace rimgui {

// The dock space split as IdlyEngine's editor is: a toolbar strip at the top, a tree on the left, details on the
// right, a log at the bottom and the main view in the middle. Dock windows by title into these.
struct DockAreas {
	ImGuiID top = 0, left = 0, right = 0, bottom = 0, bottom_right = 0, center = 0;
};

struct AppConfig {
	std::string title = "R_ImGUI Application";
	int width = 1440, height = 900;  // At 100% scale; grown with the monitor's scale and kept within its work area.
	std::string layout_file = "imgui.ini"; // Where docking and window positions are kept; empty keeps none.
	int toolbar_rows = 1;            // How tall the docked toolbar strip (DockAreas::top) is first made, in rows.
	// Docks windows the first time (no saved layout yet), or after reset_layout().
	std::function<void(const DockAreas &)> first_layout;
	// Clear colour behind every window.
	ImVec4 clear_color{0.075f, 0.078f, 0.086f, 1};
};

// Opens the window and calls `frame` once a frame, between ImGui::NewFrame and ImGui::Render, until the window is
// closed or quit() is called. Returns the process exit code. Options it reads from the command line:
//   --screenshot <file.ppm>  saves the window after its 60th frame, then quits (for docs and tests)
int run(const AppConfig &config, const std::function<void()> &frame, int argc = 0, char **argv = nullptr);

// Ends the loop after this frame.
void quit();
// Docks the windows again as first_layout says, from the next frame.
void reset_layout();

} // namespace rimgui
