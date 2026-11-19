#pragma once
// The pieces IdlyEngine's editor is made of, in R_ImGUI's theme.
#include <imgui.h>

namespace rimgui {

// ---- Toolbar: a strip docked at the top, with no tab of its own ---------------------------------------------------
// Between begin_toolbar and end_toolbar, buttons go on one line with SameLine(); divider() groups them.
bool begin_toolbar(const char *title, bool *open = nullptr);
void end_toolbar();
void divider();

// ---- Buttons ------------------------------------------------------------------------------------------------------
enum class Tone { Normal, Go, Stop, Accent };
// A button in a tone: Go (green) to start something, Stop (red) to end it, Accent (blue) for the main action.
// Disabled draws it greyed and returns false. `tooltip` shows on hover, also when disabled.
bool button(const char *label, Tone tone = Tone::Normal, bool enabled = true, const char *tooltip = nullptr,
	ImVec2 size = {0, 0});
// A small square toggle at the right of a list row (like the editor's eye and lock): covers what the row wrote there.
bool row_toggle(const char *id, bool on, const char *tooltip, float right_offset);

// ---- Property panels ----------------------------------------------------------------------------------------------
// A section with a quiet band for a bar, open at first. Content follows when it returns true; no end call needed.
bool section(const char *title, bool open_at_first = true);
// A two-column table: names on the left, values on the right, filling the panel.
bool begin_properties(const char *id);
void end_properties();
// One row: the name, then the value widget the caller draws next, as wide as the column. Use "##id" labels for it.
void property(const char *name, const char *tooltip = nullptr);
// In a panel that is not a property table, value fields that leave room for their labels on the right.
void fit_items_to_labels(float label_width = 118);

// ---- Feedback -----------------------------------------------------------------------------------------------------
// Muted text that wraps: a sentence telling the user what to do next.
void hint(const char *text);
// Coloured status lines.
void text_good(const char *text);
void text_warning(const char *text);
void text_error(const char *text);
// A small shaded caption at the bottom left of a rectangle (a viewport's navigation help).
void overlay_caption(ImDrawList *draw, ImVec2 min, ImVec2 max, const char *text);
// A chip: a small rounded toggle, as for filters ("Mesh", "Light").
bool chip(const char *label, bool *on);

} // namespace rimgui
