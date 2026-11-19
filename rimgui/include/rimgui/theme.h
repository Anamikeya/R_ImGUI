#pragma once
// R_ImGUI's look: the theme of IdlyEngine's editor, for any Dear ImGui application.
//
//   Colours   charcoal panels, one blue for what is selected or active, green to start and red to stop
//   Text      Segoe UI at 15 px for the interface, Consolas for code (ImGui's own font where those are missing)
//   Spacing   a little more room than ImGui's defaults, rounded corners, thin dividers
//   Scale     every size follows the monitor's scale; px() turns a length written for 100% into pixels now
#include <imgui.h>

namespace rimgui {

// The interface's text at 100% scale. Lengths written for it grow with the text (px).
inline constexpr float base_font_size = 15;

// A length written for 100% scale, in pixels at the current font size.
float px(float length);

// The theme's colours, for drawing your own widgets in the same hand.
struct Palette {
	ImVec4 text{0.90f, 0.91f, 0.93f, 1};
	ImVec4 muted{0.54f, 0.56f, 0.60f, 1};           // Hints, disabled text, tags beside names.
	ImVec4 deep{0.075f, 0.078f, 0.086f, 1};          // Menu bar, tab bar, empty dock space.
	ImVec4 panel{0.115f, 0.120f, 0.130f, 1};         // Window backgrounds.
	ImVec4 raised{0.150f, 0.156f, 0.170f, 1};        // Popups, table headers.
	ImVec4 field{0.185f, 0.192f, 0.210f, 1};         // Inputs and buttons.
	ImVec4 field_hot{0.235f, 0.245f, 0.270f, 1};
	ImVec4 field_on{0.280f, 0.295f, 0.330f, 1};
	ImVec4 line{0.235f, 0.245f, 0.265f, 1};          // Borders and separators.
	ImVec4 band{0.200f, 0.212f, 0.240f, 1};          // Section bars (rimgui::section).
	ImVec4 accent{0.26f, 0.53f, 0.96f, 1};           // Selection, focus, sliders.
	ImVec4 accent_soft{0.20f, 0.38f, 0.66f, 1};
	ImVec4 accent_bright{0.47f, 0.70f, 1.0f, 1};
	ImVec4 go{0.16f, 0.42f, 0.24f, 1};               // Play, Run, Start.
	ImVec4 go_hot{0.20f, 0.54f, 0.30f, 1};
	ImVec4 stop{0.52f, 0.18f, 0.17f, 1};             // Stop, Delete, Cancel a run.
	ImVec4 stop_hot{0.66f, 0.23f, 0.21f, 1};
	ImVec4 good{0.45f, 0.85f, 0.45f, 1};             // Status text: done, running.
	ImVec4 warning{0.95f, 0.75f, 0.30f, 1};
	ImVec4 error{0.95f, 0.38f, 0.35f, 1};
};
const Palette &palette();

// Colours, rounding and spacing, at 100% scale. Call scale_style() after it for another monitor scale.
void apply_theme(ImGuiStyle &style);

// The interface's font and, second, a fixed-width one for code, at `pixel_size` (base_font_size times the scale the
// application opens at). Call once, before the first frame.
void load_fonts(float pixel_size);
// The fixed-width font; null when only one font could be loaded (then the interface's font is used).
ImFont *code_font();
// Code text: the fixed-width font, a little smaller than the interface's text so the two look alike. Pop with
// ImGui::PopFont().
void push_code_font();

// Re-applies the theme at `scale` (a monitor's content scale), keeping the fonts built for `font_scale`.
void scale_style(float scale, float font_scale);

} // namespace rimgui
