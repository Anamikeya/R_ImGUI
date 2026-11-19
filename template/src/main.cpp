// StarterApp: what a new R_ImGUI application starts as. Replace the panels with your own; keep the shape:
// a menu bar, a toolbar strip, a list on the left, details on the right, a log below and the main view in the middle.
#include <rimgui/app.h>
#include <rimgui/theme.h>
#include <rimgui/widgets.h>

#include <imgui.h>
#include <imgui_internal.h> // DockBuilder, for the first layout.

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Item {
	std::string name, kind;
	bool visible = true;
	float position[3]{0, 0, 0};
	float color[3]{0.53f, 0.78f, 0.96f};
	int quality = 1;
};

struct State {
	std::vector<Item> items{{"Camera", "camera"}, {"Sun", "light"}, {"Floor", "mesh"}, {"Crate 1", "mesh"}, {"Crate 2", "mesh"}};
	int selected = 3;
	bool running = false;
	std::array<char, 64> search{};
	bool show_meshes = false, show_lights = false;
	std::vector<std::string> log{"Started.", "Open a file, or pick an item on the left."};
	float progress = 0.35f;
};

State state;

void menu_bar()
{
	if (!ImGui::BeginMainMenuBar()) {
		return;
	}
	if (ImGui::BeginMenu("File")) {
		ImGui::MenuItem("New", "Ctrl+N");
		ImGui::MenuItem("Open...", "Ctrl+O");
		ImGui::MenuItem("Save", "Ctrl+S");
		ImGui::Separator();
		if (ImGui::MenuItem("Quit", "Alt+F4")) {
			rimgui::quit();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Window")) {
		if (ImGui::MenuItem("Reset layout")) {
			rimgui::reset_layout();
		}
		ImGui::EndMenu();
	}
	ImGui::EndMainMenuBar();
}

void toolbar()
{
	if (rimgui::begin_toolbar("Toolbar")) {
		rimgui::button("New");
		ImGui::SameLine();
		rimgui::button("Open");
		ImGui::SameLine();
		rimgui::button("Save");
		rimgui::divider();
		rimgui::button("Undo");
		ImGui::SameLine();
		rimgui::button("Redo", rimgui::Tone::Normal, false, "Nothing to redo");
		rimgui::divider();
		if (rimgui::button(state.running ? "Pause" : "Run", state.running ? rimgui::Tone::Normal : rimgui::Tone::Go)) {
			state.running = !state.running;
			state.log.push_back(state.running ? "Running." : "Paused.");
		}
		ImGui::SameLine();
		if (rimgui::button("Stop", state.running ? rimgui::Tone::Stop : rimgui::Tone::Normal, state.running)) {
			state.running = false;
			state.log.push_back("Stopped.");
		}
		ImGui::SameLine(0, rimgui::px(14));
		ImGui::AlignTextToFramePadding();
		if (state.running) {
			rimgui::text_good("RUNNING");
		} else {
			ImGui::TextDisabled("Ready");
		}
	}
	rimgui::end_toolbar();
}

void item_list()
{
	ImGui::Begin("Items");
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##search", "Search items", state.search.data(), state.search.size());
	rimgui::chip("Mesh", &state.show_meshes);
	ImGui::SameLine();
	rimgui::chip("Light", &state.show_lights);
	ImGui::Separator();
	const std::string wanted = state.search.data();
	for (int i = 0; i < static_cast<int>(state.items.size()); ++i) {
		Item &item = state.items[static_cast<std::size_t>(i)];
		if (!wanted.empty() && item.name.find(wanted) == std::string::npos) {
			continue;
		}
		if ((state.show_meshes || state.show_lights) && !((state.show_meshes && item.kind == "mesh") || (state.show_lights && item.kind == "light"))) {
			continue;
		}
		ImGui::PushID(i);
		if (!item.visible) {
			ImGui::PushStyleColor(ImGuiCol_Text, rimgui::palette().muted);
		}
		if (ImGui::Selectable(item.name.c_str(), state.selected == i, ImGuiSelectableFlags_AllowOverlap)) {
			state.selected = i;
		}
		if (!item.visible) {
			ImGui::PopStyleColor();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", item.kind.c_str());
		if (rimgui::row_toggle("##visible", item.visible, item.visible ? "Click to hide" : "Hidden. Click to show", ImGui::GetFrameHeight())) {
			item.visible = !item.visible;
		}
		ImGui::PopID();
	}
	ImGui::End();
}

void details()
{
	ImGui::Begin("Details");
	if (state.selected < 0 || state.selected >= static_cast<int>(state.items.size())) {
		rimgui::hint("Pick an item on the left to see its details here.");
		ImGui::End();
		return;
	}
	Item &item = state.items[static_cast<std::size_t>(state.selected)];
	ImGui::TextDisabled("%s", item.kind.c_str());
	if (rimgui::section("Transform") && rimgui::begin_properties("##transform")) {
		rimgui::property("Position", "Where it is, in metres");
		ImGui::DragFloat3("##position", item.position, 0.05f, 0, 0, "%.2f");
		rimgui::end_properties();
	}
	if (rimgui::section("Look") && rimgui::begin_properties("##look")) {
		rimgui::property("Color");
		ImGui::ColorEdit3("##color", item.color);
		rimgui::property("Quality");
		ImGui::Combo("##quality", &item.quality, "Low\0Medium\0High\0");
		rimgui::property("Visible");
		ImGui::Checkbox("##visible", &item.visible);
		rimgui::end_properties();
	}
	if (rimgui::section("Notes", false)) {
		rimgui::hint("Sections start open or closed; property rows keep names on the left and values on the right.");
	}
	ImGui::End();
}

void main_view()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
	ImGui::Begin("View");
	ImGui::PopStyleVar();
	const ImVec2 min = ImGui::GetCursorScreenPos();
	const ImVec2 size = ImGui::GetContentRegionAvail();
	const ImVec2 max{min.x + size.x, min.y + size.y};
	ImDrawList *draw = ImGui::GetWindowDrawList();
	draw->AddRectFilledMultiColor(min, max, IM_COL32(38, 44, 56, 255), IM_COL32(38, 44, 56, 255), IM_COL32(20, 22, 28, 255), IM_COL32(20, 22, 28, 255));
	for (float x = min.x; x < max.x; x += rimgui::px(40)) {
		draw->AddLine({x, min.y}, {x, max.y}, IM_COL32(255, 255, 255, 12));
	}
	for (float y = min.y; y < max.y; y += rimgui::px(40)) {
		draw->AddLine({min.x, y}, {max.x, y}, IM_COL32(255, 255, 255, 12));
	}
	ImGui::SetCursorScreenPos({min.x + rimgui::px(10), min.y + rimgui::px(10)});
	rimgui::button("Perspective");
	ImGui::SameLine();
	rimgui::button("Lit");
	rimgui::overlay_caption(draw, min, max, "Your main view goes here: a canvas, a 3D view, a document");
	// The view's whole area takes the mouse, for your own dragging and clicking.
	ImGui::SetCursorScreenPos(min);
	ImGui::InvisibleButton("##canvas", {std::max(size.x, 1.0f), std::max(size.y, 1.0f)});
	ImGui::End();
}

void log_panel()
{
	ImGui::Begin("Log");
	if (rimgui::button("Clear")) {
		state.log.clear();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(rimgui::px(160));
	ImGui::ProgressBar(state.progress, {0, 0});
	ImGui::BeginChild("##lines", {0, 0}, ImGuiChildFlags_Borders);
	for (const auto &line : state.log) {
		ImGui::TextWrapped("%s", line.c_str());
	}
	rimgui::text_good("Saved.");
	rimgui::text_warning("A warning looks like this.");
	rimgui::text_error("An error looks like this.");
	ImGui::EndChild();
	ImGui::End();
}

void code_panel()
{
	ImGui::Begin("Script");
	rimgui::push_code_font();
	static std::array<char, 512> text{"-- Code is set in the fixed-width font.\nfunction on_start()\n    print(\"hello\")\nend\n"};
	ImGui::InputTextMultiline("##code", text.data(), text.size(), {-FLT_MIN, -FLT_MIN});
	ImGui::PopFont();
	ImGui::End();
}

} // namespace

int main(int argc, char **argv)
{
	rimgui::AppConfig config;
	config.title = "StarterApp";
	config.first_layout = [](const rimgui::DockAreas &areas) {
		ImGui::DockBuilderDockWindow("Toolbar", areas.top);
		ImGui::DockBuilderDockWindow("Items", areas.left);
		ImGui::DockBuilderDockWindow("Details", areas.right);
		ImGui::DockBuilderDockWindow("View", areas.center);
		ImGui::DockBuilderDockWindow("Log", areas.bottom);
		ImGui::DockBuilderDockWindow("Script", areas.bottom_right);
	};
	return rimgui::run(config, [] {
		menu_bar();
		toolbar();
		item_list();
		details();
		main_view();
		log_panel();
		code_panel();
	}, argc, argv);
}
